#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "smmutil.h"
#include "smmmsgs.h"
#include "smmlexer.h"

#define SMM_STDIN_BUFFER_LENGTH 64 * 1024
#define SMM_LEXER_DICT_SIZE 8 * 1024
#define SMM_MAX_HEX_DIGITS 16

static char* tokenTypeToString[] = {
	"identifier",
	"div", "mod", "and", "or", "xor",
	"uint32", "uint64", "float64", "bool",
	"eof"
};

/********************************************************
Type Definitions
*********************************************************/

struct PrivLexer {
	struct SmmLexer lex;
	SmmLexTypeEnum lexType;
	PSmmAllocator allocator;
	void(*skipWhitespace)(const PSmmLexer);
	PSmmDict symTable;
};
typedef struct PrivLexer* PPrivLexer;

/********************************************************
Private Functions
*********************************************************/

static char nextChar(const PSmmLexer lex) {
	lex->filePos.lineOffset++;
	lex->curChar++;
	lex->scanCount++;
	return *lex->curChar;
}

static void skipWhitespaceFromBuffer(const PSmmLexer lex) {
	char cc = *lex->curChar;
	bool thereMayBeMoreWhites;
	do {
		thereMayBeMoreWhites = false;
		while (cc == '\t' || cc == ' ' || cc == '\v' || cc == '\f') {
			cc = nextChar(lex);
			thereMayBeMoreWhites = true;
		}
		if (cc == 0) return;
		if (cc == '\r' || cc == '\n') {
			if (cc + lex->curChar[1] == '\r' + '\n') nextChar(lex);
			cc = nextChar(lex);
			lex->filePos.lineNumber++;
			lex->filePos.lineOffset = 1;
			thereMayBeMoreWhites = true;
		}
	} while (thereMayBeMoreWhites);
}

