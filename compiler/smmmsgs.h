#pragma once

#ifndef SMM_MSGS_H
#define SMM_MSGS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	errSmmUnknown,
	errSmmInvalidHexDigit, errSmmIntTooBig, errSmmInvalidFloatExponent, errSmmInvalidNumber,
	errSmmInvalidCharacter
} SmmMsgType;

struct SmmFilePos {
	int32_t lineNumber;
	int32_t lineOffset;
};
typedef struct SmmFilePos* PSmmFilePos;

void smmPostMessage(SmmMsgType msg, const char* fileName, const struct SmmFilePos filePos);

bool smmHadErrors();

#endif