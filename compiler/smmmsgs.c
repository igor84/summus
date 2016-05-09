#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "smmmsgs.h"

static const char* smmMsgTypeToString[] = {
	"unknown error",
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
		printf("Error: %s (at %s:%d:%d)", smmMsgTypeToString[msg], fileName, filePos.lineNumber, filePos.lineOffset);
	} else {
		printf("Error: %s (at %d:%d)", smmMsgTypeToString[msg], filePos.lineNumber, filePos.lineOffset);
	}
}

bool smmHadErrors() {
	return smmErrorCounter > 0;
}