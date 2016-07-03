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
	"unknown error",
	"failed allocating memory",
	"invalid hex digit",
	"integer literal too big",
	"invalid exponent in float literal",
	"only binary, hex and float literals can start with 0",
	"invalid number literal",
	"invalid character",

	"missing expected %s",
	"expected %s but got %s",
	"identifier '%s' is undefined",
	"identifier '%s' is already defined",
	"operand must be l-value",
	"undefined type '%s'",
	"identifier '%s' is already taken as %s",
	"operator %s not defined for operands of type %s",
	"got %s but expected one of: \n %s",
	"can't assign a value to a constant",
	"non constant values are not allowed in constant expressions",
	"type of return expression: %s doesn't match function return type: %s",

	"possible loss of data in conversion from %s to %s"
};

static int errorCounter;

void smmPostMessage(SmmMsgType msgType, const struct SmmFilePos filePos, ...) {
	const char* lvl;
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

	if (filePos.filename) {
		printf("%s: %s (at %s:%d:%d)\n", lvl, msg, filePos.filename, filePos.lineNumber, filePos.lineOffset);
	} else {
		printf("%s: %s (at %d:%d)\n", lvl, msg, filePos.lineNumber, filePos.lineOffset);
	}
}

void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* filename, const int line) {
	printf("Compiler Error: %s %s (at %s:%d)\n", msgTypeToString[msgType], additionalInfo, filename, line);
	exit(EXIT_FAILURE);
}

bool smmHadErrors() {
	return errorCounter > 0;
}
