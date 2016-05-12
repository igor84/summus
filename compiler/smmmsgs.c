#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "smmmsgs.h"
#include "smmutil.h"

static const char* smmMsgTypeToString[] = {
	"unknown error", "failed allocating memory",
	"invalid hex digit",
	"integer literal too big",
	"invalid exponent in float literal",
	"only binary, hex and float literals can start with 0",
	"invalid character"
};

static int smmErrorCounter;

void smmPostMessage(SmmMsgType msg, const char* fileName, const struct SmmFilePos filePos) {
	smmErrorCounter++;
	if (fileName) {
		printf("Error: %s (at %s:%d:%d)\n", smmMsgTypeToString[msg], fileName, filePos.lineNumber, filePos.lineOffset);
	} else {
		printf("Error: %s (at %d:%d)\n", smmMsgTypeToString[msg], filePos.lineNumber, filePos.lineOffset);
	}
}

void smmAbortWithMessage(SmmMsgType msg, const char* additionalInfo, const char* fileName, const int line) {
	printf("Compiler Error: %s %s (at %s:%d)\n", smmMsgTypeToString[msg], additionalInfo, fileName, line);
	exit(1);
}

bool smmHadErrors() {
	return smmErrorCounter > 0;
}