#include "smmlllvmmodulegen.h"
#include "llvm-c/Core.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/TargetMachine.h"

#include <assert.h>
#include <stdio.h>

struct SmmLLVMModuleGenData {
	PSmmAstNode module;
	const char* filename;
	PSmmAllocator allocator;
	PSmmDict localVars;
	LLVMBuilderRef builder;
};
typedef struct SmmLLVMModuleGenData* PSmmLLVMModuleGenData;

LLVMTypeRef getLLVMFloatType(PSmmTypeInfo type) {
	if (type->kind == tiSmmFloat32) return LLVMFloatType();
	return LLVMDoubleType();
}

LLVMTypeRef getLLVMType(PSmmTypeInfo type) {
	switch (type->kind) {
	case tiSmmInt8: case tiSmmInt16: case tiSmmInt32: case tiSmmInt64:
	case tiSmmBool: case tiSmmUInt8: case tiSmmUInt16: case tiSmmUInt32: case tiSmmUInt64:
		return LLVMIntType(type->sizeInBytes << 3);
	case tiSmmFloat32: return LLVMFloatType();
	case tiSmmFloat64: return LLVMDoubleType();
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
	
	if ((dtype->flags & stype->flags & tifSmmInt) == tifSmmInt && dtype->sizeInBytes != stype->sizeInBytes) {
		return LLVMBuildIntCast(data->builder, val, getLLVMType(dtype), "");
	}

	if ((dtype->flags & stype->flags & tifSmmFloat) == tifSmmFloat && dtype->sizeInBytes != stype->sizeInBytes) {
		return LLVMBuildFPCast(data->builder, val, getLLVMType(dtype), "");
	}

	return val;
}

LLVMValueRef convertToInstructions(PSmmLLVMModuleGenData data, PSmmAstNode node) {
	assert(LLVMFRem - LLVMAdd == nkSmmFRem - nkSmmAdd);
	LLVMValueRef res = NULL;
	LLVMValueRef left = NULL;
	LLVMValueRef right = NULL;
	LLVMBuilderRef builder = data->builder;
	if (node->left) {
		if (node->kind == nkSmmAssignment) {
			PSmmToken varToken = node->left->token;
			left = smmGetDictValue(data->localVars, varToken->repr, varToken->hash, false);
		} else {
			left = convertToInstructions(data, node->left);
		}
	}
	if (node->right) {
		right = convertToInstructions(data, node->right);
	}
	if (node->kind >= nkSmmAdd && node->kind <= nkSmmFRem) {
		return LLVMBuildBinOp(builder, node->kind - nkSmmAdd + LLVMAdd, left, right, "");
	}

	bool signExtend;
	LLVMTypeRef intType;

	switch (node->kind) {
	case nkSmmAssignment:
		res = LLVMBuildStore(builder, right, left);
		LLVMSetAlignment(res, node->left->type->sizeInBytes);
		res = left;
		break;
	case nkSmmNeg: res = LLVMBuildNeg(builder, left, ""); break;
	case nkSmmInt:
		signExtend = !(node->type->flags & tifSmmUnsigned);
		intType = LLVMIntType(node->type->sizeInBytes << 3);
		res = LLVMConstInt(intType, node->token->uintVal, signExtend);
		break;
	case nkSmmFloat:
		if (node->type->kind == tiSmmFloat32) {
			res = LLVMConstReal(LLVMFloatType(), node->token->floatVal);
		} else {
			res = LLVMConstReal(LLVMDoubleType(), node->token->floatVal);
		}
		break;
	case nkSmmIdent:
		res = smmGetDictValue(data->localVars, node->token->repr, node->token->hash, false);
		res = LLVMBuildLoad(builder, res, "");
		LLVMSetAlignment(res, node->type->sizeInBytes);
		break;
	case nkSmmCast:
		res = getCastInstruction(data, node->type, node->left->type, left);
		break;
	default:
		assert(false && "Encountered unknown node type!");
	}
	return res;
}

PSmmAstNode createLocalVars(PSmmLLVMModuleGenData data) {
	PSmmAstNode node = data->module;
	while (node && node->kind == nkSmmDecl) {
		LLVMTypeRef type = NULL;
		LLVMValueRef zero = NULL;
		bool emptyDecl = !node->right;
		if (emptyDecl) {
			zero = getZeroForType(node->left->type);
			type = LLVMTypeOf(zero);
		} else {
			type = getLLVMType(node->left->type);
		}

		LLVMValueRef var = LLVMBuildAlloca(data->builder, type, node->left->token->repr);
		LLVMSetAlignment(var, node->left->type->sizeInBytes);
		PSmmToken varToken = node->left->token;
		smmAddDictValue(data->localVars, varToken->repr, varToken->hash, var);
		if (emptyDecl) {
			LLVMBuildStore(data->builder, var, zero);
		}
		node = node->next;
	}
	return node;
}

void smmGenLLVMModule(PSmmModuleData mdata, PSmmAllocator a) {
	if (!mdata || !mdata->module) return;

	PSmmLLVMModuleGenData data = (PSmmLLVMModuleGenData)a->alloc(a, sizeof(struct SmmLLVMModuleGenData));
	data->allocator = a;
	data->filename = mdata->filename;
	data->module = mdata->module;
	data->localVars = smmCreateDict(a, 512, NULL, NULL);
	data->localVars->storeKeyCopy = false;

	if (data->module->kind == nkSmmProgram) data->module = data->module->next;

	LLVMModuleRef mod = LLVMModuleCreateWithName(data->filename);
	LLVMSetDataLayout(mod, "");
	LLVMSetTarget(mod, LLVMGetDefaultTargetTriple());
	
	LLVMTypeRef funcType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
	LLVMValueRef mainfunc = LLVMAddFunction(mod, "main", funcType);

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainfunc, "entry");

	data->builder = LLVMCreateBuilder();
	LLVMPositionBuilderAtEnd(data->builder, entry);

	LLVMValueRef lastVal = NULL;
	PSmmAstNode curStmt = createLocalVars(data);
	while (curStmt) {
		if (curStmt->kind != nkSmmError) {
			lastVal = convertToInstructions(data, curStmt);
		}
		curStmt = curStmt->next;
	}
	if (!lastVal) {
		lastVal = LLVMConstInt(LLVMInt32Type(), 0, true);
	} else {
		lastVal = LLVMBuildLoad(data->builder, lastVal, "");
	}
	LLVMBuildRet(data->builder, lastVal);

	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	char* err = NULL;
	LLVMPrintModuleToFile(mod, "test.ll", &err);
	if (err) {
		printf("\nError Saving Module: %s\n", err);
	} else {
		printf("\nModule saved to test.ll");
	}
}
