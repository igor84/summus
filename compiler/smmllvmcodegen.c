#include "smmllvmcodegen.h"
#include "llvm-c/Core.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/TargetMachine.h"

#include <assert.h>
#include <stdio.h>

struct SmmLLVMCodeGenData {
	LLVMModuleRef llvmModule;
	PIbsDict localVars;
	LLVMBuilderRef builder;
	LLVMValueRef curFunc;
	LLVMBasicBlockRef endBlock; // Used for logical expressions
};
typedef struct SmmLLVMCodeGenData* PSmmLLVMCodeGenData;

#define MAX_LOGICAL_EXPR_DEPTH 100

struct LogicalExprData {
	PSmmLLVMCodeGenData data;
	LLVMBasicBlockRef lastCreatedBlock;
	LLVMBasicBlockRef incomeBlocks[MAX_LOGICAL_EXPR_DEPTH];
	LLVMValueRef incomeValues[MAX_LOGICAL_EXPR_DEPTH];
	uint8_t blockCount;
};
typedef struct LogicalExprData* PLogicalExprData;

static LLVMValueRef processExpression(PSmmLLVMCodeGenData data, PSmmAstNode expr, PIbsAllocator a);
static void processBlock(PSmmLLVMCodeGenData data, PSmmAstBlockNode block, PIbsAllocator a);
static void processStatement(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PIbsAllocator a);

static LLVMTypeRef getLLVMType(PSmmTypeInfo type) {
	if (!type) return LLVMVoidType();
	switch (type->kind) {
	case tiSmmInt8: case tiSmmInt16: case tiSmmInt32: case tiSmmInt64:
	case tiSmmUInt8: case tiSmmUInt16: case tiSmmUInt32: case tiSmmUInt64:
		return LLVMIntType(type->sizeInBytes << 3);
	case tiSmmFloat32: return LLVMFloatType();
	case tiSmmFloat64: return LLVMDoubleType();
	case tiSmmBool: return LLVMInt1Type();
	default:
		// custom types should be handled here
		assert(false && "Custom types are not yet supported");
		return NULL;
	}
}

