#include <stdio.h>

#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"

int main(void) {
	/*
	TODO:
		Add typing to parser and lexer
		Add logical operators
		Add bitwise operators
		GlobalSettings
	*/
	char buf[64 * 1024] = { 0 };
	FILE* f = fopen("test.smm", "rb");
	if (!f) {
		printf("Can't find test.smm in the current folder!\n");
		return EXIT_FAILURE;
	}
	fread(buf, 1, 64 * 1024, f);
	fclose(f);
	PSmmAllocator allocator = smmCreatePermanentAllocator("test.smm", 64 * 1024 * 1024);
	PSmmLexer lex = smmCreateLexer(buf, "test.smm", allocator);
	
	PSmmParser parser = smmCreateParser(lex, allocator);

	smmParse(parser);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}