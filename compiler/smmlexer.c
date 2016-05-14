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
	res->type = ttSmmIdent;
	return res;
}

static void initSymTableWithKeywords(PPrivLexer lex) {
	static char* keywords[] = { "div", "and", "or", "xor" };
	static SmmTokenType keyTypes[] = { ttSmmIntDiv, ttSmmAndOp, ttSmmOrOp, ttSmmXorOp };
	int size = sizeof(keywords) / sizeof(char*);
	for (int i = 0; i < size; i++) {
		PSmmSymbol symbol = (PSmmSymbol)smmGetDictValue(lex->symTable, keywords[i], smmHashString(keywords[i]), true);
		symbol->type = keyTypes[i];
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

	token->type = symbol->type;
	token->repr = symbol->name;
	if (symbol->type == ttSmmIdent) {
		token->hash = hash;
	}
	return true;
}

static void parseHexNumber(PSmmLexer lex, PSmmToken token) {
	int64_t res = 0;
	int digitsLeft = 64 / 4;
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
				break;
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
		token->type = ttSmmInteger;
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
	do {
		if (cc >= '0' && cc <= '9') {
			int d = cc - '0';
			if (parseAsInt || part == smmExponent) {
				if (res > ((UINT64_MAX - d) / 10)) {
					if (parseAsInt) {
						parseAsInt = false;
						dres = res * 10.0 + d;
					} else {
						dres = INFINITY;
						do {
							cc = nextChar(lex);
						} while (isdigit(cc));
						break;
					}
				} else {
					res = res * 10 + d;
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
			if (parseAsInt) dres = res;
			res = 0;
			parseAsInt = false;
			part = smmFraction;
		} else if ((cc == 'e' || cc == 'E') && part != smmExponent) {
			part = smmExponent;
			if (parseAsInt) {
				dres = res;
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
		int e = expSign * res - exp;
		dres *= pow(10, e);
	}
	if (parseAsInt) {
		token->type = ttSmmInteger;
		token->intVal = res;
	} else if (part != smmMainInt) {
		token->type = ttSmmFloat;
		token->floatVal = dres;
	} else {
		token->type = ttSmmErr;
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
	privLex->skipWhitespace(lex);

	PSmmToken token = (PSmmToken)calloc(1, sizeof(struct SmmToken));
	token->filePos = lex->filePos;
	token->pos = lex->scanCount;
	char* cc = lex->curChar;

	switch (*cc)
	{
	case 0:
		token->type = ttSmmEof;
		return token;
	case '+': case '-': case '*': case '/': case '=': case ';': case '(': case ')':
		token->type = *cc;
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
		if (isalpha(*cc)) {
			parseIdent(privLex, token);
		} else {
			smmPostMessage(errSmmInvalidCharacter, lex->fileName, lex->filePos);
			nextChar(lex);
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