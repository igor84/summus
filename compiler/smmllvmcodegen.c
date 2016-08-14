#include "smmllvmcodegen.h"
#include "llvm-c/Core.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/TargetMachine.h"

#include <assert.h>
#include <stdio.h>

struct SmmLLVMCodeGenData {
	LLVMModuleRef llvmModule;
	PSmmDict localVars;
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

static LLVMValueRef processExpression(PSmmLLVMCodeGenData data, PSmmAstNode expr, PSmmAllocator a);

static LLVMTypeRef getLLVMType(PSmmTypeInfo type) {
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
		LLVMBasicBlockRef trueBlock, LLVMBasicBlockRef falseBlock, PSmmAllocator a) {
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

static LLVMValueRef processExpression(PSmmLLVMCodeGenData data, PSmmAstNode expr, PSmmAllocator a) {
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
			LLVMValueRef func = smmGetDictValue(data->localVars, callNode->token->repr, false);
			LLVMValueRef* args = NULL;
			size_t argCount = 0;
			if (callNode->params) {
				argCount = callNode->params->count;
				PSmmAstNode astArg = callNode->args;
				args = a->alloca(a, argCount * sizeof(args[0]));
				for (size_t i = 0; i < argCount; i++) {
					args[i] = processExpression(data, astArg, a);
					astArg = astArg->next;
				}
			}
			res = LLVMBuildCall(data->builder, func, args, (unsigned)argCount, "");
			if (args) a->freea(a, args);
			break;
		}
	case nkSmmParam: case nkSmmIdent:
		res = smmGetDictValue(data->localVars, expr->token->repr, false);
		res = LLVMBuildLoad(data->builder, res, "");
		LLVMSetAlignment(res, expr->type->sizeInBytes);
		break;
	case nkSmmConst:
		res = smmGetDictValue(data->localVars, expr->token->repr, false);
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

static void processLocalSymbols(PSmmLLVMCodeGenData data, PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		LLVMTypeRef type = getLLVMType(decl->left->type);

		LLVMValueRef var = NULL;

		if (decl->left->kind == nkSmmIdent) {
			var = LLVMBuildAlloca(data->builder, type, decl->left->token->repr);
			LLVMSetAlignment(var, decl->left->type->sizeInBytes);
		} else if (decl->left->kind == nkSmmConst) {
			var = processExpression(data, decl->right, a);
		} else {
			assert(false && "Declaration of unknown node kind");
		}

		PSmmToken varToken = decl->left->token;
		smmAddDictValue(data->localVars, varToken->repr, var);

		decl = decl->next;
	}
}

static void processAssignment(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PSmmAllocator a) {
	LLVMValueRef val = processExpression(data, stmt->right, a);
	LLVMValueRef left = smmGetDictValue(data->localVars, stmt->left->token->repr, false);
	LLVMValueRef res = LLVMBuildStore(data->builder, val, left);
	LLVMSetAlignment(res, stmt->left->type->sizeInBytes);
}

static void processReturn(PSmmLLVMCodeGenData data, PSmmAstNode stmt, PSmmAllocator a) {
	if (stmt->left) {
		LLVMValueRef val = processExpression(data, stmt->left, a);
		LLVMBuildRet(data->builder, val);
	} else LLVMBuildRetVoid(data->builder);
}

static void processBlock(PSmmLLVMCodeGenData data, PSmmAstBlockNode block, PSmmAllocator a) {
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		switch (stmt->kind) {
		case nkSmmBlock:
			{
				PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
				processLocalSymbols(data, newBlock->scope->decls, a);
				processBlock(data, newBlock, a);
				break;
			}
		case nkSmmAssignment: processAssignment(data, stmt, a); break;
		case nkSmmReturn: processReturn(data, stmt, a); break;
		default:
			processExpression(data, stmt, a); break;
		}
		stmt = stmt->next;
	}
}