static void skipWhitespaceFromStdIn(const PSmmLexer lex) {
	char cc = *lex->curChar;
	bool thereMayBeMoreWhites;
	do {
		thereMayBeMoreWhites = false;
		while (isspace(cc)) {
			cc = nextChar(lex);
			thereMayBeMoreWhites = true;
		}
		if (cc == 0) {
			if (feof(stdin)) return;
			lex->buffer[0] = 0;
			fgets(lex->buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
			lex->curChar = lex->buffer;
			cc = *lex->curChar;
			lex->filePos.lineNumber++;
			lex->filePos.lineOffset = 1;
			thereMayBeMoreWhites = true;
		}
	} while (thereMayBeMoreWhites);
}

static void skipAlNum(PSmmLexer lex) {
	char cc;
	do {
		cc = nextChar(lex);
	} while (isalnum(cc));
}

static void* createSymbolElem(char* key, PSmmAllocator a, void* context) {
	PSmmSymbol res = (PSmmSymbol)a->alloc(a, sizeof(struct SmmSymbol));
	res->name = key;
	res->kind = tkSmmIdent;
	return res;
}

static void initSymTableWithKeywords(PPrivLexer lex) {
	static char* keywords[] = {
		"div", "mod", "and", "or", "xor"
	};
	static SmmTokenKind keyKinds[] = {
		tkSmmIntDiv, tkSmmIntMod, tkSmmAndOp, tkSmmOrOp, tkSmmXorOp
	};
	int count = sizeof(keywords) / sizeof(char*);
	// Assert that both arrays have the same size
	assert((sizeof(keyKinds) / sizeof(keyKinds[0])) == count);
	for (int i = 0; i < count; i++) {
		PSmmSymbol symbol = (PSmmSymbol)smmGetDictValue(lex->symTable, keywords[i], smmHashString(keywords[i]), true);
		symbol->kind = keyKinds[i];
	}
}

static bool parseIdent(PPrivLexer privLex, PSmmToken token) {
	uint32_t hash = 0;
	char* cc = privLex->lex.curChar;
	char* ident = cc;
	int i = 0;
	do {
		hash = smmUpdateHash(hash, *cc);
		i++;
		cc++;
	} while (isalnum(*cc));
	hash = smmCompleteHash(hash);
	privLex->lex.curChar = cc;
	privLex->lex.filePos.lineOffset += i;
	privLex->lex.scanCount += i;

	char old = *cc;
	*cc = 0;
	PSmmSymbol symbol = smmGetDictValue(privLex->symTable, ident, hash, true);
	*cc = old;

	token->kind = symbol->kind;
	token->repr = symbol->name;
	if (symbol->kind == tkSmmIdent) {
		token->hash = hash;
	}
	return true;
}

static void parseHexNumber(PSmmLexer lex, PSmmToken token) {
	int64_t res = 0;
	int digitsLeft = SMM_MAX_HEX_DIGITS;
	char cc = *lex->curChar;
	do {
		if (cc >= '0' && cc <= '9') {
			res = (res << 4) + cc - '0';
		} else {
			cc |= 0x20; //to lowercase
			if (cc >= 'a' && cc <= 'f') {
				res = (res << 4) + cc - 'a' + 10;
			} else if (cc > 'f' && cc < 'z') {
				smmPostMessage(errSmmInvalidHexDigit, lex->fileName, lex->filePos);
				skipAlNum(lex);
				return;
			} else {
				break;
			}
		}
		cc = nextChar(lex);
		digitsLeft--;
	} while (digitsLeft > 0);

	if (digitsLeft == 0 && isalnum(*lex->curChar)) {
		smmPostMessage(errSmmIntTooBig, lex->fileName, lex->filePos);
		skipAlNum(lex);
	} else {
		if (digitsLeft < 8) token->kind = tkSmmUInt64;
		else token->kind = tkSmmUInt32;
		token->intVal = res;
	}
}

static void parseNumber(PSmmLexer lex, PSmmToken token) {
	uint64_t res = 0;
	bool parseAsInt = true;
	enum {smmMainInt, smmFraction, smmExponent} part = smmMainInt;
	double dres = 0;
	int exp = 0;
	int expSign = 1;
	char cc = *lex->curChar;
	token->kind = tkSmmUInt32;
	do {
		if (cc >= '0' && cc <= '9') {
			int d = cc - '0';
			if (parseAsInt || part == smmExponent) {
				if (res > ((UINT64_MAX - d) / 10)) {
					if (parseAsInt) {
						parseAsInt = false;
						dres = res * 10.0 + d;
					} else {
						do {
							cc = nextChar(lex);
						} while (isdigit(cc));
						break;
					}
				} else {
					res = res * 10 + d;
					if (res > UINT32_MAX) {
						token->kind = tkSmmUInt64;
					}
				}
			} else if (part == smmMainInt) {
				dres = dres * 10 + d;
			} else if (part == smmFraction) {
				exp++;
				dres = dres * 10 + d;
			}
		} else if (cc == '.' && part == smmMainInt) {
			if (!isdigit(lex->curChar[1])) {
				smmPostMessage(errSmmInvalidNumber, lex->fileName, lex->filePos);
				nextChar(lex);
				skipAlNum(lex);
				break;
			}
			if (parseAsInt) dres = (double)res;
			res = 0;
			parseAsInt = false;
			part = smmFraction;
		} else if ((cc == 'e' || cc == 'E') && part != smmExponent) {
			part = smmExponent;
			if (parseAsInt) {
				dres = (double)res;
				parseAsInt = false;
			}
			res = 0;
			if (lex->curChar[1] == '-' || lex->curChar[1] == '+') {
				expSign = '+' - lex->curChar[1] + 1; // 1 or -1
				nextChar(lex);
			}
			if (!isdigit(lex->curChar[1])) {
				smmPostMessage(errSmmInvalidFloatExponent, lex->fileName, lex->filePos);
				nextChar(lex);
				skipAlNum(lex);
				break;
			}
		} else {
			break;
		}
		cc = nextChar(lex);
	} while (true);

	if (part != smmMainInt) {
		double e = expSign * (double)res - exp;
		dres *= pow(10, e);
	}
	if (parseAsInt) {
		token->intVal = res;
	} else if (part != smmMainInt) {
		token->kind = tkSmmFloat64;
		token->floatVal = dres;
	} else {
		token->kind = tkSmmErr;
		smmPostMessage(errSmmIntTooBig, lex->fileName, lex->filePos);
	}
}

/********************************************************
API Functions
*********************************************************/

/**
Returns a new instance of SmmLexer that will scan the given buffer or stdin
if given buffer is null. When scanning stdin end of file is signaled using
"Enter - CTRL+Z - Enter" on Windows and CTRL+D on *nix systems
*/
PSmmLexer smmCreateLexer(char* buffer, char* fileName, PSmmAllocator allocator) {
	PPrivLexer privLex = (PPrivLexer)allocator->alloc(allocator, sizeof(struct PrivLexer));

	if (!buffer) {
		buffer = (char *)allocator->alloc(allocator, SMM_STDIN_BUFFER_LENGTH);
		fgets(buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
		privLex->skipWhitespace = skipWhitespaceFromStdIn;
		privLex->lexType = smmLexTypeStdIn;
	} else {
		privLex->skipWhitespace = skipWhitespaceFromBuffer;
		privLex->lex.fileName = fileName;
	}
	privLex->allocator = allocator;
	privLex->lex.buffer = buffer;
	privLex->lex.curChar = buffer;
	privLex->lex.filePos.lineNumber = 1;
	privLex->lex.filePos.lineOffset = 1;
	privLex->symTable = smmCreateDict(allocator, SMM_LEXER_DICT_SIZE, privLex, createSymbolElem);
	initSymTableWithKeywords(privLex);
	return &privLex->lex;
}

PSmmToken smmGetNextToken(PSmmLexer lex) {
	PPrivLexer privLex = (PPrivLexer)lex;
	int lastLine = lex->filePos.lineNumber;
	privLex->skipWhitespace(lex);

	PSmmToken token = (PSmmToken)calloc(1, sizeof(struct SmmToken));
	token->filePos = lex->filePos;
	token->pos = lex->scanCount;
	token->isFirstOnLine = lastLine != lex->filePos.lineNumber;
	char* firstChar = lex->curChar;

	switch (*firstChar) {
	case 0:
		token->kind = tkSmmEof;
		return token;
	case '+': case '-': case '*': case '/': case '%': case '=':
	case ':': case ';': case '(': case ')':
		token->kind = *firstChar;
		nextChar(lex);
		break;
	case '0':
		if (lex->curChar[1] == 'x') {
			nextChar(lex);
			nextChar(lex);
			parseHexNumber(lex, token);
		} else if (lex->curChar[1] == '.') {
			parseNumber(lex, token);
		} else {
			smmPostMessage(errSmmInvalid0Number, lex->fileName, lex->filePos);
			skipAlNum(lex);
		}
		break;
	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		parseNumber(lex, token);
		break;
	default:
		if (isalpha(*firstChar)) {
			parseIdent(privLex, token);
		} else {
			smmPostMessage(errSmmInvalidCharacter, lex->fileName, lex->filePos);
			nextChar(lex);
		}
		break;
	}

	if (!token->repr) {
		int cnt = (int)(lex->scanCount - token->pos);
		token->repr = (char*)privLex->allocator->alloc(privLex->allocator, cnt + 1);
		strncpy(token->repr, firstChar, cnt);
	}
	return token;
}

char* smmTokenToString(PSmmToken token, char* buf) {
	if (token->kind > 255) return tokenTypeToString[token->kind - 256];
	if (token->kind == tkSmmErr) return token->repr;

	buf[2] = buf[0] = '\'';
	buf[1] = (char)token->kind;
	buf[3] = 0;
	return buf;
}