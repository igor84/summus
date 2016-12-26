#pragma once

/**
* Functions for reporting compiler errors.
*
* We use enum of possible error messages so we can later easily define that
* some range of enums are warnings or hints and provide the option to disable
* some of them.
*/

#include "ibsallocator.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	errSmmUnknown,
	errSmmInvalidDigit, errSmmIntTooBig, errSmmInvalidFloatExponent, errSmmInvalid0Number,
	errSmmInvalidNumber, errSmmInvalidCharacter, errSmmBadStringEscape, errSmmUnclosedString,

	errSmmNoExpectedToken, errSmmGotUnexpectedToken, errSmmUndefinedIdentifier, errSmmRedefinition,
	errSmmOperandMustBeLVal, errSmmUnknownType, errSmmIdentTaken, errSmmBadOperandsType,
	errSmmGotBadArgs, errSmmCantAssignToConst, errSmmNonConstInConstExpression,
	errSmmBadReturnStmtType, errSmmFuncMustReturnValue, errSmmUnreachableCode,
	errSmmFuncUnderScope, errSmmUnexpectedBool, errSmmBangUsedAsNot, errSmmNotAFunction,
	errSmmInvalidExprUsed, errSmmNoReturnValueNeeded, errSmmFuncRedefinition,
	errSmmCircularDefinition,

	wrnSmmConversionDataLoss, wrnSmmNoEffectStmt, wrnSmmComparingSignedAndUnsigned,

	hintSmmTerminator
} SmmMsgType;

struct SmmFilePos {
	const char* filename;
	uint32_t lineNumber;
	uint32_t lineOffset;
};
typedef struct SmmFilePos* PSmmFilePos;

typedef struct SmmMsg* PSmmMsg;
struct SmmMsg {
	SmmMsgType type;
	char* text;
	struct SmmFilePos filePos;
	PSmmMsg next;
};

struct SmmMsgs {
	PIbsAllocator a;
	PSmmMsg items;
	uint16_t errorCount;
	uint16_t warningCount;
	uint16_t hintCount;
};
typedef struct SmmMsgs* PSmmMsgs;

void smmPostMessage(PSmmMsgs msgs, SmmMsgType msgType, struct SmmFilePos filePos, ...);

// We define separate functions for all messages that take two or more params so autocomplete
// can help us avoid confusion what are the params and in which order should they be given
void smmPostGotUnexpectedToken(PSmmMsgs msgs, struct SmmFilePos filePos, const char* expected, const char* got);
void smmPostIdentTaken(PSmmMsgs msgs, struct SmmFilePos filePos, const char* identifier, const char* takenAs);
void smmPostGotBadOperands(PSmmMsgs msgs, struct SmmFilePos filePos, const char* operator, const char* gotType);
void smmPostGotBadArgs(PSmmMsgs msgs, struct SmmFilePos filePos, const char* gotSig, const char* expectedSigs);
void smmPostGotBadReturnType(PSmmMsgs msgs, struct SmmFilePos filePos, const char* gotType, const char* expectedType);
void smmPostConversionLoss(PSmmMsgs msgs, struct SmmFilePos filePos, const char* fromType, const char* toType);

void smmFlushMessages(PSmmMsgs msgs);
void smmAbortWithMessage(const char* msg, const char* filename, const int line);

bool smmHadErrors(PSmmMsgs msgs);
