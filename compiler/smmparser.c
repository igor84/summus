#include <stdint.h>
#include <stdio.h>

#include "smmutil.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"

// Disable warning that we are using anonimous unions in structs
#pragma warning(disable : 4201)

#define SMM_PARSER_IDENTS_DICT_SIZE 8 * 1024

/********************************************************
Type Definitions
*********************************************************/

// There should be one string corresponding to each value of
// SmmAstNodeKind enum from smmparser.h
static char* nodeKindToString[] = {
	"error", "Program", "=", "Ident", "", "", "", "",
	"+", "+.",
	"-", "-.",
	"*", "*.",
	"udiv", "sdiv", "/",
	"umod", "smod", "%",
	"-", "type", "uint", "float"
};

static struct SmmTypeInfo builtInTypes[] = {
	{ tiSmmUnknown, "/unknown/", 0},
	{ tiSmmInt8, "int8", 1, tifSmmInt }, { tiSmmInt16, "int16", 2, tifSmmInt },
	{ tiSmmInt32, "int32", 4, tifSmmInt }, { tiSmmInt64, "int64", 8, tifSmmInt },
	{ tiSmmUInt8, "uint8", tifSmmUnsignedInt }, { tiSmmUInt16, "uint16", 2, tifSmmUnsignedInt },
	{ tiSmmUInt32, "uint32", 4, tifSmmUnsignedInt }, { tiSmmUInt64, "uint64", 8, tifSmmUnsignedInt },
	{ tiSmmFloat32, "float32", 4, tifSmmFloat }, { tiSmmFloat64, "float64", 8, tifSmmFloat },
	{ tiSmmBool, "bool", 1 }
};

static PSmmTypeInfo tokenTypeToTypeInfo[] = {
	&builtInTypes[tiSmmUInt32],
	&builtInTypes[tiSmmUInt64],
	&builtInTypes[tiSmmFloat64],
	&builtInTypes[tiSmmBool]
};

static struct SmmAstNode errorNode = { nkSmmError, &builtInTypes[0] };

/********************************************************
Private Functions
*********************************************************/

#define newSmmAstNode() parser->allocator->alloc(parser->allocator, sizeof(struct SmmAstNode))

PSmmAstNode parseExpression(PSmmParser parser);

void getNextToken(PSmmParser parser) {
	parser->prevToken = parser->curToken;
	parser->curToken = smmGetNextToken(parser->lex);
}

bool findToken(PSmmParser parser, int tokenType) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenType && curKind != ';' && curKind != tkSmmEof) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	return curKind == tokenType;
}

PSmmToken expect(PSmmParser parser, int type) {
	PSmmToken token = parser->curToken;
	if (token->kind != type) {
		if (token->kind != tkSmmErr) {
			// If it is smmErr, lexer already reported the error
			char expBuf[4];
			char tmpRepr[2] = { (char)type, 0 };
			struct SmmToken tmpToken = {type};
			tmpToken.repr = tmpRepr;
			char* expected = smmTokenToString(&tmpToken, expBuf);
			struct SmmFilePos filePos;
			if (token->isFirstOnLine && parser->prevToken) {
				filePos = parser->prevToken->filePos;
			} else {
				filePos = token->filePos;
			}
			smmPostMessage(errSmmNoExpectedToken, parser->lex->fileName, filePos, expected);
			parser->lastErrorLine = filePos.lineNumber; //TODO(igors): Do I want to set this on all errors
		}
		return NULL;
	}
	getNextToken(parser);
	return token;
}

bool isNegFactor(PSmmParser parser) {
	bool doNeg = false;
	if (parser->curToken->kind == '-') {
		doNeg = true;
		getNextToken(parser);
	} else if (parser->curToken->kind == '+') {
		getNextToken(parser);
	}
	return doNeg;
}

