#include "../compiler/smmcommon.h"
#include "CuTest.h"
#include "../compiler/smmparser.h"
#include "smmastwritter.h"
#include "smmastreader.h"
#include "smmastmatcher.h"
#include "smmmsgsMockup.h"

#include <string.h>
#include <stdlib.h>

static int sampleNo = 1;
static char* msgTypeEnumToString[hintSmmTerminator + 1];
static PSmmDict msgTypeStrToEnum;

#define MAX_MSGS 100

static struct {
	char* msgs[MAX_MSGS];
	int msgCount;
} receivedMsgs;

#define SAMPLE_FORMAT "sample%.4d"

void parserOnPostMessage(SmmMsgType msgType) {
	receivedMsgs.msgs[receivedMsgs.msgCount] = msgTypeEnumToString[msgType];
	receivedMsgs.msgCount++;
}

static PSmmAstNode loadModule(const char* filename, const char* moduleName, PSmmAllocator a) {
	char filebuf[64 * 1024] = { 0 };
	FILE* f = fopen(filename, "rb");
	if (!f) {
		printf("Can't find %s!\n", filename);
		exit(EXIT_FAILURE);
	}
	fread(filebuf, 1, 64 * 1024, f);
	fclose(f);

	onPostMessageCalled = parserOnPostMessage;
	PSmmLexer lex = smmCreateLexer(filebuf, moduleName, a);

	PSmmParser parser = smmCreateParser(lex, a);

	PSmmAstNode module = smmParse(parser);
	onPostMessageCalled = NULL;

	return module;
}

static void checkMsgs(CuTest* tc, PSmmLexer lex) {
	PSmmToken t = smmGetNextToken(lex);
	int msgCount = 0;
	while (strcmp(t->repr, "MODULE") != 0 && msgCount < MAX_MSGS) {
		if (msgCount < receivedMsgs.msgCount) {
			CuAssertStrEquals(tc, t->repr, receivedMsgs.msgs[msgCount]);
		}
		t = smmGetNextToken(lex);
		msgCount++;
	}
	CuAssertIntEquals_Msg(tc, "Number of reported messages not matched", msgCount, receivedMsgs.msgCount);
}

static void TestSample(CuTest *tc) {
	memset(&receivedMsgs, 0, sizeof(receivedMsgs));

	char baseName[20] = { 0 };
	snprintf(baseName, 20, SAMPLE_FORMAT, sampleNo++);
	char inFileName[30] = { 0 };
	char outFileName[30] = { 0 };
	snprintf(inFileName, 30, "samples/%s.smm", baseName);
	snprintf(outFileName, 30, "samples/%s.ast", baseName);
	PSmmAllocator a = smmCreatePermanentAllocator(baseName, 1024 * 1024);
	PSmmAstNode module = loadModule(inFileName, baseName, a);
	if (!module) return;
	FILE* f = fopen(outFileName, "rb");
	if (!f) {
		snprintf(outFileName, 30, "samples/%sTST.ast", baseName);
		f = fopen(outFileName, "ab");
		if (!f) {
			printf("Can't open %s for writing!\n", outFileName);
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < receivedMsgs.msgCount; i++) {
			fprintf(f, "%s\n", receivedMsgs.msgs[i]);
		}
		fputs("\n", f);
		smmOutputAst(module, f, a);
		fclose(f);
		printf("Generated new test data for %s\n", baseName);
	} else {
		char filebuf[64 * 1024] = { 0 };
		fread(filebuf, 1, 64 * 1024, f);
		fclose(f);
		PSmmLexer lex = smmCreateLexer(filebuf, baseName, a);
		checkMsgs(tc, lex);
		PSmmAstNode refModule = smmLoadAst(lex, a);
		smmAssertASTEquals(tc, refModule, module);
	}

	smmPrintAllocatorInfo(a);
	smmFreePermanentAllocator(a);
}

static void loadMsgStrings(PSmmAllocator a) {
	if (msgTypeStrToEnum) return;
	msgTypeStrToEnum = smmCreateDict(a, NULL, NULL);
	msgTypeStrToEnum->storeKeyCopy = false;
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
		SmmMsgType* tokenData = a->alloc(a, strlen(token) + sizeof(SmmMsgType) + 1);
		*tokenData = enumVal;
		char* tokenStr = (char*)(tokenData + 1);
		strcpy(tokenStr, token);
		msgTypeEnumToString[enumVal++] = tokenStr;
		smmAddDictValue(msgTypeStrToEnum, tokenStr, tokenData);
		token = strtok(NULL, delims);
	}
}

CuSuite* SmmParserGetSuite() {
	CuSuite* suite = CuSuiteNew();
	loadMsgStrings(smmCreatePermanentAllocator("msgData", 4 * 1024));
	for (int i = 1; i < 10000; i++) {
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
