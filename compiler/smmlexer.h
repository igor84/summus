#pragma once

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SMM_STDIN_BUFFER_LENGTH 65536

/********************************************************
 Type Definitions
*********************************************************/

typedef enum { smmLexTypeFile, smmLexTypeStdIn } SmmLexTypeEnum;

typedef struct {
	int32_t lineNumber;
	int32_t lineOffset;
} SmmFilePos;

typedef struct SmmTmpLexer {
	char* buffer;
	char* curChar;
	uint64_t scanCount;

	SmmFilePos filePos;
} SmmLexer;
typedef SmmLexer* PSmmLexer;

typedef struct {
	SmmLexer lex;
	SmmLexTypeEnum lexType;
	void (*skipWhiteSpace)(const PSmmLexer);
} SmmPrivLexer;
typedef SmmPrivLexer PSmmPrivLexer;

/********************************************************
 "Private" Functions
*********************************************************/

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

/********************************************************
 API Functions
*********************************************************/

/**
	Returns a new instance of SmmLexer that will scan the given buffer or stdin
	if given buffer is null. When scanning stdin end of file is signaled using
	"Enter, CTRL+Z, Enter" on Windows and CTRL+D on *nix systems
*/
PSmmLexer smmInitLexer(char* buffer) {
	SmmPrivLexer* privLex = (SmmPrivLexer*) calloc(1, sizeof(SmmPrivLexer));

	if (!buffer) {
		buffer = (char *)calloc(SMM_STDIN_BUFFER_LENGTH, sizeof(char));
		fgets(buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
		privLex->skipWhiteSpace = smmSkipWhitespaceFromStdIn;
		privLex->lexType = smmLexTypeStdIn;
	} else {
		privLex->skipWhiteSpace = smmSkipWhitespaceFromBuffer;
	}
	privLex->lex.buffer = buffer;
	privLex->lex.curChar = buffer;
	privLex->lex.filePos.lineNumber = 1;
	privLex->lex.filePos.lineOffset = 1;
	assert(&privLex == &privLex->lex); // The code elseware assumes this so if it is not true this compiler is not supported
	return &privLex->lex;
}