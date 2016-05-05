#pragma once

#include "smmlexer.h"
#include <llvm/Pass.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/TypeBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/MathExtras.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace llvm;

#define SMM_VAR_TABLE_SIZE 8096

typedef struct {
	char* name;
	AllocaInst* var;
} SmmStorage;
typedef SmmStorage* PSmmStorage;

typedef struct SmmVarTableEntry *PSmmVarTableEntry;
struct SmmVarTableEntry {
	SmmStorage storage;
	PSmmVarTableEntry next;
};

typedef struct {
	PSmmLexer lex;
	PSmmToken curToken;
	PSmmVarTableEntry varTable[SMM_VAR_TABLE_SIZE];
} SmmParser;
typedef SmmParser* PSmmParser;

static PSmmStorage smmGetStorage(PSmmParser parser, PSmmToken token) {
	if (!token) token = parser->curToken;
	uint32_t hash = token->intVal & (SMM_VAR_TABLE_SIZE - 1);
	char * varName = token->repr;
	PSmmVarTableEntry result = parser->varTable[hash];
	PSmmVarTableEntry last = NULL;

	while (result) {
		if (strcmp(varName, result->storage.name) == 0) {
			// Put the found element on start of the list so next search is faster
			if (last) {
				last->next = result->next;
				result->next = parser->varTable[hash];
				parser->varTable[hash] = result;
			}
			return &result->storage;
		}
		last = result;
		result = result->next;
	}

	result = (PSmmVarTableEntry)calloc(1, sizeof(SmmVarTableEntry));
	result->storage.name = varName;
	result->next = parser->varTable[hash];
	parser->varTable[hash] = result;

	return &result->storage;
}

Function* smmMakeLLVMEntryFunc(Module *mod) {
	FunctionType* entryFuncType = TypeBuilder<types::i<32>(), true>::get(mod->getContext());

	Function* entryFunc = mod->getFunction("main");
	if (!entryFunc) {
		entryFunc = Function::Create(entryFuncType, GlobalValue::ExternalLinkage, "main", mod);
	}
	return entryFunc;
}

void smmReportParseError(char* msg, PSmmToken token) {
	printf("Error: %s %d (at %d:%d)\n", msg, token->tokenType, token->filePos.lineNumber, token->filePos.lineOffset);
}

void smmSkipToEndStatementToken(PSmmParser parser) {
	while (parser->curToken->tokenType != ';' && parser->curToken->tokenType != smmEof) {
		parser->curToken = smmGetNextToken(parser->lex);
	}
}

PSmmToken smmExpect(PSmmParser parser, SmmTokenType type) {
	PSmmToken token = parser->curToken;
	if (token->tokenType != type) {
		if (parser->curToken->tokenType != smmErr) {
			// If it is smmErr lexer already reported the error
			smmReportParseError("Expected identifier but got ", parser->curToken);
		}
		smmSkipToEndStatementToken(parser);
		return NULL;
	}
	parser->curToken = smmGetNextToken(parser->lex);
	return token;
}

Value* parseExpression(PSmmParser parser, BasicBlock* bb);

Value* parseFactor(PSmmParser parser, BasicBlock* bb) {
	Value* res = NULL;
	if (parser->curToken->tokenType == '(') {
		parser->curToken = smmGetNextToken(parser->lex);
		res = parseExpression(parser, bb);
		smmExpect(parser, (SmmTokenType)')'); //TODO(igors): what about error
	} else if (parser->curToken->tokenType == smmIdent) {
		PSmmStorage storage = smmGetStorage(parser, NULL);
		if (storage->var == NULL) {
			smmReportParseError("Use of undefined variable", parser->curToken);
			return NULL;
		}
		res = new LoadInst(storage->var, "", bb);
		parser->curToken = smmGetNextToken(parser->lex);
	} else if (parser->curToken->tokenType == smmInteger) {
		res = ConstantInt::get(bb->getContext(), APInt(32, parser->curToken->intVal));
		parser->curToken = smmGetNextToken(parser->lex);
	} else {
		smmReportParseError("Expected identifier or number but got", parser->curToken);
		return NULL;
	}
	return res;
}

