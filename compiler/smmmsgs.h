#ifndef SMM_MSGS_H
#define SMM_MSGS_H

/**
 * Functions for reporting compiler errors.
 *
 * We use enum of possible error messages so we can later easily define that
 * some range of enums are warnings or hints and provide the option to disable
 * some of them.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	errSmmUnknown, errSmmMemoryAllocationFailed,
	errSmmInvalidHexDigit, errSmmIntTooBig, errSmmInvalidFloatExponent, errSmmInvalid0Number,
	errSmmInvalidNumber, errSmmInvalidCharacter,

	errSmmNoExpectedToken, errSmmGotUnexpectedToken, errSmmUndefinedIdentifier, errSmmRedefinition,
	errSmmOperandMustBeLVal, errSmmUnknownType, errSmmIdentTaken, errSmmBadOperandsType,

	wrnSmmConversionDataLoss
} SmmMsgType;

struct SmmFilePos {
	const char* filename;
	int32_t lineNumber;
	int32_t lineOffset;
};
typedef struct SmmFilePos* PSmmFilePos;

void smmPostMessage(SmmMsgType msgType, const struct SmmFilePos filePos, ...);
void smmAbortWithMessage(SmmMsgType msgType, const char* additionalInfo, const char* filename, const int line);

bool smmHadErrors();

#endif