PSmmAstNode parseFactor(PSmmParser parser) {
	PSmmAstNode res = &errorNode;
	bool doNeg = false;
	if (parser->curToken->kind == '(') {
		doNeg = isNegFactor(parser);
		getNextToken(parser);
		res = parseExpression(parser);
		if (!expect(parser, ')')) return &errorNode;
	} else {
		bool reportedError = parser->lastErrorLine == parser->curToken->filePos.lineNumber;
		SmmAstNodeKind kind;
		do {
			doNeg = doNeg || isNegFactor(parser);
			PSmmToken identToken = NULL;
			kind = nkSmmError;
			switch (parser->curToken->kind) {
			case tkSmmIdent: kind = nkSmmIdent; break;
			case tkSmmUInt32: case tkSmmUInt64: kind = nkSmmUInt; break;
			case tkSmmFloat64: kind = nkSmmFloat; break;
			default: break;
			}
			if (kind != nkSmmError) {
				if (kind != nkSmmIdent) {
					res = newSmmAstNode();
					res->kind = kind;
					res->type = tokenTypeToTypeInfo[parser->curToken->kind - tkSmmUInt32];
					res->token = parser->curToken;
				} else {
					PSmmAstNode var = (PSmmAstNode)smmGetDictValue(parser->idents, parser->curToken->repr, parser->curToken->hash, false);
					if (!var) {
						identToken = parser->curToken;
					} else if (var->kind != nkSmmIdent) {
						char* tokenStr = nodeKindToString[var->kind];
						smmPostMessage(errSmmIdentTaken, parser->lex->fileName, parser->curToken->filePos, parser->curToken->repr, tokenStr);
					} else if (!var->type) {
						res = var;
					} else {
						res = newSmmAstNode();
						*res = *var;
						res->token = parser->curToken;
					}
				}
			} else if (!reportedError && parser->curToken->kind != tkSmmErr) {
				char gotBuf[4];
				char* got = smmTokenToString(parser->curToken, gotBuf);
				smmPostMessage(errSmmGotUnexpectedToken, parser->lex->fileName, parser->curToken->filePos, "identifier or literal", got);
			}
			getNextToken(parser);
			if (identToken) {
				if (parser->curToken->kind == ':') {
					// This is declaration
					res = newSmmAstNode();
					res->kind = nkSmmIdent;
					res->token = identToken;
					smmAddDictValue(parser->idents, res->token->repr, res->token->hash, res);
				} else {
					smmPostMessage(errSmmUndefinedIdentifier, parser->lex->fileName, identToken->filePos, identToken->repr);
				}
			}
		} while (kind == nkSmmError && parser->curToken->kind != ';' && parser->curToken->kind != tkSmmEof);
	}

	if (doNeg) {
		PSmmAstNode neg = newSmmAstNode();
		neg->kind = nkSmmNeg;
		neg->left = res;
		neg->type = res->type;
		if ((neg->type->kind >= tiSmmUInt8) && (neg->type->kind < tiSmmUInt64)) {
			neg->type = &builtInTypes[neg->type->kind - tiSmmUInt8 + tiSmmInt16];
		} else if (neg->type->kind == tiSmmUInt64) {
			neg->type = &builtInTypes[tiSmmInt64];
		}
		res = neg;
	}
	return res;
}

PSmmAstNode parseTerm(PSmmParser parser) {
	PSmmAstNode term1 = parseFactor(parser);
	PSmmToken opToken = parser->curToken;
	while (opToken->kind == '*' || opToken->kind == '/' || opToken->kind == '%'
		|| opToken->kind == tkSmmIntDiv || opToken->kind == tkSmmIntMod) {
		getNextToken(parser);
		PSmmAstNode term2 = parseFactor(parser);
		PSmmAstNode res = newSmmAstNode();
		res->left = term1;
		res->right = term2;

		PSmmTypeInfo type = (term1->type->kind > term2->type->kind) ? term1->type : term2->type;
		PSmmTypeInfo ftype = type->kind < tiSmmFloat32 ? &builtInTypes[tiSmmFloat64] : type;
		switch (opToken->kind) {
		case '/':
			res->kind = nkSmmFDiv;
			res->type = ftype;
			break;
		case '%':
			res->kind = nkSmmFRem;
			res->type = ftype;
			break;
		case tkSmmIntDiv: case tkSmmIntMod:
			res->kind = ((term1->type->flags & term1->type->flags & tifSmmUnsignedInt) == tifSmmUnsignedInt) ? nkSmmUDiv : nkSmmSDiv;
			res->type = type;
			if (type->kind >= tiSmmFloat32) {
				char buf[4];
				smmPostMessage(errSmmBadOperandsType, parser->lex->fileName, opToken->filePos, smmTokenToString(opToken, buf), type->name);
			}
			if (opToken->kind == tkSmmIntMod) res->kind += nkSmmSRem - nkSmmSDiv;
			break;
		default:
			res->kind = nkSmmMul;
			res->type = type;
			break;
		}

		term1 = res;
		opToken = parser->curToken;
	};
	return term1;
}

PSmmAstNode parseExpression(PSmmParser parser) {
	PSmmAstNode term1 = parseTerm(parser);
	while (parser->curToken->kind == '-' || parser->curToken->kind == '+') {
		SmmAstNodeKind kind = nkSmmAdd;
		if (parser->curToken->kind == '-') {
			kind = nkSmmSub;
		}
		getNextToken(parser);
		PSmmAstNode term2 = parseTerm(parser);
		PSmmAstNode res = newSmmAstNode();
		if (((term1->type->flags | term2->type->flags) & tifSmmFloat) != 0) {
			res->kind = kind + 1;
		} else {
			res->kind = kind;
		}
		res->left = term1;
		res->right = term2;
		if (term1->type->kind >= term2->type->kind) {
			res->type = term1->type;
		} else {
			res->type = term2->type;
		}
		term1 = res;
	};
	return term1;
}

PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	expect(parser, '=');
	PSmmAstNode val = parseExpression(parser);
	if (lval == &errorNode || val == &errorNode) return &errorNode;
	if (!lval->type) {
		lval->type = val->type;
	}
	PSmmAstNode assignment = newSmmAstNode();
	assignment->kind = nkSmmAssignment;
	assignment->left = lval;
	assignment->right = val;
	assignment->type = lval->type;
	return assignment;
}

PSmmAstNode parseDeclaration(PSmmParser parser, PSmmAstNode lval) {
	expect(parser, ':');
	if (parser->curToken->kind == tkSmmIdent) {
		//Type is given in declaration so use it
		PSmmAstNode typeInfo = (PSmmAstNode)smmGetDictValue(parser->idents, parser->curToken->repr, parser->curToken->hash, false);
		if (!typeInfo || typeInfo->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->lex->fileName, parser->curToken->filePos, parser->curToken->repr);
		}
		else if (!lval->type) {
			lval->type = typeInfo->type;
		}
		getNextToken(parser);
	}
	if (parser->curToken->kind == '=') {
		return parseAssignment(parser, lval);
	}
	// Otherwise it should just be declaration so ';' is expected next
	return NULL;
}

PSmmAstNode parseStatement(PSmmParser parser) {
	PSmmAstNode lval;

	struct SmmFilePos fpos = parser->curToken->filePos;
	lval = parseExpression(parser);

	bool justCreatedLValIdent = (lval->kind == nkSmmIdent) && (lval->type == NULL);
	
	if (lval->kind != nkSmmIdent && (parser->curToken->kind == ':' || parser->curToken->kind == '=')) {
		smmPostMessage(errSmmOperandMustBeLVal, parser->lex->fileName, fpos);
		findToken(parser, ';');
		return &errorNode;
	}

	if (parser->curToken->kind == ':') {
		if ((lval->kind == nkSmmIdent) && (lval->type != NULL)) {
			smmPostMessage(errSmmRedefinition, parser->lex->fileName, lval->token->filePos, lval->token->repr);
		}
		return parseDeclaration(parser, lval);
	} else if (parser->curToken->kind == '=') {
		return parseAssignment(parser, lval);
	}
	else if (lval->kind == nkSmmIdent) {
		char* expected;
		if (justCreatedLValIdent) expected = ": or =";
		else expected = "=";
		smmPostMessage(errSmmGotUnexpectedToken, parser->lex->fileName, parser->curToken->filePos, expected, parser->curToken->repr);
		findToken(parser, ';');
		return &errorNode;
	}
	return lval;
}

void printNode(PSmmAstNode node) {
	fputs(nodeKindToString[node->kind], stdout);
	if (node->kind == nkSmmIdent) {
		fputs(":", stdout);
		fputs(node->type->name, stdout);
	}
	if (node->kind != nkSmmNeg) {
		fputs(" ", stdout);
	}
	if (node->left) printNode(node->left);
	if (node->right) printNode(node->right);
	if (node->next) {
		puts("");
		printNode(node->next);
	}
}

void initIdentsDict(PSmmParser parser) {
	parser->idents = smmCreateDict(parser->allocator, SMM_PARSER_IDENTS_DICT_SIZE, NULL, NULL);
	int cnt = sizeof(builtInTypes) / sizeof(struct SmmTypeInfo);
	for (int i = 0; i < cnt; i++) {
		PSmmAstNode typeNode = newSmmAstNode();
		typeNode->kind = nkSmmType;
		typeNode->type = &builtInTypes[i];
		smmAddDictValue(parser->idents, typeNode->type->name, smmHashString(typeNode->type->name), typeNode);
	}
}

/********************************************************
API Functions
*********************************************************/

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator) {
	PSmmParser parser = (PSmmParser)allocator->alloc(allocator, sizeof(struct SmmParser));
	parser->lex = lex;
	parser->curToken = smmGetNextToken(lex);
	parser->allocator = allocator;
	initIdentsDict(parser);
	return parser;
}


void smmParse(PSmmParser parser) {
	PSmmAstNode program = newSmmAstNode();
	program->kind = nkSmmProgram;
	PSmmAstNode lastStmt = program;
	
	while (parser->curToken->kind != tkSmmEof) {
		PSmmAstNode curStmt = parseStatement(parser);
		if (!expect(parser, ';')) curStmt = &errorNode;
		if (curStmt == NULL) continue;
		if (curStmt == &errorNode) {
			curStmt = newSmmAstNode();
			*curStmt = errorNode;
		}
		lastStmt->next = curStmt;
		lastStmt = curStmt;
	}

	printNode(program);
	puts("");
}