Value* parseTerm(PSmmParser parser, BasicBlock* bb) {
	Value* term1 = parseFactor(parser, bb);
	if (!term1) return NULL;
	while (parser->curToken->tokenType == '*' || parser->curToken->tokenType  == '/' || parser->curToken->tokenType == smmIntDiv) {
		Instruction::BinaryOps op = Instruction::Mul;
		if (parser->curToken->tokenType == '/') {
			op = Instruction::FDiv;
			if (term1->getType()->isIntegerTy()) {
				//TODO(igors): Make it configurable if double or float are default
				term1 = new SIToFPInst(term1, Type::getDoubleTy(bb->getContext()), "", bb);
			}
		} else if (parser->curToken->tokenType == smmIntDiv) {
			op = Instruction::SDiv;
		}
		parser->curToken = smmGetNextToken(parser->lex);
		Value* term2 = parseFactor(parser, bb);
		if (!term2) return NULL;
		if (!term1->getType()->isIntegerTy() && term2->getType()->isIntegerTy()) {
			term2 = new SIToFPInst(term2, Type::getDoubleTy(bb->getContext()), "", bb);
		} else if (term1->getType()->isIntegerTy() && !term2->getType()->isIntegerTy()) {
			term1 = new SIToFPInst(term1, Type::getDoubleTy(bb->getContext()), "", bb);
		}
		if (!term1->getType()->isIntegerTy() && op == Instruction::Mul) {
			op = Instruction::FMul;
		}
		term1 = BinaryOperator::Create(op, term1, term2, "", bb);
	};
	return term1;
}

Value* parseExpression(PSmmParser parser, BasicBlock* bb) {
	Value* term1 = NULL;
	if (parser->curToken->tokenType == '-') {
		term1 = ConstantInt::get(bb->getContext(), APInt(32, 0));
	} else {
		if (parser->curToken->tokenType == '+') {
			parser->curToken = smmGetNextToken(parser->lex);
		}
		term1 = parseTerm(parser, bb);
		if (!term1) return NULL;
	}
	while (parser->curToken->tokenType == '-' || parser->curToken->tokenType == '+') {
		Instruction::BinaryOps op = Instruction::Add;
		if (parser->curToken->tokenType == '-') {
			op = Instruction::Sub;
		}
		parser->curToken = smmGetNextToken(parser->lex);
		Value* term2 = parseTerm(parser, bb);
		if (!term2) return NULL;
		if (!term1->getType()->isIntegerTy() && term2->getType()->isIntegerTy()) {
			term2 = new SIToFPInst(term2, Type::getDoubleTy(bb->getContext()), "", bb);
		} else if (term1->getType()->isIntegerTy() && !term2->getType()->isIntegerTy()) {
			term1 = new SIToFPInst(term1, Type::getDoubleTy(bb->getContext()), "", bb);
		}
		if (!term1->getType()->isIntegerTy()) {
			if (op == Instruction::Add) op = Instruction::FAdd;
			else op = Instruction::FSub;
		}
		term1 = BinaryOperator::Create(op, term1, term2, "", bb);
	};
	return term1;
}

Value* smmParseStatement(PSmmParser parser, BasicBlock* bb) {
	PSmmToken token = smmExpect(parser, smmIdent);
	if (!token) return NULL;
	smmExpect(parser, (SmmTokenType)'=');
	Value *val = parseExpression(parser, bb);
	if (!val) return NULL; //TODO(igors): Probably need to skip to end of stmt on these returns
	PSmmStorage storage = smmGetStorage(parser, token);
	if (!storage->var) {
		storage->var = new AllocaInst(val->getType(), "", bb);
	}
	new StoreInst(val, storage->var, bb);
	return storage->var;
}

void smmParse(PSmmLexer lex) {
	SmmParser parser = {lex, smmGetNextToken(lex)};
	Module* mod = new Module("main.ll", getGlobalContext());
	Function* func = smmMakeLLVMEntryFunc(mod);

	BasicBlock* bb = BasicBlock::Create(mod->getContext(), "", func, 0);
	Value* lastVal = NULL;

	while (parser.curToken->tokenType != smmEof) {
		lastVal = smmParseStatement(&parser, bb);
		PSmmToken t = smmExpect(&parser, (SmmTokenType)';');
		if (!t) {
			smmReportParseError("Expected ; but got ", parser.curToken);
			break;
		}
	}

	if (lastVal) {
		lastVal = new LoadInst(lastVal, "", bb);
		if (!lastVal->getType()->isIntegerTy()) {
			lastVal = new FPToSIInst(lastVal, Type::getInt32Ty(mod->getContext()), "", bb);
		}
		ReturnInst::Create(bb->getContext(), lastVal, bb);
	}

	verifyModule(*mod, &errs());
	PassManager<Module> PM;
	PrintModulePass modPass(outs());
	PM.addPass(modPass);
	PM.run(*mod);
}