#include "smmmsgs.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#define WARNING_START wrnSmmConversionDataLoss
#define MSG_BUFFER_MAX_LENGTH 2000

static const char* msgTypeToString[] = {
	"unknown error",
	"invalid %s digit",
	"integer literal too big",
	"invalid exponent in float literal",
	"only binary, hex and float literals can start with 0",
	"invalid number literal",
	"invalid character",
	"invalid escape sequence",
	"unclosed string literal starting at line %d",

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
	"function must return a value",
	"unreachable code",
	"function '%s' must be defined in top scope",
	"unexpected bool operand found",
	"'!' used as not operator, use 'not' instead",
	"'%s' is not a function",
	"expected expression that produces a value",
	"function should not return any value",
	"function with same parameters already defined",
	"curcular definition detected for '%s'",

	"possible loss of data in conversion from %s to %s",
	"statement without effect",
	"comparing signed and unsigned values can have unpredictable results. Add explicit casts to avoid this warning",
};

void smmPostMessage(PSmmMsgs msgs, SmmMsgType msgType, struct SmmFilePos filePos, ...) {
	if (msgType < WARNING_START) {
		msgs->errorCount++;
	} else {
		msgs->warningCount++;
	}
	PSmmMsg msg = ibsAlloc(msgs->a, sizeof(struct SmmMsg));
	msg->type = msgType;
	msg->filePos = filePos;
	msg->text = ibsStartAlloc(msgs->a);

	va_list argList;
	va_start(argList, filePos);
	int written = vsnprintf(msg->text, MSG_BUFFER_MAX_LENGTH, msgTypeToString[msgType], argList);
	if (written >= MSG_BUFFER_MAX_LENGTH) written = MSG_BUFFER_MAX_LENGTH - 1;
	va_end(argList);

	ibsEndAlloc(msgs->a, written + 1);

	// We keep the messages sorted by filepos because different compiler passes can report
	// errors in various positions out of order.
	PSmmMsg* curMsgField = &msgs->items;
	while (
		*curMsgField
		&& (*curMsgField)->filePos.lineNumber < filePos.lineNumber
	) {
		curMsgField = &(*curMsgField)->next;
	}
	while (
		*curMsgField
		&& (*curMsgField)->filePos.lineNumber == filePos.lineNumber
		&& (*curMsgField)->filePos.lineOffset < filePos.lineOffset
	) {
		curMsgField = &(*curMsgField)->next;
	}

	msg->next = *curMsgField;
	*curMsgField = msg;
}

void smmPostGotUnexpectedToken(PSmmMsgs msgs, struct SmmFilePos filePos, const char* expected, const char* got) {
	smmPostMessage(msgs, errSmmGotUnexpectedToken, filePos, expected, got);
}

void smmPostIdentTaken(PSmmMsgs msgs, struct SmmFilePos filePos, const char* identifier, const char* takenAs) {
	smmPostMessage(msgs, errSmmIdentTaken, filePos, identifier, takenAs);
}

void smmPostGotBadOperands(PSmmMsgs msgs, struct SmmFilePos filePos, const char* operator, const char* gotType) {
	smmPostMessage(msgs, errSmmBadOperandsType, filePos, operator, gotType);
}

void smmPostGotBadArgs(PSmmMsgs msgs, struct SmmFilePos filePos, const char* gotSig, const char* expectedSigs) {
	smmPostMessage(msgs, errSmmGotBadArgs, filePos, gotSig, expectedSigs);
}

void smmPostGotBadReturnType(PSmmMsgs msgs, struct SmmFilePos filePos, const char* gotType, const char* expectedType) {
	smmPostMessage(msgs, errSmmBadReturnStmtType, filePos, gotType, expectedType);
}

void smmPostConversionLoss(PSmmMsgs msgs, struct SmmFilePos filePos, const char* fromType, const char* toType) {
	smmPostMessage(msgs, wrnSmmConversionDataLoss, filePos, fromType, toType);
}

void smmFlushMessages(PSmmMsgs msgs) {
	PSmmMsg curMsg = msgs->items;

	while (curMsg) {
		const char* lvl;
		if (curMsg->type < WARNING_START) {
			lvl = "ERROR";
		} else {
			lvl = "WARNING";
		}

		if (curMsg->filePos.filename) {
			printf("%s (at %s:%d:%d): %s\n", lvl,
				curMsg->filePos.filename,
				curMsg->filePos.lineNumber,
				curMsg->filePos.lineOffset,
				curMsg->text);
		} else {
			printf("%s (at %d:%d): %s\n", lvl,
				curMsg->filePos.lineNumber,
				curMsg->filePos.lineOffset,
				curMsg->text);
		}
		curMsg = curMsg->next;
	}
}

void smmAbortWithMessage(const char * msg, const char * filename, const int line) {
	printf("Compiler Error: %s (at %s:%d)\n", msg, filename, line);
	exit(EXIT_FAILURE);
}

bool smmHadErrors(PSmmMsgs msgs) {
	return msgs->errorCount > 0;
}

