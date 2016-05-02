#pragma once

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SMM_STDIN_BUFFER_LENGTH 65536
#define SMM_SYM_TABLE_SIZE 8096

/********************************************************
Type Definitions
*********************************************************/

typedef enum { smmLexTypeFile, smmLexTypeStdIn } SmmLexTypeEnum;

typedef enum {
	smmErr,
	smmIdent = 256, // Because first 255 values are reserved for existing chars
	smmInteger,
	smmIntDiv, smmAndOp, smmOrOp, smmXorOp,
	smmEof
} SmmTokenType;

typedef struct {
	int32_t lineNumber;
	int32_t lineOffset;
} SmmFilePos;

typedef struct {
	char* buffer;
	char* curChar;
	uint64_t scanCount;

	SmmFilePos filePos;
} SmmLexer;
typedef SmmLexer* PSmmLexer;

typedef struct {
	char* name;
	SmmTokenType symbolType;
} SmmSymbol;
typedef SmmSymbol* PSmmSymbol;

typedef struct SmmSymTableEntry *PSmmSymTableEntry;
struct SmmSymTableEntry {
	SmmSymbol symbol;
	PSmmSymTableEntry next;
};

typedef struct {
	SmmLexer lex;
	SmmLexTypeEnum lexType;
	void(*skipWhitespace)(const PSmmLexer);
	PSmmSymTableEntry symTable[SMM_SYM_TABLE_SIZE];
} SmmPrivLexer;
typedef SmmPrivLexer* PSmmPrivLexer;

typedef struct {
	SmmTokenType tokenType;
	int pos;
	SmmFilePos filePos;
	char* repr;
	union {
		uint64_t intVal;
		double floatVal;
		bool boolVal;
	};
} SmmToken;
typedef SmmToken* PSmmToken;;

/********************************************************
"Private" Functions
*********************************************************/

static uint32_t smmUpdateHash(uint32_t hash, char val) {
	uint32_t result = hash + val;
	result = result + (result << 10);
	return result ^ (result >> 6);
}

static uint32_t smmCompleteHash(uint32_t hash) {
	uint32_t result = hash + (hash << 3);
	result = result ^ (result >> 11);
	return result + (result << 15);
}

static char smmNextChar(const PSmmLexer lex) {
	lex->filePos.lineOffset++;
	lex->curChar++;
	lex->scanCount++;
	return *lex->curChar;
}

static void smmSkipWhitespaceFromBuffer(const PSmmLexer lex) {
	char cc = *lex->curChar;
	bool thereAreMoreWhites;
	do {
		thereAreMoreWhites = false;
		while (cc == '\t' || cc == ' ' || cc == '\v' || cc == '\f') {
			cc = smmNextChar(lex);
			thereAreMoreWhites = true;
		}
		if (cc == 0) return;
		if (cc == '\r' || cc == '\n') {
			if (cc + lex->curChar[1] == '\r' + '\n') smmNextChar(lex);
			cc = smmNextChar(lex);
			lex->filePos.lineNumber++;
			lex->filePos.lineOffset = 1;
			thereAreMoreWhites = true;
		}
	} while (thereAreMoreWhites);
}

