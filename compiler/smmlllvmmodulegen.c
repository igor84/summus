#include "smmlllvmmodulegen.h"
#include "llvm-c/Core.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/TargetMachine.h"

#include <assert.h>
#include <stdio.h>

struct SmmLLVMModuleGenData {
	PSmmAstNode module;
	LLVMModuleRef llvmModule;
	PSmmAllocator allocator;
	PSmmDict localVars;
	LLVMBuilderRef builder;
	LLVMValueRef curFunc;
	LLVMBasicBlockRef endBlock; // Used for logical expressions
};
typedef struct SmmLLVMModuleGenData* PSmmLLVMModuleGenData;

#define MAX_LOGICAL_EXPR_DEPTH 100

struct LogicalExprData {
	PSmmLLVMModuleGenData data;
	LLVMBasicBlockRef lastCreatedBlock;
	LLVMBasicBlockRef incomeBlocks[MAX_LOGICAL_EXPR_DEPTH];
	LLVMValueRef incomeValues[MAX_LOGICAL_EXPR_DEPTH];
	uint8_t blockCount;
};
typedef struct LogicalExprData* PLogicalExprData;

LLVMValueRef convertToInstructions(PSmmLLVMModuleGenData data, PSmmAstNode node);

LLVMTypeRef getLLVMFloatType(PSmmTypeInfo type) {
	if (type->kind == tiSmmFloat32) return LLVMFloatType();
	return LLVMDoubleType();
}

