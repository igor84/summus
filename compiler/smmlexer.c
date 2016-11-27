#include "ibscommon.h"
#include "smmlexer.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define STDIN_BUFFER_LENGTH 64 * 1024
#define MAX_HEX_DIGITS 16
#define MAX_OCTAL_DIGITS 21

#define MAX_MANTISSA_FOR_FAST_CONVERSION 0x20000000000000
#define MAX_EXP_FOR_FAST_CONVERSION 22

static const char* tokenTypeToString[] = {
	"identifier",
	"div", "mod", "not", "and", "or", "xor",
	"==", "!=", ">=", "<=",
	"int", "uint", "float", "bool",
	"->", "return",
	"eof"
};

static char decimalSeparator;

typedef enum {octalNumber = 3, hexNumber = 4} BinNumberKind;

/********************************************************
Type Definitions
*********************************************************/

struct Symbol {
	const char* name;
	int kind;
};
typedef struct Symbol* PSymbol;

struct PrivLexer {
	struct SmmLexer lex;
	PSmmMsgs msgs;
	PIbsAllocator a;
	PIbsAllocator syma; // Used for symTable allocations
	void(*skipWhitespace)(const PSmmLexer);
	PIbsDict symTable;
};
typedef struct PrivLexer* PPrivLexer;

/********************************************************
Private Functions
*********************************************************/

static char moveFor(const PSmmLexer lex, int move) {
	lex->filePos.lineOffset += move;
	lex->curChar += move;
	lex->scanCount += move;
	return *lex->curChar;
}

