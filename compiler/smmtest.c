#include <stdio.h>

#include "smmutil.h"
#include "smmlexer.h"

int main(void) {
	//TODO literal types
	//char buf[] = "1231231239999999999999999999999999.456456e10";
	PSmmAllocator allocator = smmCreatePermanentAllocator("test.smm", 64 * 1024 * 1024);
	PSmmLexer lex = smmInitLexer(NULL, "test.smm", allocator);
	PSmmToken t = smmGetNextToken(lex);
	while (t->type != smmEof) {
		printf("Got token %d with value %.20e  single: %.20e\n", t->type, t->floatVal, (float)t->floatVal);
		t = smmGetNextToken(lex);
	}

	printf("0.1 double=%.20e single=%.20e\n", 0.1l, 0.1f);
	printf("4.2 double=%.20e single=%.20e\n", 4.2l, 4.2f);
	
	smmPrintAllocatorInfo(allocator);

	return 0;
}