#pragma once

/**
* Lexer goes through the input buffer character per character and recognizes
* and returns meaningful tokens from them. For single character tokens token
* kind is equal to that char so multicharacter tokens start with value 256.
* Lexer also parser numeric and other literals so their value is ready to be
* used by parser. If Lexer is constructed with NULL buffer it will read from
* standard input which allows implementation of language console later on.
*/

#include <stdint.h>
#include <stdbool.h>

#include "smmmsgs.h"
#include "ibsallocator.h"
#include "ibsdictionary.h"

/********************************************************
Type Definitions
*********************************************************/

typedef enum { smmLexTypeFile, smmLexTypeStdIn } SmmLexTypeEnum;

// Each enum value above 255 should have coresponding string in smmlexer.c
typedef enum {
	tkSmmErr,
	tkSmmIdent = 256, // Because first 255 values are reserved for existing chars
	tkSmmIntDiv, tkSmmIntMod, tkSmmNot, tkSmmAndOp, tkSmmXorOp, tkSmmOrOp,
	tkSmmEq, tkSmmNotEq, tkSmmGtEq, tkSmmLtEq,
	tkSmmInt, tkSmmUInt, tkSmmFloat, tkSmmBool,
	tkSmmChar, tkSmmString,
	tkSmmRArrow, tkSmmReturn,
	tkSmmIf, tkSmmThen, tkSmmElse, tkSmmWhile, tkSmmDo,
	tkSmmEof
} SmmTokenKind;

typedef enum {
	soSmmLeaveWhitespace,
	soSmmCollapseIdent,
	soSmmCollapseWhitespace
} SmmStringParseOption;

typedef struct SmmToken* PSmmToken;

struct SmmLexer {
	char* buffer;
	char* curChar;
	uint64_t scanCount;
	PSmmToken lastToken;
	struct SmmFilePos filePos;
};
typedef struct SmmLexer* PSmmLexer;

struct SmmToken {
	uint32_t kind;
	uint32_t isFirstOnLine : 1;
	uint32_t canBeNewSymbol : 1;
	const char* repr;
	struct SmmFilePos filePos;
	union {
		char* stringVal;
		uint64_t uintVal;
		int64_t sintVal;
		double floatVal;
		bool boolVal;
		char charVal;
	};
};

/**
* Returns a new instance of SmmLexer that will scan the given buffer or stdin
* if given buffer is null. When scanning stdin end of file is signaled using
* "Enter, CTRL+Z, Enter" on Windows and CTRL+D on *nix systems
*/
PSmmLexer smmCreateLexer(char* buffer, const char* filename, PSmmMsgs msgs, PIbsAllocator a);

PSmmToken smmGetNextToken(PSmmLexer lex);
PSmmToken smmGetNextStringToken(PSmmLexer lex, char termChar, SmmStringParseOption option);

const char* smmTokenToString(PSmmToken token, char* buf);
