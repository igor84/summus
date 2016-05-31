#include "CuTest.h"
#include "../compiler/smmparser.c"

static PSmmAllocator allocator;

void TestSomething(CuTest *tc) {
	
}


CuSuite* SmmParserGetSuite() {
	allocator = smmCreatePermanentAllocator("parserTest", 1024 * 1024);
	CuSuite* suite = CuSuiteNew();
	SUITE_ADD_TEST(suite, TestSomething);
	return suite;
}
