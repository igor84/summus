#include "../compiler/ibscommon.h"
#include "CuTest.h"
#include "../compiler/smmlexer.h"

static PIbsAllocator a;

static void TestParseIdent(CuTest *tc) {
	char buf[] = "whatever and something or whatever again";
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseIdent", &msgs, a);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);
	CuAssertStrEquals(tc, "whatever", token->repr);
	const char* whatever = token->repr;
	
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

static void TestParseHexNumber(CuTest *tc) {
	char buf[] = "0x0 0x1234abcd 0x567890ef 0xffffffff 0x100000000 0xFFFFFFFFFFFFFFFF "
		"0x10000000000000000 0xxrg 0x123asd 0x123.324 ";
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseHexNumber", &msgs, a);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0x1234abcd, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0x567890ef, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0xffffffff, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertTrue(tc, 0x100000000 == token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertTrue(tc, 0xFFFFFFFFFFFFFFFF == token->uintVal);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);
	// 0x10000000000000000
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that int is too big not received", msgs.items);
	PSmmMsg curMsg = msgs.items;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmIntTooBig, curMsg->type);

	// 0xxrg
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that hex number is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmInvalidDigit, curMsg->type);

	// 0x123asd
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that hex number is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmInvalidDigit, curMsg->type);

	// 0x123.324
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0x123, token->uintVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '.', token->kind);
	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, curMsg->next);
}

static void TestParseNumber(CuTest *tc) {
	char buf[] = "0 1 1234567890 4294967295 4294967296 18446744073709551615 18446744073709551616 "
		"002342 02392 02342 43abc 123.321 4.2 456E2 789E-2 901.234E+123 56789.01235E-456 "
		"234.3434E-234.34 37.b 1111111111111111111111111111111.456 1.12345678901234567890";
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseNumber", &msgs, a);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 0, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 1, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 1234567890, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 4294967295, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertTrue(tc, 4294967296 == token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertTrue(tc, 18446744073709551615U == token->uintVal);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);
	// 18446744073709551616 MAX_UINT64 + 1
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that int is too big not received", msgs.items);
	PSmmMsg curMsg = msgs.items;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmIntTooBig, curMsg->type);

	// 002342
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that number is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmInvalid0Number, curMsg->type);

	// 02392
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that number is invalid octal not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmInvalidDigit, curMsg->type);

	// 02342
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertPtrEquals_Msg(tc, "Got Unexpected err while parsing octal number", NULL, curMsg->next);
	CuAssertUIntEquals(tc, 02342, token->uintVal);

	// 43abc
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 43, token->uintVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmIdent, token->kind);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 123.321, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 4.2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 456E2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 789E-2, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 901.234E+123, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
#pragma GCC diagnostic push // Clang understands both GCC and clang pragmas
#pragma GCC diagnostic ignored "-Woverflow"
#pragma clang diagnostic ignored "-Wliteral-range"
	CuAssertDblEquals(tc, 56789.01235E-456, token->floatVal, 0);
#pragma GCC diagnostic pop

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 234.3434E-234, token->floatVal, 0);

	// . (dot)
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '.', token->kind);

	// 34
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);

	// 37.b
	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, curMsg->next);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that number is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmInvalidNumber, curMsg->type);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 1111111111111111111111111111111.456, token->floatVal, 0);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 1.12345678901234567890, token->floatVal, 0);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, curMsg->next);
}

static void TestParseNegNumber(CuTest *tc) {
	char buf[] = "123 - 321.23; -234532 - - 23423.2342; -9223372036854775807; "
		"-9223372036854775808; -9223372036854775809; -18446744073709551615";
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseNegNumber", &msgs, a);
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmUInt, token->kind);
	CuAssertUIntEquals(tc, 123, token->uintVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '-', token->kind);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, 321.23, token->floatVal, 0);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, ';', token->kind);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmInt, token->kind);
	CuAssertTrue(tc, -234532 == token->sintVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '-', token->kind);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmFloat, token->kind);
	CuAssertDblEquals(tc, -23423.2342, token->floatVal, 0);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, ';', token->kind);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmInt, token->kind);
	CuAssertTrue(tc, -9223372036854775807 == token->sintVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, ';', token->kind);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);
	// -9223372036854775808 MAX_INT64 + 1
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmInt, token->kind);
	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);
	CuAssertTrue(tc, INT64_MIN == token->sintVal);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, ';', token->kind);

	// -9223372036854775809 MAX_INT64 + 2
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that int is too big not received", msgs.items);
	PSmmMsg curMsg = msgs.items;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmIntTooBig, curMsg->type);
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, ';', token->kind);

	// -18446744073709551615
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmInt, token->kind);
	CuAssertPtrNotNullMsg(tc, "Expected err that number is too big not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmIntTooBig, curMsg->type);
}

static void TestParseChar(CuTest *tc) {
	char buf[] = "@a @z @\\n @\\t @@ @\\x20 @\\16 @\\\\ @  @\\z @\\xxx";
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseChar", &msgs, a);

	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, 'a', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, 'z', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '\n', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '\t', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '@', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '\x20', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '\20', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '\\', token->charVal);
	
	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, ' ', token->charVal);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '?', token->charVal);

	CuAssertPtrNotNullMsg(tc, "Expected err that escape sequence is invalid not received", msgs.items);
	PSmmMsg curMsg = msgs.items;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmBadStringEscape, curMsg->type);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, tkSmmChar, token->kind);
	CuAssertIntEquals(tc, '?', token->charVal);

	CuAssertPtrNotNullMsg(tc, "Expected err that escape sequence is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmBadStringEscape, curMsg->type);
}