static LLVMValueRef createFunc(PSmmLLVMCodeGenData data, PSmmAstFuncDefNode astFunc, PSmmAllocator a) {
	LLVMTypeRef returnType = getLLVMType(astFunc->returnType);
	LLVMTypeRef* params = NULL;
	size_t paramsCount = 0;
	if (astFunc->params) {
		PSmmAstParamNode param = astFunc->params;
		paramsCount = param->count;
		params = a->alloca(a, paramsCount * sizeof(LLVMTypeRef));
		for (size_t i = 0; i < paramsCount; i++) {
			params[i] = getLLVMType(param->type);
			param = param->next;
		}
	}
	LLVMTypeRef funcType = LLVMFunctionType(returnType, params, (unsigned)paramsCount, false);
	if (params) {
		a->freea(a, params);
		params = NULL;
	}
	LLVMValueRef func = LLVMAddFunction(data->llvmModule, astFunc->token->repr, funcType);
	smmPushDictValue(data->localVars, astFunc->token->repr, func);
	return func;
}

static void processGlobalSymbols(PSmmLLVMCodeGenData data, PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		LLVMTypeRef type = getLLVMType(decl->left->type);

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
					paramAllocs = a->alloca(a, paramsCount * sizeof(LLVMValueRef));
					paramVals = a->alloca(a, paramsCount * sizeof(LLVMValueRef));
					LLVMGetParams(func, paramVals);
					PSmmAstParamNode param = funcNode->params;
					for (size_t i = 0; i < paramsCount; i++) {
						LLVMSetValueName(paramVals[i], param->token->repr);
						paramAllocs[i] = LLVMBuildAlloca(data->builder, LLVMTypeOf(paramVals[i]), "");
						smmPushDictValue(data->localVars, param->token->repr, paramAllocs[i]);
						param = param->next;
					}
				}

				processLocalSymbols(data, funcNode->body->scope->decls, a);
				if (paramsCount > 0) {
					for (size_t i = 0; i < paramsCount; i++) {
						LLVMBuildStore(data->builder, paramVals[i], paramAllocs[i]);
					}
					a->freea(a, paramVals); // We must free in opposite order of allocation
					a->freea(a, paramAllocs);
				}

				processBlock(data, funcNode->body, a);

				PSmmAstParamNode param = funcNode->params;
				while (param) {
					smmPopDictValue(data->localVars, param->token->repr);
					param = param->next;
				}

				LLVMPositionBuilderAtEnd(data->builder, prevBlock);
				data->curFunc = prevFunc;

				LLVMVerifyFunction(func, LLVMPrintMessageAction);
			}
		} else {
			assert(decl->right && "Global var must have initializer");
			LLVMValueRef val = processExpression(data, decl->right, a);

			LLVMValueRef globalVar = NULL;
			if (decl->left->kind == nkSmmConst) {
				globalVar = val;
			} else {
				globalVar = LLVMAddGlobal(data->llvmModule, type, decl->left->token->repr);
				LLVMSetGlobalConstant(globalVar, false);
				LLVMSetInitializer(globalVar, val);
			}

			PSmmToken varToken = decl->left->token;
			smmAddDictValue(data->localVars, varToken->repr, globalVar);
		}
		decl = decl->next;
	}
}

void smmExecuteLLVMCodeGenPass(PSmmAstNode module, PSmmAllocator a) {
	PSmmLLVMCodeGenData data = a->alloc(a, sizeof(struct SmmLLVMCodeGenData));
	data->localVars = smmCreateDict(a, NULL, NULL);
	data->localVars->storeKeyCopy = false;

	data->llvmModule = LLVMModuleCreateWithName(module->token->repr);
	LLVMSetDataLayout(data->llvmModule, "");
	LLVMSetTarget(data->llvmModule, LLVMGetDefaultTargetTriple());

	LLVMTypeRef funcType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
	LLVMValueRef mainfunc = LLVMAddFunction(data->llvmModule, "main", funcType);
	data->curFunc = mainfunc;

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainfunc, "entry");

	data->builder = LLVMCreateBuilder();
	LLVMPositionBuilderAtEnd(data->builder, entry);

	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(data, globalBlock->scope->decls, a);
	processBlock(data, globalBlock, a);

	char *error = NULL;
	LLVMVerifyModule(data->llvmModule, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	char* err = NULL;
	LLVMPrintModuleToFile(data->llvmModule, "test.ll", &err);
	if (err) {
		printf("\nError Saving Module: %s\n", err);
	} else {
		printf("\nModule saved to test.ll");
	}
}