static void smmSkipWhitespaceFromStdIn(const PSmmLexer lex) {
	char cc = *lex->curChar;
	bool thereMayBeMoreWhites;
	do {
		thereMayBeMoreWhites = false;
		while (isspace(cc)) {
			cc = smmNextChar(lex);
			thereMayBeMoreWhites = true;
		}
		if (cc == 0) {
			if (feof(stdin)) return;
			fgets(lex->buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
			lex->curChar = lex->buffer;
			cc = *lex->curChar;
			lex->filePos.lineNumber++;
			lex->filePos.lineOffset = 1;
			thereMayBeMoreWhites = true;
		}
	} while (thereMayBeMoreWhites);
}

static void smmSkipStatement(PSmmLexer lex) {
	while (*lex->curChar != ';' && *lex->curChar != 0) {
		smmNextChar(lex);
		if (isspace(*lex->curChar)) {
			((PSmmPrivLexer)lex)->skipWhitespace(lex);
		}
	}
}

static void smmReportError(PSmmLexer lex, char* msg) {
	printf("\nError: %s (at %d:%d)\n", msg, lex->filePos.lineNumber, lex->filePos.lineOffset);
}

static PSmmSymbol smmGetSymbol(PSmmPrivLexer lex, char* symName, uint32_t hash) {
	hash = hash & (SMM_SYM_TABLE_SIZE - 1);
	PSmmSymTableEntry result = lex->symTable[hash];
	PSmmSymTableEntry last = NULL;

	while (result) {
		if (strcmp(symName, result->symbol.name) == 0) {
			// Put the found element on start of the list so next search is faster
			if (last) {
				last->next = result->next;
				result->next = lex->symTable[hash];
				lex->symTable[hash] = result;
			}
			return &result->symbol;
		}
		last = result;
		result = result->next;
	}

	result = (PSmmSymTableEntry)calloc(1, sizeof(SmmSymTableEntry));
	int symNameLength = strlen(symName) + 1;
	result->symbol.name = (char*)malloc(symNameLength);
	strcpy(result->symbol.name, symName);
	result->symbol.symbolType = smmIdent;
	result->next = lex->symTable[hash];
	lex->symTable[hash] = result;

	return &result->symbol;
}

static PSmmSymbol smmGetSymbol(PSmmPrivLexer lex, char* symName) {
	uint32_t hash = 0;
	char* cc = symName;
	while (*cc != 0) {
		hash = smmUpdateHash(hash, *cc);
		cc++;
	}
	hash = smmCompleteHash(hash);
	return smmGetSymbol(lex, symName, hash);
}

static void smmInitSymTableWithKeywords(PSmmPrivLexer lex) {
	static char* keywords[] = { "div", "and", "or", "xor" };
	static SmmTokenType keyTypes[] = { smmIntDiv, smmAndOp, smmOrOp, smmXorOp };
	int size = sizeof(keywords) / sizeof(char*);
	for (int i = 0; i < size; i++) {
		PSmmSymbol symbol = smmGetSymbol(lex, keywords[i]);
		symbol->symbolType = keyTypes[i];
	}
}

static PSmmSymbol smmParseIdent(PSmmPrivLexer privLex) {
	char tmpString[1024];
	uint32_t hash = 0;
	char* cc = privLex->lex.curChar;
	int i = 0;
	do {
		hash = smmUpdateHash(hash, *cc);
		tmpString[i] = *cc;
		i++;
		cc++;
	} while (isalnum(*cc));
	tmpString[i] = 0;
	hash = smmCompleteHash(hash);
	privLex->lex.curChar = cc;
	privLex->lex.filePos.lineOffset += i;
	privLex->lex.scanCount += i;

	return smmGetSymbol(privLex, tmpString, hash);
}

static bool smmParseHexNumber(PSmmLexer lex, PSmmToken token) {
	int64_t res = 0;
	int digitsLeft = 64 / 4;
	do {
		char cc = *lex->curChar;
		if (cc >= '0' && cc <= '9') {
			res = (res << 4) + cc - '0';
		} else {
			cc |= 0x20; //to lowercase
			if (cc >= 'a' && cc <= 'f') {
				res = (res << 4) + cc - 'a' + 10;
			} else if (cc > 'f' && cc < 'z') {
				smmReportError(lex, "Invalid hexadecimal digit");
				smmSkipStatement(lex);
				return false;
			} else {
				break;
			}
		}
		smmNextChar(lex);
		digitsLeft--;
	} while (digitsLeft > 0);

	if (digitsLeft == 0 && isalnum(*lex->curChar)) {
		smmReportError(lex, "Integer literal too long");
		smmSkipStatement(lex);
		return false;
	} else {
		token->tokenType = smmInteger;
		token->intVal = res;
		return true;
	}
}

static bool smmParseNumber(PSmmLexer lex, PSmmToken token) {
	uint64_t res = 0;
	do {
		char cc = *lex->curChar;
		if (cc >= '0' && cc <= '9') {
			int d = cc - '0';
			if (res > ((UINT64_MAX - d) / 10)) {
				smmReportError(lex, "Integer literal too long");
				smmSkipStatement(lex);
				return false;
			}
			res = res * 10 + d;
		} else {
			break;
		}
		smmNextChar(lex);
	} while (true);

	token->tokenType = smmInteger;
	token->intVal = res;
	return true;
}

/********************************************************
API Functions
*********************************************************/

/**
Returns a new instance of SmmLexer that will scan the given buffer or stdin
if given buffer is null. When scanning stdin end of file is signaled using
"Enter, CTRL+Z, Enter" on Windows and CTRL+D on *nix systems
*/
PSmmLexer smmInitLexer(char* buffer) {
	SmmPrivLexer* privLex = (SmmPrivLexer*)calloc(1, sizeof(SmmPrivLexer));

	if (!buffer) {
		buffer = (char *)calloc(SMM_STDIN_BUFFER_LENGTH, sizeof(char));
		fgets(buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
		privLex->skipWhitespace = smmSkipWhitespaceFromStdIn;
		privLex->lexType = smmLexTypeStdIn;
	} else {
		privLex->skipWhitespace = smmSkipWhitespaceFromBuffer;
	}
	privLex->lex.buffer = buffer;
	privLex->lex.curChar = buffer;
	privLex->lex.filePos.lineNumber = 1;
	privLex->lex.filePos.lineOffset = 1;
	smmInitSymTableWithKeywords(privLex);
	// The code elsewhere assumes this so if it is not true this compiler is not supported
	assert(&privLex == &privLex->lex);
	return &privLex->lex;
}

PSmmToken smmGetNextToken(PSmmLexer lex) {
	PSmmPrivLexer privLex = (PSmmPrivLexer)lex;
	privLex->skipWhitespace(lex);

	PSmmToken token = (PSmmToken)calloc(1, sizeof(SmmToken));
	token->filePos = lex->filePos;
	token->pos = lex->scanCount;
	char* cc = lex->curChar;

	switch (*cc)
	{
	case 0:
		token->tokenType = smmEof;
		return token;
	case '+': case '-': case '*': case '/': case '=': case ';':
		token->tokenType = (SmmTokenType)*cc;
		smmNextChar(lex);
		break;
	case '0':
		smmNextChar(lex);
		if (*lex->curChar == 'x') {
			smmNextChar(lex);
			smmParseHexNumber(lex, token);
		} else {
			smmReportError(lex, "Only hexadecimal literals can start with 0");
			smmSkipStatement(lex);
		}
		break;
	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		smmParseNumber(lex, token);
		break;
	default:
		if (isalpha(*cc)) {
			PSmmSymbol symbol = smmParseIdent(privLex);
			token->tokenType = symbol->symbolType;
			token->repr = symbol->name;
		} else {
			smmReportError(lex, "Invalid character");
			smmSkipStatement(lex);
		}
		break;
	}

	if (!token->repr) {
		int cnt = lex->scanCount - token->pos + 1;
		token->repr = (char*)malloc(cnt);
		strncpy(token->repr, cc, cnt - 1);
		token->repr[cnt - 1] = 0;
	}
	return token;
}