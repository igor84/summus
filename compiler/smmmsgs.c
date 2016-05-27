#define _CRT_SECURE_NO_WARNINGS
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "smmmsgs.h"
#include "smmutil.h"

static const char* msgTypeToString[] = {
	"unknown error", "failed allocating memory",
	"invalid hex digit",
	"integer literal too big",
	"invalid exponent in float literal",
	"only binary, hex and float literals can start with 0",
	"invalid number literal",
	"invalid character",
	"missing expected %s",
	"expected %s but got '%s'",
	"identifier '%s' is undefined",
	"identifier '%s' is already defined",
	"operand must be l-value",
	"undefined type '%s'",
	"identifier '%s' is already taken as %s",
	"operator %s not defined for operands of type %s"
};

static int errorCounter;

void smmPostMessage(SmmMsgType msgType, const char* fileName, const struct SmmFilePos filePos, ...) {
	errorCounter++;
	char msg[2000] = { 0 };
	
	va_list argList;
	va_start(argList, filePos);
	vsprintf(msg, msgTypeToString[msgType], argList);
	va_end(argList);

	if (fileName) {
		printf("Error: %s (at %s:%d:%d)\n", msg, fileName, filePos.lineNumber, filePos.lineOffset);
	} else {
		printf("Error: %s (at %d:%d)\n", msg, filePos.lineNumber, filePos.lineOffset);
	}
}

void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* fileName, const int line) {
	printf("Compiler Error: %s %s (at %s:%d)\n", msgTypeToString[msgType], additionalInfo, fileName, line);
	exit(1);
}

bool smmHadErrors() {
	return errorCounter > 0;
}