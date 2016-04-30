
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

typedef struct {
	char curChar;
} TLexer;

void nextChar(TLexer *lexer) {
	lexer->curChar = getchar();
}

void printErr(char *errorMsg) {
	printf("\nERROR: %s.\n", errorMsg);
}

void abort(char *errorMsg) {
	printErr(errorMsg);
	exit(EXIT_FAILURE);
}

void expected(char *expected) {
	printf("\nERROR: %s expected.\n", expected);
	exit(EXIT_FAILURE);
}

void match(TLexer *lexer, char c) {
	if (lexer->curChar == c) nextChar(lexer);
	else {
		char quoted[4] = { '\'', c, '\'', 0 };
		expected(&quoted[0]);
	}
}

bool isAlpha(TLexer *lexer) {
	char c = lexer->curChar;
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(TLexer *lexer) {
	char c = lexer->curChar;
	return (c >= '0' && c <= '9');
}

char getName(TLexer *lexer) {
	if (!isAlpha(lexer)) expected("Name");
	char c = lexer->curChar;
	nextChar(lexer);
	return c;
}

char getNum(TLexer *lexer) {
	if (!isDigit(lexer)) expected("Integer");
	char c = lexer->curChar;
	nextChar(lexer);
	return c;
}

Value* parseExpression(TLexer* lexer, BasicBlock* bb);

Value* parseFactor(TLexer* lexer, BasicBlock* bb) {
	Value* res = NULL;
	if (lexer->curChar == '(') {
		nextChar(lexer);
		res = parseExpression(lexer, bb);
		match(lexer, ')');
	} else {
		char num = getNum(lexer);
		res = ConstantInt::get(bb->getContext(), APInt(32, StringRef(&num, 1), 10));
	}
	return res;
}

Value* parseTerm(TLexer* lexer, BasicBlock* bb) {
	Value* term1 = parseFactor(lexer, bb);
	while (lexer->curChar == '*' || lexer->curChar == '/') {
		Instruction::BinaryOps op = Instruction::Mul;
		if (lexer->curChar == '/') {
			op = Instruction::UDiv;
		}
		nextChar(lexer);
		Value* term2 = parseFactor(lexer, bb);
		term1 = BinaryOperator::Create(op, term1, term2, "", bb);
	};
	return term1;
}

Value* parseExpression(TLexer* lexer, BasicBlock* bb) {
	Value* term1 = NULL;
	if (lexer->curChar == '-') {
		term1 = ConstantInt::get(bb->getContext(), APInt(32, 0));
	}
	else {
		if (lexer->curChar == '+') {
			nextChar(lexer);
		}
		term1 = parseTerm(lexer, bb);
	}
	while (lexer->curChar == '-' || lexer->curChar == '+') {
		Instruction::BinaryOps op = Instruction::Add;
		if (lexer->curChar == '-') {
			op = Instruction::Sub;
		}
		nextChar(lexer);
		Value* term2 = parseTerm(lexer, bb);
		term1 = BinaryOperator::Create(op, term1, term2, "", bb);
	};
	return term1;
}

Module* makeLLVMModule() {
	// Module Construction
	Module* mod = new Module("main.ll", getGlobalContext());
	mod->setDataLayout("");
	//mod->setTargetTriple("x86_64-pc-windows-msvc18.0.0");

	return mod;
}

Function* makeLLVMEntryFunc(Module *mod) {
	FunctionType* entryFuncType = TypeBuilder<types::i<32>(), true>::get(mod->getContext());

	Function* entryFunc = mod->getFunction("main");
	if (!entryFunc) {
		entryFunc = Function::Create(entryFuncType, GlobalValue::ExternalLinkage, "main", mod);
	}
	return entryFunc;
}



int main() {
	TLexer lexer = {};
	TLexer *plex = &lexer;
	nextChar(plex);

	Module* mod = new Module("main.ll", getGlobalContext());
	Function* func = makeLLVMEntryFunc(mod);

	BasicBlock* bb = BasicBlock::Create(mod->getContext(), "", func, 0);

	Value* val = parseExpression(plex, bb);
	if (lexer.curChar != '\n') expected("New Line");

	ReturnInst::Create(bb->getContext(), val, bb);
	
	verifyModule(*mod, &errs());
	PassManager<Module> PM;
	PrintModulePass modPass;
	PM.addPass(modPass);
	PM.run(*mod);

	return 0;
}