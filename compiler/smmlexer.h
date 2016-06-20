#ifndef SMM_LEXER_H
#define SMM_LEXER_H

/**
 * Lexer goes through the input buffer character per character and recognizes
 * and returns meaningful tokens from them. For single character tokens token
 * kind is equal to that char so multicharacter tokens start with value 256.
 * Lexer also parser numeric and other literals so their value is ready to be
 * used by parser. If Lexer is constructed with NULL buffer it will read from
 * standard input which allows implementation of language console later on.
 */

#include "smmcommon.h"
#include <stdint.h>
#include <stdbool.h>

#include "smmmsgs.h"
#include "smmutil.h"

/********************************************************
Type Definitions
*********************************************************/

typedef enum { smmLexTypeFile, smmLexTypeStdIn } SmmLexTypeEnum;

typedef enum {
	tkSmmErr,
	tkSmmIdent = 256, // Because first 255 values are reserved for existing chars
	tkSmmIntDiv, tkSmmIntMod, tkSmmAndOp, tkSmmXorOp, tkSmmOrOp,
	tkSmmInt, tkSmmUInt, tkSmmFloat, tkSmmBool,
	tkSmmEof
} SmmTokenKind;

struct SmmLexer {
	char* buffer;
	char* curChar;
	uint64_t scanCount;

	struct SmmFilePos filePos;
};
typedef struct SmmLexer* PSmmLexer;

struct SmmSymbol {
	const char* name;
	int kind;
};
typedef struct SmmSymbol* PSmmSymbol;

struct SmmToken {
	int kind;
	const char* repr;
	bool isFirstOnLine;
	struct SmmFilePos filePos;
	union {
		uint64_t uintVal;
		int64_t sintVal;
		double floatVal;
		bool boolVal;
	};
};
typedef struct SmmToken* PSmmToken;

/**
 * Returns a new instance of SmmLexer that will scan the given buffer or stdin
 * if given buffer is null. When scanning stdin end of file is signaled using
 * "Enter, CTRL+Z, Enter" on Windows and CTRL+D on *nix systems
 */
PSmmLexer smmCreateLexer(char* buffer, const char* filename, PSmmAllocator allocator);

PSmmToken smmGetNextToken(PSmmLexer lex);

const char* smmTokenToString(PSmmToken token, char* buf);

#endif
