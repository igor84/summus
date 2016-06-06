#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmsemtypes.h"

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
		Think about supporting literals of all primitive types using suffixes and such
		(when doing int / int how to specify if you want float32 or float64 result or how to smartly determine that)
		Do complete code review and add all the comments
		Add logical operators
		Add bitwise operators
		GlobalSettings
	*/
	char buf[64 * 1024] = { 0 };
	char* filename = "test.smm";
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
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}