#include "../compiler/smmcommon.h"
#include "CuTest.h"
#include "../compiler/smmlexer.c"

static PSmmAllocator allocator;

void TestSkipWhiteSpace(CuTest *tc) {
#define spaces "   "
#define newLines "\n\n"
	char buf[] = spaces newLines spaces "whatever";
	PSmmLexer lex = smmCreateLexer(buf, "testWhiteSpace", allocator);
	skipWhitespaceFromBuffer(lex);
	CuAssertIntEquals(tc, 'w', *lex->curChar);
	CuAssertUIntEquals(tc, strlen(spaces newLines spaces), (int)lex->scanCount);
	CuAssertUIntEquals(tc, strlen(spaces), lex->filePos.lineNumber);
	CuAssertUIntEquals(tc, strlen(newLines) + 2, lex->filePos.lineOffset);
}

void TestParseIdent(CuTest *tc) {
	char buf[] = "whatever and something or whatever again";
	PSmmLexer lex = smmCreateLexer(buf, "TestParseIdent", allocator);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);
	CuAssertStrEquals(tc, "whatever", token->repr);
	char* whatever = token->repr;
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmAndOp, token->kind);
	CuAssertStrEquals(tc, "and", token->repr);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);
	CuAssertStrEquals(tc, "something", token->repr);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmOrOp, token->kind);
	CuAssertStrEquals(tc, "or", token->repr);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);
	CuAssertPtrEquals(tc, whatever, token->repr);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);
	CuAssertStrEquals(tc, "again", token->repr);
}

void TestParseHexNumber(CuTest *tc) {
	char buf[] = "0x0 0x1234abcd 0x567890ef 0xffffffff 0x100000000 0xFFFFFFFFFFFFFFFF "
		"0x10000000000000000 0xxrg 0x123asd 0x123.324 ";
	PSmmLexer lex = smmCreateLexer(buf, "TestParseHexNumber", allocator);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0x1234abcd, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0x567890ef, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0xffffffff, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt64, token->kind);
	CuAssertTrue(tc, 0x100000000 == token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt64, token->kind);
	CuAssertTrue(tc, 0xFFFFFFFFFFFFFFFF == token->intVal);

	// 0x10000000000000000
	struct SmmFilePos filepos = { 0 };
	smmPostMessage(errSmmIntTooBig, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 0xxrg
	smmPostMessage(errSmmInvalidHexDigit, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 0x123asd
	smmPostMessage(errSmmInvalidHexDigit, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 0x123.324
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0x123, token->intVal);
	smmPostMessage(errSmmInvalidCharacter, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);
	CuAssertStrEquals(tc, ".", token->repr);
}

void TestParseNumber(CuTest *tc) {
	char buf[] = "0 1 1234567890 4294967295 4294967296 18446744073709551615 18446744073709551616 02342 43abc "
		"123.321 4.2 456E2 789E-2  901.234E+123 56789.01235E-456 234.3434E-234.34 37.b "
		"1111111111111111111111111111111.456 1.12345678901234567890";
	PSmmLexer lex = smmCreateLexer(buf, "TestParseNumber", allocator);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 0, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 1, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 1234567890, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 4294967295, token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt64, token->kind);
	CuAssertTrue(tc, 4294967296 == token->intVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt64, token->kind);
	CuAssertTrue(tc, 18446744073709551615U == token->intVal);

	// 18446744073709551616 MAX_UINT64 + 1
	struct SmmFilePos filepos = { 0 };
	smmPostMessage(errSmmIntTooBig, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 02342
	smmPostMessage(errSmmInvalid0Number, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 43abc
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);
	CuAssertUIntEquals(tc, 43, token->intVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 123.321, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 4.2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 456E2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 789E-2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 901.234E+123, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
#pragma GCC diagnostic push // Clang understands both GCC and clang pragmas
#pragma GCC diagnostic ignored "-Woverflow"
#pragma clang diagnostic ignored "-Wliteral-range"
	CuAssertDblEquals(tc, 56789.01235E-456, token->floatVal, 0);
#pragma GCC diagnostic pop

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 234.3434E-234, token->floatVal, 0);

	// . (dot)
	smmPostMessage(errSmmInvalidCharacter, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmErr, token->kind);

	// 34
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);

	// 37.b
	smmPostMessage(errSmmInvalidNumber, (char*)tc, filepos);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt32, token->kind);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 1111111111111111111111111111111.456, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat64, token->kind);
	CuAssertDblEquals(tc, 1.12345678901234567890, token->floatVal, 0);
}

void TestTokenToString(CuTest *tc) {
	struct SmmToken token = { 0, "repr" };
	char buf[4] = { 0 };
	char* res = smmTokenToString(&token, buf);
	CuAssertStrEquals(tc, "repr", res);
	CuAssertStrEquals(tc, "", buf);

	token.kind = tkSmmIdent;
	res = smmTokenToString(&token, buf);
	CuAssertStrEquals(tc, "identifier", res);
	CuAssertStrEquals(tc, "", buf);

	token.kind = '+';
	res = smmTokenToString(&token, buf);
	CuAssertStrEquals(tc, "'+'", res);
	CuAssertPtrEquals(tc, buf, res);
}

CuSuite* SmmLexerGetSuite() {
	allocator = smmCreatePermanentAllocator("lexerTest", 64 * 1024 * 1024);
	CuSuite* suite = CuSuiteNew();
	SUITE_ADD_TEST(suite, TestSkipWhiteSpace);
	SUITE_ADD_TEST(suite, TestParseIdent);
	SUITE_ADD_TEST(suite, TestParseHexNumber);
	SUITE_ADD_TEST(suite, TestParseNumber);
	SUITE_ADD_TEST(suite, TestTokenToString);
	return suite;
}
