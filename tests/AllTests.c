#include "CuTest.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
// MSDN recommends against using getcwd & chdir names
#define cwd _getcwd
#define cd _chdir
#else
#include "unistd.h"
#define cwd getcwd
#define cd chdir
#endif

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

char buf[4096];

int main(void) {
	// Simple attempt to make sure we are in tests directory
	cwd(buf, sizeof(buf));
	char* cur = buf;
	while (*cur != 0) {
		if (*cur == '\\') *cur = '/';
		cur++;
	}
	if (!strstr(buf, "/tests/")) cd("tests");

	return RunAllTests();
}