static LLVMValueRef getCastInstruction(PSmmLLVMCodeGenData data, PSmmTypeInfo dtype, PSmmTypeInfo stype, LLVMValueRef val) {
	if (dtype->isInt && stype->isFloat) {
		//if dest is int and node is float
		if (dtype->isUnsigned) {
			return LLVMBuildFPToUI(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildFPToSI(data->builder, val, getLLVMType(dtype), "");
	}
	if ((dtype->isFloat) && (stype->isInt)) {
		//if dest is float and node is int
		if (stype->isUnsigned) {
			return LLVMBuildUIToFP(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildSIToFP(data->builder, val, getLLVMType(dtype), "");
	}

	bool dstIsInt = dtype->isInt || dtype->isBool;
	bool srcIsInt = stype->isInt || stype->isBool;
	bool differentSize = dtype->sizeInBytes != stype->sizeInBytes || dtype->kind == tiSmmBool;
	if (dstIsInt && srcIsInt && differentSize) {
		if ((stype->isUnsigned && dtype->sizeInBytes > stype->sizeInBytes) || stype->kind == tiSmmBool) {
			return LLVMBuildZExt(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildIntCast(data->builder, val, getLLVMType(dtype), "");
	}

	if (dtype->isFloat && stype->isFloat && dtype->sizeInBytes != stype->sizeInBytes) {
		return LLVMBuildFPCast(data->builder, val, getLLVMType(dtype), "");
	}

	return val;
}

static LLVMValueRef processAndOrInstr(PLogicalExprData ledata, PSmmAstNode node,
		LLVMBasicBlockRef trueBlock, LLVMBasicBlockRef falseBlock, PIbsAllocator a) {
	LLVMBasicBlockRef newRightBlock = LLVMInsertBasicBlock(ledata->lastCreatedBlock, "");
	LLVMBasicBlockRef prevLastBlock = ledata->lastCreatedBlock;
	ledata->lastCreatedBlock = newRightBlock;
	LLVMBasicBlockRef nextTrue = trueBlock;
	LLVMBasicBlockRef nextFalse = falseBlock;

	if (node->kind == nkSmmAndOp) {
		nextTrue = newRightBlock;
	} else {
		nextFalse = newRightBlock;
	}

	LLVMValueRef left = NULL;
	switch (node->left->kind) {
	case nkSmmAndOp: case nkSmmOrOp: left = processAndOrInstr(ledata, node->left, nextTrue, nextFalse, a); break;
	default: left = processExpression(ledata->data, node->left, a); break;
	}

	LLVMBuildCondBr(ledata->data->builder, left, nextTrue, nextFalse);
	if (ledata->data->endBlock == nextTrue || ledata->data->endBlock == nextFalse) {
		if (ledata->blockCount >= MAX_LOGICAL_EXPR_DEPTH - 1) { // We are leaving one for the end block
			char msg[500] = { 0 };
			snprintf(msg, 500, "Logical expression at %s:%d too complicated", node->token->filePos.filename, node->token->filePos.lineNumber);
			smmAbortWithMessage(msg, __FILE__, __LINE__);
		}
		ledata->incomeBlocks[ledata->blockCount] = LLVMGetInsertBlock(ledata->data->builder);
		ledata->incomeValues[ledata->blockCount] = LLVMConstInt(LLVMInt1Type(), ledata->data->endBlock == nextTrue, false);
		ledata->blockCount++;
	}

	LLVMPositionBuilderAtEnd(ledata->data->builder, newRightBlock);
	ledata->lastCreatedBlock = prevLastBlock;

	switch (node->right->kind) {
	case nkSmmAndOp: case nkSmmOrOp:
		return processAndOrInstr(ledata, node->right, trueBlock, falseBlock, a);
	default: return processExpression(ledata->data, node->right, a);
	}
}

static LLVMValueRef processExpression(PSmmLLVMCodeGenData data, PSmmAstNode expr, PIbsAllocator a) {
	assert(LLVMFRem - LLVMAdd == nkSmmFRem - nkSmmAdd);
	LLVMValueRef res = NULL;

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
		{
			LLVMValueRef left = processExpression(data, expr->left, a);
			LLVMValueRef right = processExpression(data, expr->right, a);
			res = LLVMBuildBinOp(data->builder, expr->kind - nkSmmAdd + LLVMAdd, left, right, "");
			break;
		}
	case nkSmmAndOp: case nkSmmOrOp:
		{
			// We initialize data.endBlock with new block
			LLVMBasicBlockRef lastEndBlock = data->endBlock;
			if (lastEndBlock) {
				data->endBlock = LLVMInsertBasicBlock(lastEndBlock, "");
			} else {
				data->endBlock = LLVMAppendBasicBlock(data->curFunc, "");
			}
			struct LogicalExprData logicalExprData = { data, data->endBlock };

			res = processAndOrInstr(&logicalExprData, expr, data->endBlock, data->endBlock, a);
			logicalExprData.incomeBlocks[logicalExprData.blockCount] = LLVMGetInsertBlock(data->builder);
			logicalExprData.incomeValues[logicalExprData.blockCount] = res;
			logicalExprData.blockCount++;
			LLVMBuildBr(data->builder, data->endBlock);

			LLVMPositionBuilderAtEnd(data->builder, data->endBlock);
			res = LLVMBuildPhi(data->builder, LLVMInt1Type(), "");
			LLVMAddIncoming(res, logicalExprData.incomeValues, logicalExprData.incomeBlocks, logicalExprData.blockCount);

			data->endBlock = lastEndBlock;

			break;
		}
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			LLVMValueRef left = processExpression(data, expr->left, a);
			LLVMValueRef right = processExpression(data, expr->right, a);
			if (expr->left->type->isInt || expr->left->type->kind == tiSmmBool) {
				LLVMIntPredicate op;
				if (expr->kind == nkSmmXorOp) op = LLVMIntNE;
				else {
					op = expr->kind - nkSmmEq + LLVMIntEQ;
					if (op >= LLVMIntUGT && !expr->left->type->isUnsigned) {
						op = op - LLVMIntUGT + LLVMIntSGT;
					}
				}
				res = LLVMBuildICmp(data->builder, op, left, right, "");
			} else if (expr->left->type->isFloat) {
				LLVMRealPredicate op = LLVMRealUNE; // For '!=', it can't be xor here because we add != 0 on float operands for xor
				switch (expr->kind) {
				case nkSmmEq: op = LLVMRealOEQ; break;
				case nkSmmGt: op = LLVMRealOGT; break;
				case nkSmmGtEq: op = LLVMRealOGE; break;
				case nkSmmLt: op = LLVMRealOLT; break;
				case nkSmmLtEq: op = LLVMRealOLE; break;
				default:
					// TODO(igors): Add rest of float comparisons as DLang: https://dlang.org/spec/expression.html#floating-point-comparisons
					assert(false && "Unexpected node type");
					break;
				}
				res = LLVMBuildFCmp(data->builder, op, left, right, "");
			} else {
				assert(false && "Got unexpected type for relation operator");
			}
			break;
		}
	case nkSmmNeg:
		{
			LLVMValueRef operand = processExpression(data, expr->left, a);
			res = LLVMBuildNeg(data->builder, operand, ""); 
			break;
		}
	case nkSmmNot:
		{
			LLVMValueRef operand = processExpression(data, expr->left, a);
			res = LLVMBuildNot(data->builder, operand, "");
			break;
		}
	case nkSmmCast:
		{
			LLVMValueRef operand = processExpression(data, expr->left, a);
			res = getCastInstruction(data, expr->type, expr->left->type, operand);
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			LLVMValueRef func = ibsDictGet(data->localVars, callNode->token->stringVal);
			LLVMValueRef* args = NULL;
			size_t argCount = 0;
			if (callNode->params) {
				argCount = callNode->params->count;
				PSmmAstNode astArg = callNode->args;
				args = ibsAlloc(a, argCount * sizeof(args[0]));
				for (size_t i = 0; i < argCount; i++) {
					args[i] = processExpression(data, astArg, a);
					astArg = astArg->next;
				}
			}
			res = LLVMBuildCall(data->builder, func, args, (unsigned)argCount, "");
			break;
		}
	case nkSmmParam: case nkSmmIdent:
		res = ibsDictGet(data->localVars, expr->token->repr);
		res = LLVMBuildLoad(data->builder, res, "");
		LLVMSetAlignment(res, expr->type->sizeInBytes);
		break;
	case nkSmmConst:
		res = ibsDictGet(data->localVars, expr->token->repr);
		break;
	case nkSmmInt:
		{
			bool signExtend = !expr->type->isUnsigned;
			LLVMTypeRef intType = LLVMIntType(expr->type->sizeInBytes << 3);
			res = LLVMConstInt(intType, expr->token->uintVal, signExtend);
			break;
		}
	case nkSmmFloat:
		if (expr->type->kind == tiSmmFloat32) {
			res = LLVMConstReal(LLVMFloatType(), expr->token->floatVal);
		} else {
			res = LLVMConstReal(LLVMDoubleType(), expr->token->floatVal);
		}
		break;
	case nkSmmBool:
		res = LLVMConstInt(LLVMInt1Type(), expr->token->boolVal, false);
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
	return res;
}

static void processLocalSymbols(PSmmLLVMCodeGenData data, PSmmAstDeclNode decl, PIbsAllocator a) {
	while (decl) {
		LLVMTypeRef type = getLLVMType(decl->left->type);

		LLVMValueRef var = NULL;
		PSmmToken varToken = decl->left->left->token;

		if (decl->left->left->kind == nkSmmIdent) {
			var = LLVMBuildAlloca(data->builder, type, varToken->repr);
			LLVMSetAlignment(var, decl->left->left->type->sizeInBytes);
		} else if (decl->left->left->kind == nkSmmConst) {
			var = processExpression(data, decl->left->right, a);
		} else {
			assert(false && "Declaration of unknown node kind");
		}

		ibsDictPut(data->localVars, varToken->repr, var);

		decl = decl->nextDecl;
	}
}

static void processAssignment(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PIbsAllocator a) {
	LLVMValueRef val = processExpression(data, stmt->right, a);
	LLVMValueRef left = ibsDictGet(data->localVars, stmt->left->token->repr);
	LLVMValueRef res = LLVMBuildStore(data->builder, val, left);
	LLVMSetAlignment(res, stmt->left->type->sizeInBytes);
}

static void processReturn(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PIbsAllocator a) {
	if (stmt->left) {
		LLVMValueRef val = processExpression(data, stmt->left, a);
		LLVMBuildRet(data->builder, val);
	} else LLVMBuildRetVoid(data->builder);
}

static void processIf(PSmmLLVMCodeGenData data, PSmmAstIfWhileNode stmt, PIbsAllocator a) {
	LLVMBasicBlockRef trueBlock = LLVMAppendBasicBlock(data->curFunc, "if.then");
	LLVMBasicBlockRef falseBlock;
	LLVMBasicBlockRef endBlock;
	if (stmt->elseBody) {
		falseBlock = LLVMAppendBasicBlock(data->curFunc, "if.else");
		endBlock = LLVMAppendBasicBlock(data->curFunc, "if.end");
	} else {
		falseBlock = LLVMAppendBasicBlock(data->curFunc, "if.end");
		endBlock = falseBlock;
	}
	LLVMValueRef res;
	data->endBlock = trueBlock;
	if (stmt->cond->kind == nkSmmAndOp || stmt->cond->kind == nkSmmOrOp) {
		struct LogicalExprData logicalExprData = { data, data->endBlock };
		res = processAndOrInstr(&logicalExprData, stmt->cond, trueBlock, falseBlock, a);
	} else {
		res = processExpression(data, stmt->cond, a);
	}
	data->endBlock = NULL;
	LLVMBuildCondBr(data->builder, res, trueBlock, falseBlock);

	LLVMPositionBuilderAtEnd(data->builder, trueBlock);

	processStatement(data, stmt->body, a);
	LLVMBuildBr(data->builder, falseBlock);
	LLVMPositionBuilderAtEnd(data->builder, falseBlock);
	if (stmt->elseBody) {
		processStatement(data, stmt->elseBody, a);
		LLVMBuildBr(data->builder, endBlock);
		LLVMPositionBuilderAtEnd(data->builder, endBlock);
	}
}

static void processWhile(PSmmLLVMCodeGenData data, PSmmAstIfWhileNode stmt, PIbsAllocator a) {
	LLVMBasicBlockRef condBlock = LLVMAppendBasicBlock(data->curFunc, "while.cond");
	LLVMBasicBlockRef trueBlock = LLVMAppendBasicBlock(data->curFunc, "while.body");
	LLVMBasicBlockRef falseBlock = LLVMAppendBasicBlock(data->curFunc, "while.end");
	LLVMBuildBr(data->builder, condBlock);
	LLVMPositionBuilderAtEnd(data->builder, condBlock);
	LLVMValueRef res;
	data->endBlock = trueBlock; // We initialize data.endBlock with new block
	if (stmt->cond->kind == nkSmmAndOp || stmt->cond->kind == nkSmmOrOp) {
		struct LogicalExprData logicalExprData = { data, data->endBlock };
		res = processAndOrInstr(&logicalExprData, stmt->cond, trueBlock, falseBlock, a);
	} else {
		res = processExpression(data, stmt->cond, a);
	}
	data->endBlock = NULL;
	LLVMBuildCondBr(data->builder, res, trueBlock, falseBlock);

	LLVMPositionBuilderAtEnd(data->builder, trueBlock);

	processStatement(data, stmt->body, a);
	LLVMBuildBr(data->builder, condBlock);
	LLVMPositionBuilderAtEnd(data->builder, falseBlock);
}

static void processStatement(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PIbsAllocator a) {
	switch (stmt->kind) {
	case nkSmmBlock:
		{
			PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
			processLocalSymbols(data, newBlock->scope->decls, a);
			processBlock(data, newBlock, a);
			break;
		}
	case nkSmmAssignment: processAssignment(data, stmt, a); break;
	case nkSmmIf: processIf(data, &stmt->asIfWhile, a); break;
	case nkSmmWhile: processWhile(data, &stmt->asIfWhile, a); break;
	case nkSmmDecl:
		if (stmt->left->left->asIdent.level == 0) {
			LLVMTypeRef type = getLLVMType(stmt->left->type);
			LLVMValueRef globalVar = LLVMAddGlobal(data->llvmModule, type, stmt->left->left->token->repr);
			LLVMSetGlobalConstant(globalVar, false);
			LLVMSetInitializer(globalVar, processExpression(data, stmt->left->right, a));
		} else {
			processAssignment(data, stmt->left, a);
		}
		break;
	case nkSmmReturn: processReturn(data, stmt, a); break;
	default:
		processExpression(data, stmt, a); break;
	}
}

static void processBlock(PSmmLLVMCodeGenData data, PSmmAstBlockNode block, PIbsAllocator a) {
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		processStatement(data, stmt, a);
		stmt = stmt->next;
	}
}

static LLVMValueRef createFunc(PSmmLLVMCodeGenData data, PSmmAstFuncDefNode astFunc, PIbsAllocator a) {
	LLVMTypeRef returnType = getLLVMType(astFunc->returnType);
	LLVMTypeRef* params = NULL;
	size_t paramsCount = 0;
	if (astFunc->params) {
		PSmmAstParamNode param = astFunc->params;
		paramsCount = param->count;
		params = ibsAlloc(a, paramsCount * sizeof(LLVMTypeRef));
		for (size_t i = 0; i < paramsCount; i++) {
			params[i] = getLLVMType(param->type);
			param = param->next;
		}
	}
	LLVMTypeRef funcType = LLVMFunctionType(returnType, params, (unsigned)paramsCount, false);
	LLVMValueRef func = LLVMAddFunction(data->llvmModule, astFunc->token->stringVal, funcType);
	ibsDictPush(data->localVars, astFunc->token->stringVal, func);
	return func;
}

static void processGlobalSymbols(PSmmLLVMCodeGenData data, PSmmAstDeclNode decl, PIbsAllocator a) {
	while (decl) {

		if (decl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;

			LLVMValueRef func = createFunc(data, (PSmmAstFuncDefNode)decl->left, a);

			if (funcNode->body) {
				LLVMBasicBlockRef prevBlock = LLVMGetInsertBlock(data->builder);
				LLVMValueRef prevFunc = data->curFunc;
				data->curFunc = func;
				LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
				LLVMPositionBuilderAtEnd(data->builder, entry);
				size_t paramsCount = 0;
				LLVMValueRef* paramAllocs = NULL;
				LLVMValueRef* paramVals = NULL;

				if (funcNode->params) {
					paramsCount = funcNode->params->count;
					paramAllocs = ibsAlloc(a, paramsCount * sizeof(LLVMValueRef));
					paramVals = ibsAlloc(a, paramsCount * sizeof(LLVMValueRef));
					LLVMGetParams(func, paramVals);
					PSmmAstParamNode param = funcNode->params;
					for (size_t i = 0; i < paramsCount; i++) {
						LLVMSetValueName(paramVals[i], param->token->repr);
						paramAllocs[i] = LLVMBuildAlloca(data->builder, LLVMTypeOf(paramVals[i]), "");
						ibsDictPush(data->localVars, param->token->repr, paramAllocs[i]);
						param = param->next;
					}
				}

				processLocalSymbols(data, funcNode->body->scope->decls, a);
				if (paramsCount > 0) {
					for (size_t i = 0; i < paramsCount; i++) {
						LLVMBuildStore(data->builder, paramVals[i], paramAllocs[i]);
					}
				}

				processBlock(data, funcNode->body, a);

				PSmmAstParamNode param = funcNode->params;
				while (param) {
					ibsDictPop(data->localVars, param->token->repr);
					param = param->next;
				}

				LLVMPositionBuilderAtEnd(data->builder, prevBlock);
				data->curFunc = prevFunc;

				LLVMVerifyFunction(func, LLVMPrintMessageAction);
			}
		} else if (decl->left->kind == nkSmmConst) {
			assert(decl->left->right && "Global var must have initializer");
			LLVMValueRef globalConst = processExpression(data, decl->left->right, a);

			PSmmToken varToken = decl->left->token;
			ibsDictPut(data->localVars, varToken->repr, globalConst);
		}
		decl = decl->nextDecl;
	}
}

bool smmExecuteLLVMCodeGenPass(PSmmAstNode module, FILE* out, PIbsAllocator a) {
	PIbsAllocator la = ibsSimpleAllocatorCreate("llvmTempAllocator", a->size);
	PSmmLLVMCodeGenData data = ibsAlloc(la, sizeof(struct SmmLLVMCodeGenData));
	data->localVars = ibsDictCreate(la);

	data->llvmModule = LLVMModuleCreateWithName(module->token->repr);
	LLVMSetDataLayout(data->llvmModule, "");
	LLVMSetTarget(data->llvmModule, LLVMGetDefaultTargetTriple());

	data->builder = LLVMCreateBuilder();

	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(data, globalBlock->scope->decls, la);

	LLVMTypeRef funcType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
	LLVMValueRef mainfunc = LLVMAddFunction(data->llvmModule, "main", funcType);
	data->curFunc = mainfunc;

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainfunc, "entry");
	LLVMPositionBuilderAtEnd(data->builder, entry);
	processBlock(data, globalBlock, la);

	char *error = NULL;
	bool isInvalid = LLVMVerifyModule(data->llvmModule, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	if (!isInvalid) {
		char* outData = LLVMPrintModuleToString(data->llvmModule);
		fputs(outData, out);
		LLVMDisposeMessage(outData);
	}
	ibsSimpleAllocatorFree(la);
	return !isInvalid;
}
