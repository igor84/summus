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
	"char", "string",
	"->", "return",
	"if", "then", "else", "while", "do",
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
	PIbsAllocator tmpa; // Used for symTable allocations
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
		{ "return", tkSmmReturn },{ "while", tkSmmWhile },{ "do", tkSmmDo },
		{ "if", tkSmmIf },{ "then", tkSmmThen },{ "else", tkSmmElse },
		{ "false", tkSmmBool },{ "true", tkSmmBool },
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

static uint64_t parseBinNumber(PPrivLexer privLex, BinNumberKind bitsPerDigit) {
	PSmmLexer lex = &privLex->lex;
	int64_t res = 0;
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
			return 0;
		} else {
			cc |= 0x20; //to lowercase
			if (cc >= 'a' && cc <= 'f') {
				res = (res << 4) + cc - 'a' + 10;
			} else if (cc > 'f' && cc < 'z') {
				smmPostMessage(privLex->msgs, errSmmInvalidDigit, lex->filePos, "hex");
				skipAlNum(lex);
				return 0;
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
		return 0;
	}
	return res;
}

static void parseNumber(PPrivLexer privLex, PSmmToken token) {
	PSmmLexer lex = &privLex->lex;
	token->kind = tkSmmUInt;
	int i = 0;
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
		if (i - sigDigits == 1) {
			// Non digit after dot
			smmPostMessage(privLex->msgs, errSmmInvalidNumber, lex->filePos);
			moveFor(lex, i);
			skipAlNum(lex);
			return;
		}
	}
	if (lex->curChar[i] == 'e' || lex->curChar[i] == 'E') {
		token->kind = tkSmmFloat;
		i++;
		if (lex->curChar[i] == '-' || lex->curChar[i] == '+') i++;
		if (!('0' <= lex->curChar[i] && lex->curChar[i] <= '9')) {
			smmPostMessage(privLex->msgs, errSmmInvalidFloatExponent, lex->filePos);
			moveFor(lex, i);
			skipAlNum(lex);
			return;
		}
		while ('0' <= lex->curChar[i] && lex->curChar[i] <= '9') {
			i++;
		}
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

	// We are here if it is float and we just use strtod since correctly parsing float is extremly complicated
	// (even strtod on some compilers isn't completely correct). For more info read:
	// http://www.exploringbinary.com/how-strtod-works-and-sometimes-doesnt/
	char* end = NULL;
	if (dot) *dot = decimalSeparator;
	token->floatVal = strtod(pc, &end);
	if (dot) *dot = '.';
	if (end != lex->curChar) {
		smmPostMessage(privLex->msgs, errSmmInvalidNumber, lex->filePos);
	}
}

