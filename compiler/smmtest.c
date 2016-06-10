#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmsemtypes.h"
#include "smmlllvmmodulegen.h"

#include <stdio.h>
#include <stdlib.h>

void printNode(PSmmAstNode node) {
	fputs(nodeKindToString[node->kind], stdout);
	if (node->type && node->type->kind != 0) {
		fputs(":", stdout);
		fputs(node->type->name, stdout);
	}
	if (node->kind != nkSmmNeg) {
		fputs(" ", stdout);
	}
	if (node->left) printNode(node->left);
	if (node->right) printNode(node->right);
	if (node->next) {
		puts("");
		printNode(node->next);
	}
}

int main(void) {
	/*
	TODO:
		Add casts and handle cast nodes in semtypes
		Do complete code review and add all the comments
		Add logical operators
		Add bitwise operators
		GlobalSettings
	*/
	char buf[64 * 1024] = { 0 };
	const char* filename = "test.smm";
	FILE* f = fopen(filename, "rb");
	if (!f) {
		printf("Can't find test.smm in the current folder!\n");
		return EXIT_FAILURE;
	}
	fread(buf, 1, 64 * 1024, f);
	fclose(f);
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

	smmGenLLVMModule(&data, allocator);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}
