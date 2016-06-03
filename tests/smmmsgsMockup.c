#include "../compiler/smmcommon.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../compiler/smmmsgs.h"
#include "CuTest.h"

static int errorCounter;

static SmmMsgType expected;
static CuTest* currentTC;

void smmPostMessage(SmmMsgType msgType, const char* fileName, const struct SmmFilePos filePos, ...) {
	if (currentTC) {
		errorCounter++;
		CuAssertIntEquals(currentTC, expected, msgType);
		currentTC = NULL;
	} else {
		expected = msgType;
		currentTC = (CuTest*)fileName;
	}
}

void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* fileName, const int line) {
	printf("Compiler Error: %d %s (at %s:%d)\n", msgType, additionalInfo, fileName, line);
	exit(EXIT_FAILURE);
}

bool smmHadErrors() {
	return errorCounter > 0;
}