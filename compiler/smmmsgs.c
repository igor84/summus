#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "smmmsgs.h"
#include "smmutil.h"

#define MSG_BUFFER_LENGTH 2000
#define WARNING_START wrnSmmConversionDataLoss

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
	"operator %s not defined for operands of type %s",

	"possible loss of data in conversion from %s to %s"
};

static int errorCounter;

void smmPostMessage(SmmMsgType msgType, const char* fileName, const struct SmmFilePos filePos, ...) {
	char* lvl;
	if (msgType < WARNING_START) {
		errorCounter++;
		lvl = "ERROR";
	} else {
		lvl = "WARNING";
	}
	char msg[MSG_BUFFER_LENGTH] = { 0 };
	
	va_list argList;
	va_start(argList, filePos);
	vsnprintf(msg, MSG_BUFFER_LENGTH, msgTypeToString[msgType], argList);
	va_end(argList);

	if (fileName) {
		printf("%s: %s (at %s:%d:%d)\n", lvl, msg, fileName, filePos.lineNumber, filePos.lineOffset);
	} else {
		printf("%s: %s (at %d:%d)\n", lvl, msg, filePos.lineNumber, filePos.lineOffset);
	}
}

void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* fileName, const int line) {
	printf("Compiler Error: %s %s (at %s:%d)\n", msgTypeToString[msgType], additionalInfo, fileName, line);
	exit(EXIT_FAILURE);
}

bool smmHadErrors() {
	return errorCounter > 0;
}