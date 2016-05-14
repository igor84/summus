#include <stdio.h>

#include "smmutil.h"
#include "smmlexer.h"
#include "smmparser.h"

int main(void) {
	//TODO literal types
	/*
	
	TODO:
		GlobalSettings
		Should I report "probably missing operator" instead of expected ';' if it is in the same line and continue to parse expression
	
	*/
	//char buf[] = "1231231239999999999999999999999999.456456e10";
	char buf[64 * 1024] = { 0 };
	FILE* f = fopen("expressions.smm", "rb");
	fread(buf, 1, 64 * 1024, f);
	fclose(f);
	PSmmAllocator allocator = smmCreatePermanentAllocator("test.smm", 64 * 1024 * 1024);
	PSmmLexer lex = smmCreateLexer(buf, "test.smm", allocator);
	
	PSmmParser parser = smmCreateParser(lex, allocator);

	smmParse(parser);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}