static void assertStringToken(CuTest* tc, const char* expected, PSmmLexer lex) {
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '"', token->kind);
	CuAssertStrEquals(tc, "\"", token->repr);
	
	token = smmGetNextStringToken(lex, '"', soSmmLeaveWhitespace);
	CuAssertIntEquals(tc, tkSmmString, token->kind);
	CuAssertStrEquals(tc, expected, token->stringVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, '"', token->kind);
	CuAssertStrEquals(tc, "\"", token->repr);
}

static void assertRawStringToken(CuTest* tc, const char* startDelim, const char* expected, PSmmLexer lex) {
	const char* termChar = startDelim;
	if (startDelim[1] != 0) termChar = &startDelim[1];
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, *termChar, token->kind);
	CuAssertStrEquals(tc, startDelim, token->repr);
	
	token = smmGetNextStringToken(lex, *termChar, token->sintVal);
	CuAssertIntEquals(tc, tkSmmString, token->kind);
	CuAssertStrEquals(tc, expected, token->stringVal);

	token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, *termChar, token->kind);
	CuAssertStrEquals(tc, termChar, token->repr);
}

static void TestParseString(CuTest *tc) {
	char buf[] = "\"special \\a \\b \\f \\n \\r \\t \\v\" \"first \\\" \\\\ \\x3c \\45 string\" "
		"'special raw \\a \\b \\f \\n \\r \\t \\v' 'second \\' \" `\\` \\\\ \\x3c \\45 string' "
		"`special raw \\a \\b \\f \\n \\r \\t \\v` `second \\' \" '\\' \\\\ \\x3c \\45 string` "
		"\"bad escape \\z, bad hex \\x5z \\x\" "
		" -'  aa   bb\n\n cc\n ' |'\n  aa\n    bb \n\r  cc\ndd' |'aa\n  bb\n    cc' |'unclosed\n"
		;
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmLexer lex = smmCreateLexer(buf, "TestParseString", &msgs, a);

	assertStringToken(tc, "special \a \b \f \n \r \t \v", lex);
	assertStringToken(tc, "first \" \\ \x3c \55 string", lex);

	assertRawStringToken(tc, "'", "special raw \\a \\b \\f \\n \\r \\t \\v", lex);
	assertRawStringToken(tc, "'", "second ' \" `\\` \\\\ \\x3c \\45 string", lex);

	assertRawStringToken(tc, "`", "special raw \\a \\b \\f \\n \\r \\t \\v", lex);
	assertRawStringToken(tc, "`", "second \\' \" '\\' \\\\ \\x3c \\45 string", lex);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, msgs.items);

	assertStringToken(tc, "bad escape ?, bad hex ?5z ?", lex);

	CuAssertPtrNotNullMsg(tc, "Expected err that escape sequence is invalid not received", msgs.items);
	PSmmMsg curMsg = msgs.items;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmBadStringEscape, curMsg->type);

	CuAssertPtrNotNullMsg(tc, "Expected err that escape sequence is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmBadStringEscape, curMsg->type);

	CuAssertPtrNotNullMsg(tc, "Expected err that escape sequence is invalid not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmBadStringEscape, curMsg->type);

	assertRawStringToken(tc, "-'", " aa bb cc ", lex);
	assertRawStringToken(tc, "|'", "aa\n  bb \ncc\ndd", lex);
	assertRawStringToken(tc, "|'", "aa\nbb\n  cc", lex);

	const char* startDelim = "|'";
	const char* termChar = startDelim;
	if (startDelim[1] != 0) termChar = &startDelim[1];
	PSmmToken token = smmGetNextToken(lex);
	CuAssertIntEquals(tc, *termChar, token->kind);
	CuAssertStrEquals(tc, startDelim, token->repr);

	CuAssertPtrEquals_Msg(tc, "Got unexpected error reported", NULL, curMsg->next);
	
	token = smmGetNextStringToken(lex, *termChar, token->sintVal);
	CuAssertIntEquals(tc, tkSmmString, token->kind);
	CuAssertStrEquals(tc, "unclosed\n", token->stringVal);
	CuAssertPtrNotNullMsg(tc, "Expected err of not closed string not received", curMsg->next);
	curMsg = curMsg->next;
	CuAssertIntEquals_Msg(tc, "Expected message not received", errSmmUnclosedString, curMsg->type);
}

static void TestTokenToString(CuTest *tc) {
	struct SmmToken token = { 0, 0, 0, "repr" };
	char buf[4] = { 0 };
	const char* res = smmTokenToString(&token, buf);
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
	a = ibsSimpleAllocatorCreate("lexerTest", 64 * 1024 * 1024);
	CuSuite* suite = CuSuiteNew();

	SUITE_ADD_TEST(suite, TestParseIdent);
	SUITE_ADD_TEST(suite, TestParseHexNumber);
	SUITE_ADD_TEST(suite, TestParseNumber);
	SUITE_ADD_TEST(suite, TestParseNegNumber);
	SUITE_ADD_TEST(suite, TestParseString);
	SUITE_ADD_TEST(suite, TestParseChar);
	SUITE_ADD_TEST(suite, TestTokenToString);

	return suite;
}