static void parseZeroNumber(PPrivLexer privLex, PSmmToken token) {
	token->kind = tkSmmUInt;
	PSmmLexer lex = &privLex->lex;
	if (lex->curChar[1] == 'x') {
		moveFor(lex, 2);
		token->uintVal = parseBinNumber(privLex, hexNumber);
	} else if (lex->curChar[1] == '.') {
		parseNumber(privLex, token);
	} else if ('1' <= lex->curChar[1] && lex->curChar[1] <= '7') {
		nextChar(lex);
		token->uintVal = parseBinNumber(privLex, octalNumber);
	} else if (!isalnum(lex->curChar[1])) {
		// It is just 0
		nextChar(lex);
	} else {
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

static char* parseEscapeChar(PPrivLexer privLex, char* str) {
	PSmmLexer lex = &privLex->lex;
	switch (*lex->curChar) {
	case '\'': *str = '\''; break;
	case '"': *str = '"'; break;
	case '`': *str = '`'; break;
	case '\\': *str = '\\'; break;
	case 'a': *str = '\a'; break;
	case 'b': *str = '\b'; break;
	case 'f': *str = '\f'; break;
	case 'n': *str = '\n'; break;
	case 'r': *str = '\r'; break;
	case 't': *str = '\t'; break;
	case 'v': *str = '\v'; break;
	case 'x':
		if (!isxdigit(lex->curChar[1]) || !isxdigit(lex->curChar[2])) {
			*str = '?';
			smmPostMessage(privLex->msgs, errSmmBadStringEscape, lex->filePos);
		} else {
			nextChar(lex);
			char r = lex->curChar[0] < 'A' ? lex->curChar[0] - '0' : (lex->curChar[0] | 0x20) - 'a' + 10;
			r <<= 4;
			nextChar(lex);
			r |= lex->curChar[0] < 'A' ? lex->curChar[0] - '0' : (lex->curChar[0] | 0x20) - 'a' + 10;
			*str = r;
		}
		break;
	case '0': case '1': case '2': case '3': case '4': 
	case '5': case '6': case '7': case '8': case '9': 
		{
			uint16_t r = lex->curChar[0] - '0';
			if (isdigit(lex->curChar[1])) {
				nextChar(lex);
				r = r * 10 + lex->curChar[0] - '0';
				if (isdigit(lex->curChar[1]) && r <= (255 + '0' - lex->curChar[1]) / 10) {
					nextChar(lex);
					r = r * 10 + lex->curChar[0] - '0';
				}
			}
			*str = (char)r;
		}
		break;
	default:
		smmPostMessage(privLex->msgs, errSmmBadStringEscape, lex->filePos);
		*str = '?';
		break;
	}
	str++;
	return str;
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
	privLex->tmpa = ibsSimpleAllocatorCreate(symaName, a->size);

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
	privLex->symTable = ibsDictCreate(privLex->tmpa);
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
		ibsSimpleAllocatorFree(privLex->tmpa);
		privLex->tmpa = NULL;
		return token;
	case '-':
		nextChar(lex);
		if (lex->curChar[0] == '>') {
			token->kind = tkSmmRArrow;
			nextChar(lex);
		} else if (lex->curChar[0] == '"' || lex->curChar[0] == '\'' || lex->curChar[0] == '`') {
			token->kind = lex->curChar[0];
			token->sintVal = soSmmCollapseWhitespace;
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
				if (token->uintVal > 0x8000000000000000) {
					smmPostMessage(privLex->msgs, errSmmIntTooBig, token->filePos);
				}
				if (token->uintVal == 0x8000000000000000) token->sintVal = INT64_MIN;
				else token->sintVal = -(int64_t)token->uintVal;
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
	case '@':
		token->kind = tkSmmChar;
		nextChar(lex);
		if (lex->curChar[0] == '\\') {
			nextChar(lex);
			parseEscapeChar(privLex, &token->charVal);
		} else {
			token->charVal = lex->curChar[0];
		}
		nextChar(lex);
		break;
	case '+': case '*': case '/': case '%': case ':': case ';': case '.':
	case ',': case '(': case ')': case '{': case '}': case '[': case ']':
		token->kind = *firstChar;
		nextChar(lex);
		break;
	case '|':
		nextChar(lex);
		if (lex->curChar[0] == '"' || lex->curChar[0] == '\'' || lex->curChar[0] == '`') {
			token->kind = lex->curChar[0];
			token->sintVal = soSmmCollapseIdent;
			nextChar(lex);
		} else {
			smmPostMessage(privLex->msgs, errSmmInvalidCharacter, lex->filePos);
		}
		break;
	case '0':
		parseZeroNumber(privLex, token);
		break;
	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		parseNumber(privLex, token);
		break;
	case '"': case '\'': case '`':
		token->kind = lex->curChar[0];
		nextChar(lex);
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

PSmmToken smmGetNextStringToken(PSmmLexer lex, char termChar, SmmStringParseOption option) {
	PPrivLexer privLex = (PPrivLexer)lex;
	if (lex->curChar[0] == 0 && lex->lastToken->kind == tkSmmEof) {
		return lex->lastToken;
	}
	PIbsAllocator a = privLex->a;

	int identSize = -1;
	uint64_t pos = lex->scanCount;
	PSmmToken token = ibsAlloc(a, sizeof(struct SmmToken));
	token->filePos = lex->filePos;
	char* str = (char*)ibsStartAlloc(privLex->tmpa);
	token->stringVal = str;
	char* firstChar = lex->curChar;
	while (*lex->curChar && *lex->curChar != termChar) {
		if (*lex->curChar == '\\' && (termChar == '"' || lex->curChar[1] == termChar)) {
			nextChar(lex);
			str = parseEscapeChar(privLex, str);
		} else if (*lex->curChar == '\n' || *lex->curChar == '\r') {
			if (option == soSmmCollapseWhitespace) {
				if (str[-1] != ' ') {
					*str = ' ';
					str++;
				}
			} else if (str != token->stringVal || option != soSmmCollapseIdent) {
				*str = '\n';
				str++;
			}
			if (lex->curChar[0] + lex->curChar[1] == '\n' + '\r') {
				nextChar(lex);
			}
			lex->filePos.lineNumber++;
			lex->filePos.lineOffset = 0;
			if (option == soSmmCollapseIdent) {
				if (identSize == -1) {
					identSize = 0;
					while (lex->curChar[1] == ' ' || lex->curChar[1] == '\t') {
						nextChar(lex);
						identSize++;
					}
				} else {
					for (int i = identSize; i > 0 && (lex->curChar[1] == ' ' || lex->curChar[1] == '\t'); i--) {
						nextChar(lex);
					}
				}
			}
		} else if (option == soSmmCollapseWhitespace && isspace(*lex->curChar)) {
			if (str[-1] != ' ') {
				*str = ' ';
				str++;
			}
			while (isspace(lex->curChar[1])) {
				nextChar(lex);
			}
		} else {
			*str = *lex->curChar;
			str++;
		}
		nextChar(lex);
	}
	size_t length = str + 1 - token->stringVal;
	str = ibsAlloc(a, length);
	strncpy(str, token->stringVal, length);
	memset(token->stringVal, 0, length);
	token->stringVal = str;
	ibsEndAlloc(privLex->tmpa, 0);
	
	if (!*lex->curChar) {
		smmPostMessage(privLex->msgs, errSmmUnclosedString, token->filePos, token->filePos.lineNumber);
	}
	token->kind = tkSmmString;
	int cnt = (int)(lex->scanCount - pos);
	char* repr = ibsAlloc(privLex->a, cnt + 1);
	strncpy(repr, firstChar, cnt);
	token->repr = repr;
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
