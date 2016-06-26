#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmsemtypes.h"
#include "smmlllvmmodulegen.h"

#include <stdio.h>
#include <stdlib.h>

void printNode(PSmmAstNode node) {
	//while (node->kind == nkSmmDecl) node = node->next;

	if (node->kind == nkSmmIdent || node->kind == nkSmmConst) {
		fputs(node->token->repr, stdout);
	} else {
		fputs(nodeKindToString[node->kind], stdout);
	}
	if (node->kind != nkSmmScope && node->type && node->type->kind != 0 /*&& (
		node->kind == nkSmmIdent || node->kind == nkSmmConst ||
		node->kind == nkSmmInt || node->kind == nkSmmFloat || node->kind == nkSmmCast)*/) {
		fputs(":", stdout);
		fputs(node->type->name, stdout);
	}
	if (node->kind != nkSmmNeg) {
		fputs(" ", stdout);
	}
	
	if (node->left) printNode(node->left);

	if (node->right) {
		if (node->kind == nkSmmDecl) fputs("hasRight", stdout);
		else printNode(node->right);
	}
	if (node->next) {
		puts("");
		printNode(node->next);
	}
}

int main(int argc, char **argv) {
	/*
	TODO:
		Add function defs
		Do complete code review and add all the comments
		Add logical operators
		Add bitwise operators
		LLVM has validateFunction as well
		GlobalSettings
	*/

	const char* filename = "console";
	char* buf = NULL;
	char filebuf[64 * 1024] = { 0 };
	if (argc > 1) {
		filename = argv[1];
		FILE* f = fopen(filename, "rb");
		if (!f) {
			printf("Can't find %s in the current folder!\n", filename);
			return EXIT_FAILURE;
		}
		fread(filebuf, 1, 64 * 1024, f);
		fclose(f);
		buf = filebuf;
	}
	PSmmAllocator allocator = smmCreatePermanentAllocator(filename, 64 * 1024 * 1024);
	PSmmLexer lex = smmCreateLexer(buf, filename, allocator);
	
	PSmmParser parser = smmCreateParser(lex, allocator);

	PSmmAstNode program = smmParse(parser);

	puts("\n");
	printNode(program);
	puts("\n");

	struct SmmModuleData data = {program, filename, allocator};
	smmAnalyzeTypes(&data);

	puts("\n");
	printNode(program);
	puts("\n");

	if (smmHadErrors()) {
		return 1;
	}
	smmGenLLVMModule(&data, allocator);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}
