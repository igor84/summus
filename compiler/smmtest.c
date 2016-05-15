#include <stdio.h>

#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"

int main(void) {
	/*
	TODO:
		GlobalSettings
		literal types
	*/
	char buf[64 * 1024] = { 0 };
	FILE* f = fopen("test.smm", "rb");
	fread(buf, 1, 64 * 1024, f);
	fclose(f);
	PSmmAllocator allocator = smmCreatePermanentAllocator("test.smm", 64 * 1024 * 1024);
	PSmmLexer lex = smmCreateLexer(buf, "test.smm", allocator);
	
	PSmmParser parser = smmCreateParser(lex, allocator);

	smmParse(parser);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}