LLVMTypeRef getLLVMType(PSmmTypeInfo type) {
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

LLVMValueRef getZeroForType(PSmmTypeInfo type) {
	LLVMTypeRef llvmType = getLLVMType(type);
	switch (type->kind) {
	case tiSmmInt8: case tiSmmInt16: case tiSmmInt32: case tiSmmInt64:
		return LLVMConstInt(llvmType, 0, true);
	case tiSmmBool: case tiSmmUInt8: case tiSmmUInt16: case tiSmmUInt32: case tiSmmUInt64:
		return LLVMConstInt(llvmType, 0, false);
	case tiSmmFloat32: case tiSmmFloat64:
		return LLVMConstReal(llvmType, 0);
	default:
		// custom types should be handled here
		assert(false && "Custom types are not yet supported");
		return NULL;
	}
}

LLVMValueRef getCastInstruction(PSmmLLVMModuleGenData data, PSmmTypeInfo dtype, PSmmTypeInfo stype, LLVMValueRef val) {
	if ((dtype->flags & tifSmmInt) && (stype->flags & tifSmmFloat)) {
		//if dest is int and node is float
		if (dtype->flags & tifSmmUnsigned) {
			return LLVMBuildFPToUI(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildFPToSI(data->builder, val, getLLVMType(dtype), "");
	} 
	if ((dtype->flags & tifSmmFloat) && (stype->flags & tifSmmInt)) {
		//if dest is float and node is int
		if (stype->flags & tifSmmUnsigned) {
			return LLVMBuildUIToFP(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildSIToFP(data->builder, val, getLLVMType(dtype), "");
	}
	
	bool dstIsInt = dtype->flags & (tifSmmInt | tifSmmBool);
	bool srcIsInt = stype->flags & (tifSmmInt | tifSmmBool);
	bool differentSize = dtype->sizeInBytes != stype->sizeInBytes || dtype->kind == tiSmmBool;
	if (dstIsInt && srcIsInt && differentSize) {
		if ((stype->flags & tifSmmUnsigned) && dtype->sizeInBytes > stype->sizeInBytes || stype->kind == tiSmmBool) {
			return LLVMBuildZExt(data->builder, val, getLLVMType(dtype), "");
		}
		return LLVMBuildIntCast(data->builder, val, getLLVMType(dtype), "");
	}

	if ((dtype->flags & stype->flags & tifSmmFloat) == tifSmmFloat && dtype->sizeInBytes != stype->sizeInBytes) {
		return LLVMBuildFPCast(data->builder, val, getLLVMType(dtype), "");
	}

	return val;
}

LLVMValueRef convertAndOrInstr(PLogicalExprData ledata, PSmmAstNode node, LLVMBasicBlockRef trueBlock, LLVMBasicBlockRef falseBlock) {
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
	case nkSmmAndOp: case nkSmmOrOp: left = convertAndOrInstr(ledata, node->left, nextTrue, nextFalse); break;
	default: left = convertToInstructions(ledata->data, node->left); break;
	}

	LLVMBuildCondBr(ledata->data->builder, left, nextTrue, nextFalse);
	if (ledata->data->endBlock == nextTrue || ledata->data->endBlock == nextFalse) {
		if (ledata->blockCount >= MAX_LOGICAL_EXPR_DEPTH) {
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
		return convertAndOrInstr(ledata, node->right, trueBlock, falseBlock);
	default: return convertToInstructions(ledata->data, node->right);
	}
}

LLVMValueRef convertToInstructions(PSmmLLVMModuleGenData data, PSmmAstNode node) {
	assert(LLVMFRem - LLVMAdd == nkSmmFRem - nkSmmAdd);
	LLVMValueRef res = NULL;
	LLVMBuilderRef builder = data->builder;

	// If it is arithmetic operator
	if (node->kind >= nkSmmAdd && node->kind <= nkSmmFRem) {
		LLVMValueRef left = convertToInstructions(data, node->left);
		LLVMValueRef right = convertToInstructions(data, node->right);
		return LLVMBuildBinOp(builder, node->kind - nkSmmAdd + LLVMAdd, left, right, "");
	}

	switch (node->kind) {
	case nkSmmAssignment: 
		{
			LLVMValueRef left = smmGetDictValue(data->localVars, node->left->token->repr, false);
			res = LLVMBuildStore(builder, convertToInstructions(data, node->right), left);
			LLVMSetAlignment(res, node->left->type->sizeInBytes);
			res = left;
			break;
		}
	case nkSmmNeg: res = LLVMBuildNeg(builder, convertToInstructions(data, node->left), ""); break;
	case nkSmmInt: {
		bool signExtend = !(node->type->flags & tifSmmUnsigned);
		LLVMTypeRef intType = LLVMIntType(node->type->sizeInBytes << 3);
		res = LLVMConstInt(intType, node->token->uintVal, signExtend);
		break;
	}
	case nkSmmFloat:
		if (node->type->kind == tiSmmFloat32) {
			res = LLVMConstReal(LLVMFloatType(), node->token->floatVal);
		} else {
			res = LLVMConstReal(LLVMDoubleType(), node->token->floatVal);
		}
		break;
	case nkSmmBool:
		res = LLVMConstInt(LLVMInt1Type(), node->token->boolVal, false);
		break;
	case nkSmmIdent: case nkSmmParam:
		res = smmGetDictValue(data->localVars, node->token->repr, false);
		res = LLVMBuildLoad(builder, res, "");
		LLVMSetAlignment(res, node->type->sizeInBytes);
		break;
	case nkSmmConst:
		res = smmGetDictValue(data->localVars, node->token->repr, false);
		break;
	case nkSmmCast:
		res = getCastInstruction(data, node->type, node->left->type, convertToInstructions(data, node->left));
		break;
	case nkSmmReturn:
		if (node->left) res = LLVMBuildRet(data->builder, convertToInstructions(data, node->left));
		else res = LLVMBuildRetVoid(data->builder);
		break;
	case nkSmmCall:
		{
			LLVMValueRef func = smmGetDictValue(data->localVars, node->token->repr, false);
			LLVMValueRef* args = NULL;
			size_t argCount = 0;
			PSmmAstCallNode callNode = (PSmmAstCallNode)node;
			if (callNode->params) {
				argCount = callNode->params->count;
				PSmmAstNode astArg = callNode->args;
				args = data->allocator->alloca(data->allocator, argCount * sizeof(LLVMValueRef));
				for (size_t i = 0; i < argCount; i++) {
					args[i] = convertToInstructions(data, astArg);
					astArg = astArg->next;
				}
			}
			res = LLVMBuildCall(data->builder, func, args, (unsigned)argCount, "");
			if (args) data->allocator->freea(data->allocator, args);
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

			res = convertAndOrInstr(&logicalExprData, node, data->endBlock, data->endBlock);
			if (logicalExprData.blockCount >= MAX_LOGICAL_EXPR_DEPTH) {
				char msg[500] = { 0 };
				snprintf(msg, 500, "Logical expression at %s:%d too complicated", node->token->filePos.filename, node->token->filePos.lineNumber);
				smmAbortWithMessage(msg, __FILE__, __LINE__);
			}
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
	case nkSmmXorOp: case nkSmmEq: case nkSmmNotEq:case nkSmmGt:case nkSmmGtEq:case nkSmmLt:case nkSmmLtEq:
		{
			// TODO(igors): test with unsigned numbers
			LLVMValueRef left = convertToInstructions(data, node->left);
			LLVMValueRef right = convertToInstructions(data, node->right);
			if ((node->left->type->flags & tifSmmInt) || node->left->type->kind == tiSmmBool) {
				LLVMIntPredicate op;
				if (node->kind == nkSmmXorOp) op = LLVMIntNE;
				else {
					op = node->kind - nkSmmEq + LLVMIntEQ;
					if (op >= LLVMIntUGT && !(node->left->type->flags & tifSmmUnsigned)) {
						op = op - LLVMIntUGT + LLVMIntSGT;
					}
				}
				res = LLVMBuildICmp(data->builder, op, left, right, "");
			} else if (node->left->type->flags & tifSmmFloat) {
				LLVMRealPredicate op = LLVMRealUNE; // For '!=', it can't be xor here because we add != 0 on float operands for xor
				switch (node->kind) {
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
	case nkSmmNot:
		res = LLVMBuildNot(data->builder, convertToInstructions(data, node->left), "");
		break;
	default:
		assert(false && "Encountered unknown node type!");
		break;
	}
	return res;
}

void createLocalVars(PSmmLLVMModuleGenData data, PSmmAstScopeNode scope) {
	PSmmAstNode decl = scope->decls;
	while (decl) {
		LLVMTypeRef type = getLLVMType(decl->left->type);

		LLVMValueRef var = NULL;
		if (decl->left->kind == nkSmmIdent) {
			var = LLVMBuildAlloca(data->builder, type, decl->left->token->repr);
			LLVMSetAlignment(var, decl->left->type->sizeInBytes);
		} else if (decl->left->kind == nkSmmConst) {
			var = convertToInstructions(data, decl->right);
		} else {
			assert(false && "Declaration of unknown node kind");
		}
		PSmmToken varToken = decl->left->token;
		smmAddDictValue(data->localVars, varToken->repr, var);
		decl = decl->next;
	}
}

void convertBlock(PSmmLLVMModuleGenData data, PSmmAstBlockNode block) {
	createLocalVars(data, block->scope);
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		convertToInstructions(data, stmt);
		stmt = stmt->next;
	}
}

void createFunc(PSmmLLVMModuleGenData data, PSmmAstFuncDefNode astFunc) {
	LLVMTypeRef returnType = getLLVMType(astFunc->returnType);
	LLVMTypeRef* params = NULL;
	size_t paramsCount = 0;
	if (astFunc->params) {
		PSmmAstParamNode param = astFunc->params;
		paramsCount = param->count;
		params = data->allocator->alloca(data->allocator, paramsCount * sizeof(LLVMTypeRef));
		for (size_t i = 0; i < paramsCount; i++) {
			params[i] = getLLVMType(param->type);
			param = param->next;
		}
	}
	LLVMTypeRef funcType = LLVMFunctionType(returnType, params, (unsigned)paramsCount, false);
	if (params) {
		data->allocator->freea(data->allocator, params);
		params = NULL;
	}
	LLVMValueRef func = LLVMAddFunction(data->llvmModule, astFunc->token->repr, funcType);
	LLVMValueRef oldFunc = data->curFunc;

	smmPushDictValue(data->localVars, astFunc->token->repr, func);
	
	if (astFunc->body) {
		data->curFunc = func;
		LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");

		LLVMPositionBuilderAtEnd(data->builder, entry);

		if (paramsCount > 0) {
			LLVMValueRef* paramVals = data->allocator->alloca(data->allocator, paramsCount * sizeof(LLVMValueRef));
			LLVMGetParams(func, paramVals);
			PSmmAstParamNode param = astFunc->params;
			for (size_t i = 0; i < paramsCount; i++) {
				LLVMSetValueName(paramVals[i], param->token->repr);
				LLVMValueRef paramStore = LLVMBuildAlloca(data->builder, LLVMTypeOf(paramVals[i]), param->token->repr);
				LLVMBuildStore(data->builder, paramVals[i], paramStore);
				smmPushDictValue(data->localVars, param->token->repr, paramStore);
				param = param->next;
			}
			data->allocator->freea(data->allocator, paramVals);
		}

		convertBlock(data, astFunc->body);

		PSmmAstParamNode param = astFunc->params;
		while (param) {
			smmPopDictValue(data->localVars, param->token->repr);
			param = param->next;
		}

		data->curFunc = oldFunc;

		LLVMVerifyFunction(func, LLVMPrintMessageAction);
	}
}

void createGlobalVars(PSmmLLVMModuleGenData data, PSmmAstScopeNode scope) {
	PSmmAstNode decl = scope->decls;
	while (decl) {
		LLVMTypeRef type = getLLVMType(decl->left->type);

		if (decl->left->kind == nkSmmFunc) {
			LLVMBasicBlockRef curBlock = LLVMGetInsertBlock(data->builder);
			createFunc(data, (PSmmAstFuncDefNode)decl->left);
			LLVMPositionBuilderAtEnd(data->builder, curBlock);
		} else {
			assert(decl->right && "Global var must have initializer");
			LLVMValueRef val = convertToInstructions(data, decl->right);
			
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

void smmGenLLVMModule(PSmmAstNode module, PSmmAllocator a) {
	if (!module) return;

	PSmmLLVMModuleGenData data = a->alloc(a, sizeof(struct SmmLLVMModuleGenData));
	data->allocator = a;
	data->module = module;
	data->localVars = smmCreateDict(a, NULL, NULL);
	data->localVars->storeKeyCopy = false;

	if (data->module->kind == nkSmmProgram) data->module = data->module->next;

	data->llvmModule = LLVMModuleCreateWithName(module->token->repr);
	LLVMSetDataLayout(data->llvmModule, "");
	LLVMSetTarget(data->llvmModule, LLVMGetDefaultTargetTriple());
	
	LLVMTypeRef funcType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
	LLVMValueRef mainfunc = LLVMAddFunction(data->llvmModule, "main", funcType);
	data->curFunc = mainfunc;

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainfunc, "entry");

	data->builder = LLVMCreateBuilder();
	LLVMPositionBuilderAtEnd(data->builder, entry);

	PSmmAstNode curStmt = data->module;
	if (curStmt && curStmt->kind == nkSmmBlock) {
		PSmmAstBlockNode astBlock = (PSmmAstBlockNode)curStmt;
		createGlobalVars(data, astBlock->scope);
		curStmt = astBlock->stmts;
	}
	while (curStmt) {
		if (curStmt->kind == nkSmmBlock) {
			convertBlock(data, (PSmmAstBlockNode)curStmt);
		} else if (curStmt->kind != nkSmmError) {
			convertToInstructions(data, curStmt);
		}
		curStmt = curStmt->next;
	}
	
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
