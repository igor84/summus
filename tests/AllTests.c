#include "CuTest.h"
#include <stdio.h>

CuSuite* SmmLexerGetSuite();
CuSuite* SmmParserGetSuite();

int RunAllTests(void) {
	CuString *output = CuStringNew();
	CuSuite* suite = CuSuiteNew();

	CuSuiteAddSuite(suite, SmmLexerGetSuite());
	CuSuiteAddSuite(suite, SmmParserGetSuite());

	CuSuiteRun(suite);
	CuSuiteSummary(suite, output);
	CuSuiteDetails(suite, output);
	printf("%s\n", output->buffer);
	return suite->failCount;
}

int main(void) {
	return RunAllTests();
}
