#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"
#include "smmsemtypes.h"
#include "smmlllvmmodulegen.h"

#include <stdio.h>
#include <stdlib.h>

void printNode(PSmmAstNode node, int align) {
	if (align) printf("%*s", align, " ");

	if (node->token && node->token->repr && node->kind != nkSmmCast) {
		fputs(node->token->repr, stdout);
	} else {
		fputs(nodeKindToString[node->kind], stdout);
	}
	if (node->kind != nkSmmScope && node->type && node->type->kind != 0) {
		fputs(":", stdout);
		fputs(node->type->name, stdout);
	}
	fputs(" ", stdout);
	
	if (node->kind == nkSmmFunc || node->kind == nkSmmCall) {
		PSmmAstFuncDefNode func = (PSmmAstFuncDefNode)node;
		bool isCall = node->kind == nkSmmCall;
		PSmmAstNode nextStmt = NULL;
		if (isCall) {
			nextStmt = node->next;
			node = node->right;
		} else {
			node = node->left;
		}
		if (!node) {
			fputs("()", stdout);
			if (!isCall && func->body) printNode((PSmmAstNode)func->body, align + 4);
			return;
		}
		printf("(%s:%s", node->token->repr, node->type->name);
		node = node->next;
		while (node) {
			printf(", %s:%s", node->token->repr, node->type->name);
			node = node->next;
		}
		if (isCall) fputs(")", stdout);
		else puts(")");
		if (!isCall && func->body) printNode((PSmmAstNode)func->body, align + 4);
		if (nextStmt) {
			puts("");
			printNode(nextStmt, align);
		}
		return;
	}
	
	if (node->kind != nkSmmParam && node->kind != nkSmmScope) {
		if (node->left) printNode(node->left, 0);

		if (node->right) {
			if (node->kind == nkSmmDecl) fputs("hasRight", stdout);
			else if (node->kind == nkSmmBlock) {
				puts("");
				printNode(node->right, align + 4);
			} else printNode(node->right, 0);
		}
	}
	if (node->kind != nkSmmParam && node->next) {
		puts("");
		printNode(node->next, align);
	}
}

int main(int argc, char **argv) {
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
	PSmmAllocator allocator = smmCreatePermanentAllocator(filename, 64 * 1024);
	PSmmLexer lex = smmCreateLexer(buf, filename, allocator);
	
	PSmmParser parser = smmCreateParser(lex, allocator);

	PSmmAstNode program = smmParse(parser);

	puts("\n");
	printNode(program, 0);
	puts("\n");

	smmAnalyzeTypes(program, allocator);

	puts("\n");
	printNode(program, 0);
	puts("\n");

	bool hadErrors = smmHadErrors();
	if (!hadErrors) {
		smmGenLLVMModule(program, allocator);
	}
	
	smmPrintAllocatorInfo(allocator);
	
	return hadErrors;
}
