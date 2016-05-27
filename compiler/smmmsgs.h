#pragma once

#ifndef SMM_MSGS_H
#define SMM_MSGS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	errSmmUnknown, errSmmMemoryAllocationFailed,
	errSmmInvalidHexDigit, errSmmIntTooBig, errSmmInvalidFloatExponent, errSmmInvalid0Number,
	errSmmInvalidNumber, errSmmInvalidCharacter,

	errSmmNoExpectedToken, errSmmGotUnexpectedToken, errSmmUndefinedIdentifier, errSmmRedefinition,
	errSmmOperandMustBeLVal, errSmmUnknownType, errSmmIdentTaken, errSmmBadOperandsType
} SmmMsgType;

struct SmmFilePos {
	int32_t lineNumber;
	int32_t lineOffset;
};
typedef struct SmmFilePos* PSmmFilePos;

void smmPostMessage(SmmMsgType msgType, const char* fileName, const struct SmmFilePos filePos, ...);
void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* fileName, const int line);

bool smmHadErrors();

#endif