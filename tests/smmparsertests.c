#include "../compiler/ibscommon.h"
#include "CuTest.h"
#include "../compiler/smmparser.h"
#include "../compiler/smmtypeinference.h"
#include "../compiler/smmsempass.h"
#include "smmastwritter.h"
#include "smmastreader.h"
#include "smmastmatcher.h"

#include <string.h>
#include <stdlib.h>

static int sampleNo = 1;
static char* msgTypeEnumToString[hintSmmTerminator + 1];
static PIbsDict msgTypeStrToEnum;

#define MAX_MSGS 100
#define WARNING_START wrnSmmConversionDataLoss

#define SAMPLE_FORMAT "sample%.4d"

static PSmmAstNode loadModule(const char* filename, const char* moduleName, PSmmMsgs msgs, PIbsAllocator a) {
	char filebuf[64 * 1024] = { 0 };
	FILE* f = fopen(filename, "rb");
	if (!f) {
		printf("Can't find %s!\n", filename);
		exit(EXIT_FAILURE);
	}
	fread(filebuf, 1, 64 * 1024, f);
	fclose(f);

	PSmmLexer lex = smmCreateLexer(filebuf, moduleName, msgs, a);

	PSmmParser parser = smmCreateParser(lex, msgs, a);

	PSmmAstNode module = smmParse(parser);

	return module;
}

static void checkMsgs(CuTest* tc, PSmmLexer lex, PSmmMsgs msgs) {
	PSmmToken t = smmGetNextToken(lex);
	int msgCount = 0;
	int rcvCount = 0;
	PSmmMsg curMsg = msgs->items;
	while (strcmp(t->repr, "MODULE") != 0 && msgCount < MAX_MSGS) {
		if (curMsg) {
			CuAssertStrEquals(tc, t->repr, msgTypeEnumToString[curMsg->type]);
			smmGetNextToken(lex); // Skip ':'
			uint32_t lineNo = (uint32_t)smmGetNextToken(lex)->uintVal;
			smmGetNextToken(lex); // Skip ':'
			uint32_t lineOffset = (uint32_t)smmGetNextToken(lex)->uintVal;
			CuAssertIntEquals_Msg(tc, "Error line numbers differ", lineNo, curMsg->filePos.lineNumber);
			CuAssertIntEquals_Msg(tc, "Error line offsets differ", lineOffset, curMsg->filePos.lineOffset);
			curMsg = curMsg->next;
			rcvCount++;
		}
		t = smmGetNextToken(lex);
		msgCount++;
	}
	CuAssertIntEquals_Msg(tc, "Number of reported messages not matched", msgCount, rcvCount);
}

static void writeReceivedMsgs(FILE* f, PSmmMsgs msgs) {
	PSmmMsg curMsg = msgs->items;
	while (curMsg) {
		fprintf(f, "%s:%d:%d\n", msgTypeEnumToString[curMsg->type], curMsg->filePos.lineNumber, curMsg->filePos.lineOffset);
		curMsg = curMsg->next;
	}
	fputs("\n", f);
}

static void TestSample(CuTest *tc) {
	char baseName[20] = { 0 };
	snprintf(baseName, 20, SAMPLE_FORMAT, sampleNo++);
	char inFileName[30] = { 0 };
	char outFileName[30] = { 0 };
	snprintf(inFileName, 30, "samples/%s.smm", baseName);
	snprintf(outFileName, 30, "samples/%s.ast", baseName);
	PIbsAllocator a = ibsSimpleAllocatorCreate(baseName, 1024 * 1024);
	struct SmmMsgs msgs = { 0 };
	msgs.a = a;
	PSmmAstNode module = loadModule(inFileName, baseName, &msgs, a);
	if (!module) return;
	smmExecuteTypeInferencePass(module, &msgs, a);
	FILE* f = fopen(outFileName, "rb");
	if (!f) {
		f = fopen(outFileName, "wb");
		if (!f) {
			printf("Can't open %s for writing!\n", outFileName);
			exit(EXIT_FAILURE);
		}
		writeReceivedMsgs(f, &msgs);
		smmOutputAst(module, f, a);
		if (msgs.errorCount == 0) {
			msgs.items = NULL;
			fputs("ENDMODULE\n\n", f);
			smmExecuteSemPass(module, &msgs, a);
			writeReceivedMsgs(f, &msgs);
			smmOutputAst(module, f, a);
		}
		fclose(f);
		printf("Generated new test data for %s\n", baseName);
	} else {
		char filebuf[64 * 1024] = { 0 };
		fread(filebuf, 1, 64 * 1024, f);
		fclose(f);
		struct SmmMsgs tmpMsgs = { 0 };
		tmpMsgs.a = a;
		PSmmLexer lex = smmCreateLexer(filebuf, baseName, &tmpMsgs, a);
		CuAssertPtrEquals_Msg(tc, "Error while parsing ast file", NULL, tmpMsgs.items);
		checkMsgs(tc, lex, &msgs);
		PSmmAstNode refModule = smmLoadAst(lex, a);
		smmAssertASTEquals(tc, refModule, module);
		refModule = NULL;
		if (msgs.errorCount == 0) {
			msgs.items = NULL;
			smmExecuteSemPass(module, &msgs, a);
			checkMsgs(tc, lex, &msgs);
			refModule = smmLoadAst(lex, a);
			smmAssertASTEquals(tc, refModule, module);
		}
	}

	ibsSimpleAllocatorPrintInfo(a);
	ibsSimpleAllocatorFree(a);
}

static void loadMsgStrings(PIbsAllocator a) {
	if (msgTypeStrToEnum) return;
	msgTypeStrToEnum = ibsDictCreate(a);
	const char* filename = "../compiler/smmmsgs.h";
	FILE* f = fopen(filename, "rb");
	if (!f) {
		fprintf(f, "\nCould not open file %s!\n", filename);
		exit(EXIT_FAILURE);
	}
	char filebuf[16 * 1024] = { 0 };
	fread(filebuf, 1, 16 * 1024, f);
	fclose(f);

	char* start = strstr(filebuf, "errSmmUnknown");
	if (!start) {
		fprintf(f, "\nCould not find errSmmUnknown in file %s!\n", filename);
		exit(EXIT_FAILURE);
	}

	const char* delims = ", \t\n\r";
	SmmMsgType enumVal = 0;
	char* token = strtok(start, delims);
	while (token && token[0] != '}') {
		SmmMsgType* tokenData = ibsAlloc(a, strlen(token) + sizeof(SmmMsgType) + 1);
		*tokenData = enumVal;
		char* tokenStr = (char*)(tokenData + 1);
		strcpy(tokenStr, token);
		msgTypeEnumToString[enumVal++] = tokenStr;
		ibsDictPut(msgTypeStrToEnum, tokenStr, tokenData);
		token = strtok(NULL, delims);
	}
}

CuSuite* SmmParserGetSuite() {
	CuSuite* suite = CuSuiteNew();
	loadMsgStrings(ibsSimpleAllocatorCreate("msgData", 4 * 1024));
	for (int i = sampleNo; i < 10000; i++) {
		char filename[30] = { 0 };
		snprintf(filename, 30, "samples/" SAMPLE_FORMAT ".smm", i);
		FILE* f = fopen(filename, "rb");
		if (f) {
			CuSuiteAdd(suite, CuTestNew(filename, TestSample));
			fclose(f);
		} else {
			break;
		}
	}
	return suite;
}
