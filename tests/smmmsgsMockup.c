#include "../compiler/smmcommon.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "smmmsgsMockup.h"
#include "CuTest.h"

void smmPostMessage(SmmMsgType msgType, const struct SmmFilePos filePos, ...) {
	if (onPostMessageCalled) onPostMessageCalled(msgType);
}

void smmAbortWithMessage(const char* msg, const char* filename, const int line) {
	printf("Compiler Error: %s (at %s:%d)\n", msg, filename, line);
	exit(EXIT_FAILURE);
}

bool smmHadErrors() {
	return true;
}
