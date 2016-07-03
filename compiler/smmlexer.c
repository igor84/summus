#include "smmlexer.h"
#include "smmutil.h"
#include "smmmsgs.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define SMM_STDIN_BUFFER_LENGTH 64 * 1024
#define SMM_MAX_HEX_DIGITS 16

static const char* tokenTypeToString[] = {
	"identifier",
	"div", "mod", "and", "or", "xor",
	"uint32", "uint64", "float64", "bool",
	"->", "return",
	"eof"
};

/********************************************************
Type Definitions
*********************************************************/

struct PrivLexer {
	struct SmmLexer lex;
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
		if (cc == '/' && lex->curChar[1] == '/') {
			while (cc != 0 && cc != '\n') {
				cc = nextChar(lex);
				thereMayBeMoreWhites = true;
			}
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
		if (cc == '/' && lex->curChar[1] == '/') {
			while (cc != 0 && cc != '\n') {
				cc = nextChar(lex);
				thereMayBeMoreWhites = true;
			}
		}
		if (cc == 0) {
			if (feof(stdin)) return;
			lex->buffer[0] = 0; // We set first character to null in case fgets just reads eof
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

static void* createSymbolElem(const char* key, PSmmAllocator a, void* context) {
	PSmmSymbol res = a->alloc(a, sizeof(struct SmmSymbol));
	res->name = key;
	res->kind = tkSmmIdent; // By default it is ident but it can be changed later
	return res;
}

static void initSymTableWithKeywords(PPrivLexer lex) {
	struct Keywords {
		const char* name;
		int kind;
	};
	static struct Keywords keywords[] = {
		{ "div", tkSmmIntDiv }, { "mod", tkSmmIntMod },
		{ "and", tkSmmAndOp }, { "or", tkSmmOrOp }, { "xor", tkSmmXorOp },
		{ "return", tkSmmReturn }
	};
	
	int count = sizeof(keywords) / sizeof(struct Keywords);
	for (int i = 0; i < count; i++) {
		struct Keywords k = keywords[i];
		PSmmSymbol symbol = smmGetDictValue(lex->symTable, k.name, true);
		symbol->kind = k.kind;
	}
}

static bool parseIdent(PPrivLexer privLex, PSmmToken token) {
	char* cc = privLex->lex.curChar;
	char* ident = cc;
	int i = 0;
	do {
		i++;
	} while (isalnum(cc[i]));
	cc += i;
	privLex->lex.curChar = cc;
	privLex->lex.filePos.lineOffset += i;
	privLex->lex.scanCount += i;

	char old = *cc;
	*cc = 0; // Temporarily set the end of string here so we can just use ident pointer bellow
	PSmmSymbol symbol = smmGetDictValue(privLex->symTable, ident, true);
	*cc = old;

	token->kind = symbol->kind;
	token->repr = symbol->name;
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
				smmPostMessage(errSmmInvalidHexDigit, lex->filePos);
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
		smmPostMessage(errSmmIntTooBig, lex->filePos);
		skipAlNum(lex);
	} else {
		token->kind = tkSmmUInt;
		token->uintVal = res;
	}
}

static void parseNumber(PSmmLexer lex, PSmmToken token) {
	uint64_t res = 0;
	bool parseAsInt = true;
	enum {smmMainInt, smmFraction, smmExponent} part = smmMainInt;
	double dres = 0;
	int exp = 0; // Exponent for decimals, not the ones after E in 12.43E+12
	int expSign = 1;
	char cc = *lex->curChar;
	token->kind = tkSmmUInt;
	do {
		if (cc >= '0' && cc <= '9') {
			int d = cc - '0';
			if (parseAsInt || part == smmExponent) {
				// If we are parsing int or exponent first check if we are about to overflow
				if (res <= ((UINT64_MAX - d) / 10)) {
					res = res * 10 + d;
				} else {
					if (parseAsInt) {
						// Since the number can't fit in the largest int we assume it is double
						parseAsInt = false;
						dres = res * 10.0 + d;
					} else {
						// If we get here we are parsing exponent after E and it is too big
						// so we just skip remaining digits and later return error
						do {
							cc = nextChar(lex);
						} while (isdigit(cc));
						break;
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
				smmPostMessage(errSmmInvalidNumber, lex->filePos);
				nextChar(lex);
				skipAlNum(lex);
				break;
			}
			// If int part was too big for int then we already switched to using dres
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
				smmPostMessage(errSmmInvalidFloatExponent, lex->filePos);
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
		// This combination of operations is the only one that passes all the tests
		dres *= pow(10, -exp);
		dres /= pow(10, -expSign * (double)res);
	}
	if (parseAsInt) {
		token->uintVal = res;
	} else if (part != smmMainInt) {
		token->kind = tkSmmFloat;
		token->floatVal = dres;
	} else {
		token->kind = tkSmmErr;
		smmPostMessage(errSmmIntTooBig, lex->filePos);
	}
}

/********************************************************
API Functions
*********************************************************/

/**
 * Returns a new instance of SmmLexer that will scan the given buffer or stdin
 * if given buffer is null. When scanning stdin end of file is signaled using
 * "Enter - CTRL+Z - Enter" on Windows and CTRL+D on *nix systems
 */
PSmmLexer smmCreateLexer(char* buffer, const char* filename, PSmmAllocator allocator) {
	PPrivLexer privLex = allocator->alloc(allocator, sizeof(struct PrivLexer));

	if (!buffer) {
		buffer = allocator->alloc(allocator, SMM_STDIN_BUFFER_LENGTH);
		fgets(buffer, SMM_STDIN_BUFFER_LENGTH, stdin);
		privLex->skipWhitespace = skipWhitespaceFromStdIn;
	} else {
		privLex->skipWhitespace = skipWhitespaceFromBuffer;
		privLex->lex.filePos.filename = filename;
	}
	privLex->allocator = allocator;
	privLex->lex.buffer = buffer;
	privLex->lex.curChar = buffer;
	privLex->lex.filePos.lineNumber = 1;
	privLex->lex.filePos.lineOffset = 1;
	privLex->symTable = smmCreateDict(allocator, privLex, createSymbolElem);
	initSymTableWithKeywords(privLex);
	return &privLex->lex;
}

PSmmToken smmGetNextToken(PSmmLexer lex) {
	PPrivLexer privLex = (PPrivLexer)lex;
	int lastLine = lex->filePos.lineNumber;
	privLex->skipWhitespace(lex);
	PSmmAllocator a = privLex->allocator;

	uint64_t pos = lex->scanCount;
	PSmmToken token = a->alloc(a, sizeof(struct SmmToken));
	token->filePos = lex->filePos;
	// It should be false for first token on first line, but true for first token on following lines
	token->isFirstOnLine = lastLine != lex->filePos.lineNumber;
	char* firstChar = lex->curChar;

	switch (*firstChar) {
	case 0:
		token->kind = tkSmmEof;
		return token;
	case '-':
		token->kind = *firstChar;
		nextChar(lex);
		if (lex->curChar[0] == '>') {
			token->kind = tkSmmRArrow;
			nextChar(lex);
		}
		break;
	case '+': case '*': case '/': case '%': case '=': case ':':
	case ';': case '(': case ')': case '{': case '}': case ',':
		token->kind = *firstChar;
		nextChar(lex);
		break;
	case '0':
		if (lex->curChar[1] == 'x') {
			nextChar(lex);
			nextChar(lex);
			parseHexNumber(lex, token);
		} else if (!isalnum(lex->curChar[1])) {
			parseNumber(lex, token);
		} else {
			smmPostMessage(errSmmInvalid0Number, lex->filePos);
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
			smmPostMessage(errSmmInvalidCharacter, lex->filePos);
			nextChar(lex);
		}
		break;
	}

	if (!token->repr) {
		int cnt = (int)(lex->scanCount - pos);
		char* repr = privLex->allocator->alloc(privLex->allocator, cnt + 1);
		strncpy(repr, firstChar, cnt);
		token->repr = repr;
	}
	return token;
}

/**
 * Given the token and 4 element buffer returns token's string representation.
 * The buffer is needed in case token is a single character token so we can just
 * put "<quote>char<quote><null>" into the buffer and return it.
 */
const char* smmTokenToString(PSmmToken token, char* buf) {
	if (token->kind > 255) return tokenTypeToString[token->kind - 256];
	if (token->kind == tkSmmErr) return token->repr;

	buf[2] = buf[0] = '\'';
	buf[1] = (char)token->kind;
	buf[3] = 0;
	return buf;
}