static char nextChar(const PSmmLexer lex) {
	return moveFor(lex, 1);
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
			fgets(lex->buffer, STDIN_BUFFER_LENGTH, stdin);
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

static void initSymTableWithKeywords(PPrivLexer lex) {
	static struct Symbol keywords[] = {
		{ "div", tkSmmIntDiv },{ "mod", tkSmmIntMod },{ "not", tkSmmNot },
		{ "and", tkSmmAndOp },{ "or", tkSmmOrOp },{ "xor", tkSmmXorOp },
		{ "return", tkSmmReturn },{ "false", tkSmmBool },{ "true", tkSmmBool },
	};

	int count = sizeof(keywords) / sizeof(struct Symbol);
	for (int i = 0; i < count; i++) {
		ibsDictPut(lex->symTable, keywords[i].name, &keywords[i]);
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

	char oldChar = *cc;
	*cc = 0;
	PSymbol symbol = ibsDictGet(privLex->symTable, ident);
	*cc = oldChar;
	if (!symbol) {
		symbol = ibsAlloc(privLex->a, sizeof(struct Symbol));
		symbol->kind = tkSmmIdent;
		char* name = ibsAlloc(privLex->a, i + 1);
		strncpy(name, ident, i);
		symbol->name = name;
		ibsDictPut(privLex->symTable, name, symbol);
	}

	token->kind = symbol->kind;
	if (token->kind == tkSmmBool) {
		token->boolVal = ident[0] == 't';
	}
	token->repr = symbol->name;
	return true;
}

static void parseBinNumber(PPrivLexer privLex, PSmmToken token, BinNumberKind bitsPerDigit) {
	PSmmLexer lex = &privLex->lex;
	int64_t res = 0;
	token->kind = tkSmmUInt;
	int digitsLeft = MAX_HEX_DIGITS;
	int maxDigit = '9';
	if (bitsPerDigit == octalNumber) {
		digitsLeft = lex->curChar[0] == '1' ? MAX_OCTAL_DIGITS + 1 : MAX_OCTAL_DIGITS;
		maxDigit = '7';
	}
	char cc = *lex->curChar;
	do {
		if (cc >= '0' && cc <= maxDigit) {
			res = (res << bitsPerDigit) + cc - '0';
		} else if (bitsPerDigit == octalNumber && isalnum(cc)) {
			smmPostMessage(privLex->msgs, errSmmInvalidDigit, lex->filePos, "octal");
			skipAlNum(lex);
			return;
		} else {
			cc |= 0x20; //to lowercase
			if (cc >= 'a' && cc <= 'f') {
				res = (res << 4) + cc - 'a' + 10;
			} else if (cc > 'f' && cc < 'z') {
				smmPostMessage(privLex->msgs, errSmmInvalidDigit, lex->filePos, "hex");
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
		smmPostMessage(privLex->msgs, errSmmIntTooBig, lex->filePos);
		skipAlNum(lex);
	} else {
		token->uintVal = res;
	}
}

static void parseNumber(PPrivLexer privLex, PSmmToken token) {
	PSmmLexer lex = &privLex->lex;
	token->kind = tkSmmUInt;
	int i = 0;
	int exp = 0;
	int esign = 1;
	int Eexp = 0;
	char* dot = NULL;
	while ('0' <= lex->curChar[i] && lex->curChar[i] <= '9') {
		i++;
	}
	int sigDigits = i;
	if (lex->curChar[i] == '.') {
		dot = &lex->curChar[i];
		token->kind = tkSmmFloat;
		i++;
		while ('0' <= lex->curChar[i] && lex->curChar[i] <= '9') {
			i++;
		}
		exp = sigDigits - i + 1;
		if (exp == 0) {
			// Non digit after dot
			smmPostMessage(privLex->msgs, errSmmInvalidNumber, lex->filePos);
			moveFor(lex, i);
			skipAlNum(lex);
			return;
		}
		sigDigits = i - 1;
	}
	if (lex->curChar[i] == 'e' || lex->curChar[i] == 'E') {
		token->kind = tkSmmFloat;
		i++;
		switch (lex->curChar[i]) {
		case '-': esign = -1; // fallthrough
		case '+': i++; break;
		}
		if (!('0' <= lex->curChar[i] && lex->curChar[i] <= '9')) {
			smmPostMessage(privLex->msgs, errSmmInvalidFloatExponent, lex->filePos);
			moveFor(lex, i);
			skipAlNum(lex);
			return;
		}
		// Exp should be short so we just parse it immediately
		int stopAt = i + 4;
		while ('0' <= lex->curChar[i] && lex->curChar[i] <= '9' && i < stopAt) {
			Eexp = Eexp * 10 + esign * (lex->curChar[i] - '0');
			i++;
		}
		while ('0' <= lex->curChar[i] && lex->curChar[i] <= '9') {
			// We are not interested in other digits because we already know exp is too long
			// for our fast algorithm and number will be parsed using strtod c function
			i++;
		}
		exp += Eexp;
	}

	uint64_t res = 0;
	char* pc = lex->curChar;
	moveFor(lex, i);
	if (token->kind == tkSmmUInt) {
		if (sigDigits > 20) {
			smmPostMessage(privLex->msgs, errSmmIntTooBig, lex->filePos);
			return;
		}
		while (pc < lex->curChar) {
			int d = *pc - '0';
			if (res > ((UINT64_MAX - d) / 10)) {
				smmPostMessage(privLex->msgs, errSmmIntTooBig, lex->filePos);
				return;
			}
			res = res * 10 + d;
			pc++;
		}
		token->uintVal = res;
		return;
	}

	// We are here if it is float
	int zerosToAdd = 15 - sigDigits;
	if (exp > 22 && zerosToAdd > 0) {
		exp -= zerosToAdd;
	} else {
		zerosToAdd = 0;
	}
	if (sigDigits > 16 || exp > MAX_EXP_FOR_FAST_CONVERSION || exp < -MAX_EXP_FOR_FAST_CONVERSION) {
		// We can't simply and correctly do the conversion so we fallback to strtod
		char* end = NULL;
		if (dot) *dot = decimalSeparator;
		token->floatVal = strtod(pc, &end);
		if (dot) *dot = '.';
		if (end != lex->curChar) {
			smmPostMessage(privLex->msgs, errSmmInvalidNumber, lex->filePos);
		}
		return;
	}
	while ('0' <= *pc && *pc <= '9') {
		res = res * 10 + (*pc - '0');
		pc++;
	}
	if (*pc == '.') {
		pc++;
		while ('0' <= *pc && *pc <= '9') {
			res = res * 10 + (*pc - '0');
			pc++;
		}
	}

	static uint64_t i10[16] = {
		1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
		10000000000, 100000000000, 1000000000000, 10000000000000, 100000000000000,
		1000000000000000
	};
	static double d10[MAX_EXP_FOR_FAST_CONVERSION + 1] = {
		1, 10, 100, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13,
		1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
	};

	res *= i10[zerosToAdd];
	if (res > MAX_MANTISSA_FOR_FAST_CONVERSION) {
		char* end = NULL;
		if (dot) *dot = decimalSeparator;
		token->floatVal = strtod(pc, &end);
		if (dot) *dot = '.';
		if (end != lex->curChar) {
			smmPostMessage(privLex->msgs, errSmmInvalidNumber, lex->filePos);
		}
		return;
	}

	bool multiply = exp >= 0;
	if (!multiply) {
		exp = -exp;
	}

	double dres = (double)res;
	if (multiply) {
		dres *= d10[exp];
	} else {
		dres /= d10[exp];
	}
	token->floatVal = dres;
}

static void parseZeroNumber(PPrivLexer privLex, PSmmToken token) {
	PSmmLexer lex = &privLex->lex;
	if (lex->curChar[1] == 'x') {
		moveFor(lex, 2);
		parseBinNumber(privLex, token, hexNumber);
	} else if (lex->curChar[1] == '.') {
		parseNumber(privLex, token);
	} else if ('1' <= lex->curChar[1] && lex->curChar[1] <= '7') {
		nextChar(lex);
		parseBinNumber(privLex, token, octalNumber);
	} else if (!isalnum(lex->curChar[1])) {
		// It is just 0
		token->kind = tkSmmUInt;
		nextChar(lex);
	} else {
		token->kind = tkSmmUInt;
		smmPostMessage(privLex->msgs, errSmmInvalid0Number, lex->filePos);
		skipAlNum(lex);
	}
}

static bool isUnaryOpOnNumber(PPrivLexer privLex) {
	char* cc = privLex->lex.curChar;
	while (*cc == '\t' || *cc == ' ' || *cc == '\v' || *cc == '\f') {
		cc++;
	}
	if (!isdigit(*cc)) return false;
	switch (privLex->lex.lastToken->kind) {
	case tkSmmBool: case tkSmmErr: case tkSmmFloat: case tkSmmIdent:
	case tkSmmInt: case tkSmmUInt: case ')':
		return false;
	default: return true;
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
PSmmLexer smmCreateLexer(char* buffer, const char* filename, PSmmMsgs msgs, PIbsAllocator a) {
	if (!decimalSeparator) {
		char* end = NULL;
		if (strtod("0,5", &end) == 0.5) decimalSeparator = ',';
		else decimalSeparator = '.';
	}

	PPrivLexer privLex = ibsAlloc(a, sizeof(struct PrivLexer));
	char symaName[1004] = { 'l', 'e', 'x' };
	strncpy(&symaName[3], filename, 1000);
	size_t symaSize = a->size >> 2;
	if (symaSize < 4 * 1024) symaSize = 4 * 1024;
	privLex->syma = ibsSimpleAllocatorCreate(symaName, symaSize);

	if (!buffer) {
		buffer = ibsAlloc(a, STDIN_BUFFER_LENGTH);
		fgets(buffer, STDIN_BUFFER_LENGTH, stdin);
		privLex->skipWhitespace = skipWhitespaceFromStdIn;
	} else {
		privLex->skipWhitespace = skipWhitespaceFromBuffer;
		privLex->lex.filePos.filename = filename;
	}
	privLex->msgs = msgs;
	privLex->a = a;
	privLex->lex.buffer = buffer;
	privLex->lex.curChar = buffer;
	privLex->lex.filePos.lineNumber = 1;
	privLex->lex.filePos.lineOffset = 1;
	privLex->symTable = ibsDictCreate(privLex->syma);
	initSymTableWithKeywords(privLex);
	return &privLex->lex;
}

PSmmToken smmGetNextToken(PSmmLexer lex) {
	PPrivLexer privLex = (PPrivLexer)lex;
	uint32_t lastLine = lex->filePos.lineNumber;
	privLex->skipWhitespace(lex);
	if (lex->curChar[0] == 0 && lex->lastToken->kind == tkSmmEof) {
		return lex->lastToken;
	}
	PIbsAllocator a = privLex->a;

	uint64_t pos = lex->scanCount;
	PSmmToken token = ibsAlloc(a, sizeof(struct SmmToken));
	token->filePos = lex->filePos;
	// It should be false for first token on first line, but true for first token on following lines
	token->isFirstOnLine = lastLine != lex->filePos.lineNumber;
	char* firstChar = lex->curChar;

	switch (*firstChar) {
	case 0:
		token->kind = tkSmmEof;
		ibsSimpleAllocatorFree(privLex->syma);
		privLex->syma = NULL;
		return token;
	case '-':
		nextChar(lex);
		if (lex->curChar[0] == '>') {
			token->kind = tkSmmRArrow;
			nextChar(lex);
		} else if (isUnaryOpOnNumber(privLex)) {
			privLex->skipWhitespace(lex);
			if (lex->curChar[0] == '0') {
				parseZeroNumber(privLex, token);
			} else {
				parseNumber(privLex, token);
			}
			if (token->kind == tkSmmUInt) {
				token->kind = tkSmmInt;
				if (token->uintVal > INT64_MAX) {
					smmPostMessage(privLex->msgs, errSmmIntTooBig, token->filePos);
				}
				token->sintVal = -(int64_t)token->uintVal;
			} else if (token->kind == tkSmmFloat) {
				token->floatVal = -token->floatVal;
			} else {
				assert(token->kind == tkSmmErr);
			}
		} else {
			token->kind = *firstChar;
		}
		break;
	case '=':
		nextChar(lex);
		if (lex->curChar[0] == '=') {
			token->kind = tkSmmEq;
			nextChar(lex);
		} else token->kind = *firstChar;
		break;
	case '!':
		nextChar(lex);
		if (lex->curChar[0] == '=') {
			token->kind = tkSmmNotEq;
			nextChar(lex);
		} else token->kind = *firstChar;
		break;
	case '>':
		nextChar(lex);
		if (lex->curChar[0] == '=') {
			token->kind = tkSmmGtEq;
			nextChar(lex);
		} else token->kind = *firstChar;
		break;
	case '<':
		nextChar(lex);
		if (lex->curChar[0] == '=') {
			token->kind = tkSmmLtEq;
			nextChar(lex);
		} else token->kind = *firstChar;
		break;
	case '+': case '*': case '/': case '%': case ':': case ';':
	case '(': case ')': case '{': case '}': case ',': case '.':
		token->kind = *firstChar;
		nextChar(lex);
		break;
	case '0':
		parseZeroNumber(privLex, token);
		break;
	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		parseNumber(privLex, token);
		break;
	default:
		if (isalpha(*firstChar)) {
			parseIdent(privLex, token);
		} else {
			smmPostMessage(privLex->msgs, errSmmInvalidCharacter, lex->filePos);
			nextChar(lex);
		}
		break;
	}

	if (!token->repr) {
		int cnt = (int)(lex->scanCount - pos);
		char* repr = ibsAlloc(privLex->a, cnt + 1);
		strncpy(repr, firstChar, cnt);
		token->repr = repr;
	}
	lex->lastToken = token;
	return token;
}

/**
* Given the token and 4 element buffer returns token's string representation.
* The buffer is needed in case token is a single character token so we can just
* put "<quote>char<quote><null>" into the buffer and return it.
*/
const char* smmTokenToString(PSmmToken token, char* buf) {
	if ((token->kind >= tkSmmInt && token->kind <= tkSmmBool) || token->kind == tkSmmErr) {
		return token->repr;
	}
	if (token->kind > 255) {
		return tokenTypeToString[token->kind - 256];
	}

	buf[2] = buf[0] = '\'';
	buf[1] = (char)token->kind;
	buf[3] = 0;
	return buf;
}
