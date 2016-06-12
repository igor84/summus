#include "smmutil.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#define SMM_PARSER_IDENTS_DICT_SIZE 8 * 1024

/********************************************************
Type Definitions
*********************************************************/

// There should be one string corresponding to each value of SmmAstNodeKind enum
const char* nodeKindToString[] = {
	"error", "Program", ":", "=", "Ident",
	"+", "+.",
	"-", "-.",
	"*", "*.",
	"udiv", "sdiv", "/",
	"umod", "smod", "%",
	"-", "type", "int", "float",
	"cast"
};

static struct SmmTypeInfo builtInTypes[] = {
	{ tiSmmUnknown, "/unknown/", 0},
	{ tiSmmUInt8, "uint8", tifSmmUnsignedInt }, { tiSmmUInt16, "uint16", 2, tifSmmUnsignedInt },
	{ tiSmmUInt32, "uint32", 4, tifSmmUnsignedInt }, { tiSmmUInt64, "uint64", 8, tifSmmUnsignedInt },
	{ tiSmmInt8, "int8", 1, tifSmmInt },{ tiSmmInt16, "int16", 2, tifSmmInt },
	{ tiSmmInt32, "int32", 4, tifSmmInt },{ tiSmmInt64, "int64", 8, tifSmmInt },
	{ tiSmmFloat32, "float32", 4, tifSmmFloat }, { tiSmmFloat64, "float64", 8, tifSmmFloat },
	{ tiSmmSoftFloat64, "/sfloat64/", 8, tifSmmFloat }, { tiSmmBool, "bool", 1 }
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
			const char* expected = smmTokenToString(&tmpToken, expBuf);
			struct SmmFilePos filePos;
			if (token->isFirstOnLine && parser->prevToken) {
				filePos = parser->prevToken->filePos;
			} else {
				filePos = token->filePos;
			}
			smmPostMessage(errSmmNoExpectedToken, filePos, expected);
			parser->lastErrorLine = filePos.lineNumber; //TODO(igors): Do I want to set this on all errors
		}
		return NULL;
	}
	getNextToken(parser);
	return token;
}

PSmmTypeInfo getLiteralTokenType(PSmmToken token) {
	switch (token->kind) {
	case tkSmmBool: return &builtInTypes[tiSmmBool];
	case tkSmmFloat: return &builtInTypes[tiSmmSoftFloat64];
	case tkSmmUInt:
		if (token->uintVal <= INT8_MAX) return &builtInTypes[tiSmmInt8];
		if (token->uintVal <= INT16_MAX) return &builtInTypes[tiSmmInt16];
		if (token->uintVal <= INT32_MAX) return &builtInTypes[tiSmmInt32];
		if (token->uintVal <= INT64_MAX) return &builtInTypes[tiSmmInt64];
		return &builtInTypes[tiSmmUInt64];
	default:
		// Should never happen
		assert(false && "Got literal of unknown kind!");
		return &builtInTypes[tiSmmUnknown];
	}
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
			case tkSmmUInt: kind = nkSmmInt; break;
			case tkSmmFloat: kind = nkSmmFloat; break;
			default: break;
			}
			if (kind != nkSmmError) {
				if (kind != nkSmmIdent) {
					res = newSmmAstNode();
					res->kind = kind;
					res->type = getLiteralTokenType(parser->curToken);
					res->token = parser->curToken;
				} else {
					PSmmAstNode var = (PSmmAstNode)smmGetDictValue(parser->idents, parser->curToken->repr, parser->curToken->hash, false);
					if (!var) {
						identToken = parser->curToken;
					} else if (var->kind != nkSmmIdent) {
						const char* tokenStr = nodeKindToString[var->kind];
						smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
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
				const char* got = smmTokenToString(parser->curToken, gotBuf);
				smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "identifier or literal", got);
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
					smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
				}
			}
		} while (kind == nkSmmError && parser->curToken->kind != ';' && parser->curToken->kind != tkSmmEof);
	}

	if (doNeg) {
		if (res->kind == nkSmmInt) {
			res->token->kind = tkSmmInt;
			res->token->sintVal = -(int64_t)res->token->uintVal;
			switch (res->type->kind) {
			case tiSmmInt16: if (res->token->sintVal == INT8_MIN) res->type = &builtInTypes[tiSmmInt8]; break;
			case tiSmmInt32: if (res->token->sintVal == INT16_MIN) res->type = &builtInTypes[tiSmmInt16]; break;
			case tiSmmInt64: if (res->token->sintVal == INT32_MIN) res->type = &builtInTypes[tiSmmInt32]; break;
			case tiSmmUInt64:
				res->type = &builtInTypes[tiSmmInt64];
				if (res->token->sintVal != INT64_MIN) {
					smmPostMessage(wrnSmmConversionDataLoss,
						res->token->filePos,
						res->type->name,
						builtInTypes[tiSmmInt64].name);
				}
				break;
			}
		} else {
			PSmmAstNode neg = newSmmAstNode();
			neg->kind = nkSmmNeg;
			neg->left = res;
			neg->type = res->type;
			if (neg->type->flags & tifSmmUnsigned) {
				neg->type = &builtInTypes[neg->type->kind - tiSmmUInt8 + tiSmmInt8];
			}
			res = neg;
		}
	}
	return res;
}

PSmmAstNode parseBinOp(PSmmParser parser, PSmmAstNode left, int prec) {
	while (true) {
		PSmmBinaryOperator binOp = parser->operatorPrecedences[parser->curToken->kind & 0x7f];
		if (!binOp || binOp->precedence < prec) {
			return left;
		}

		PSmmToken resToken = parser->curToken;

		getNextToken(parser);
		PSmmAstNode right = parseFactor(parser);
		if (right == &errorNode) {
			return right;
		}

		PSmmBinaryOperator nextBinOp = parser->operatorPrecedences[parser->curToken->kind & 0x7f];
		if (nextBinOp && nextBinOp->precedence > binOp->precedence) {
			right = parseBinOp(parser, right, binOp->precedence + 1);
			if (right == &errorNode) {
				return right;
			}
		}

		PSmmAstNode res = newSmmAstNode();
		res->left = left;
		res->right = right;
		res->token = resToken;
		left = binOp->setupNode(res);
	}
}

PSmmAstNode parseExpression(PSmmParser parser) {
	PSmmAstNode left = parseFactor(parser);
	if (left != &errorNode) {
		left = parseBinOp(parser, left, 0);
	}
	return left;
}

PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken eqToken = parser->curToken;
	expect(parser, '=');
	PSmmAstNode val = parseExpression(parser);
	if (lval == &errorNode || val == &errorNode) return &errorNode;
	if (!lval->type) {
		if (val->type->kind == tiSmmSoftFloat64) {
			val->type = &builtInTypes[tiSmmFloat64];
		}
		lval->type = val->type;
	}
	PSmmAstNode assignment = newSmmAstNode();
	assignment->kind = nkSmmAssignment;
	assignment->left = lval;
	assignment->right = val;
	assignment->type = lval->type;
	assignment->token = eqToken;
	return assignment;
}

PSmmAstNode parseDeclaration(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken declToken = parser->curToken;
	expect(parser, ':');
	if (parser->curToken->kind == tkSmmIdent) {
		//Type is given in declaration so use it
		PSmmAstNode typeInfo = (PSmmAstNode)smmGetDictValue(parser->idents, parser->curToken->repr, parser->curToken->hash, false);
		if (!typeInfo || typeInfo->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
		}
		else if (!lval->type) {
			lval->type = typeInfo->type;
		}
		getNextToken(parser);
	}
	PSmmAstNode decl = newSmmAstNode();
	decl->kind = nkSmmDecl;
	decl->token = declToken;
	decl->left = lval;
	if (parser->curToken->kind == '=') {
		decl->right = parseAssignment(parser, lval);
	}
	// Otherwise it should just be declaration so ';' is expected next
	return decl;
}

PSmmAstNode parseStatement(PSmmParser parser) {
	PSmmAstNode lval;

	struct SmmFilePos fpos = parser->curToken->filePos;
	lval = parseExpression(parser);

	bool justCreatedLValIdent = (lval->kind == nkSmmIdent) && (lval->type == NULL);
	
	if (lval->kind != nkSmmIdent && (parser->curToken->kind == ':' || parser->curToken->kind == '=')) {
		smmPostMessage(errSmmOperandMustBeLVal, fpos);
		findToken(parser, ';');
		return &errorNode;
	}

	if (parser->curToken->kind == ':') {
		if ((lval->kind == nkSmmIdent) && (lval->type != NULL)) {
			smmPostMessage(errSmmRedefinition, lval->token->filePos, lval->token->repr);
		}
		return parseDeclaration(parser, lval);
	} else if (parser->curToken->kind == '=') {
		return parseAssignment(parser, lval);
	}
	else if (lval->kind == nkSmmIdent) {
		const char* expected;
		if (justCreatedLValIdent) expected = ": or =";
		else expected = "=";
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, expected, parser->curToken->repr);
		findToken(parser, ';');
		return &errorNode;
	}
	return lval;
}

// Called from parseBinOp for specific binary operators
PSmmAstNode setupMulDivModNode(PSmmAstNode res) {
	PSmmTypeInfo type = (res->left->type->kind > res->right->type->kind) ? res->left->type : res->right->type;
	PSmmTypeInfo ftype = type->kind < tiSmmFloat32 ? &builtInTypes[tiSmmSoftFloat64] : type;
	switch (res->token->kind) {
	case tkSmmIntDiv: case tkSmmIntMod: {
		bool bothUnsigned = (res->left->type->flags & res->right->type->flags & tifSmmUnsigned);
		res->kind = bothUnsigned ? nkSmmUDiv : nkSmmSDiv;
		if (res->token->kind == tkSmmIntMod) res->kind += nkSmmSRem - nkSmmSDiv;
		res->type = type;
		if (type->kind >= tiSmmFloat32) {
			char buf[4];
			smmPostMessage(errSmmBadOperandsType, res->token->filePos, smmTokenToString(res->token, buf), type->name);
			if (res->token->kind == tkSmmIntMod) res->kind = nkSmmFRem;
			else res->kind = nkSmmFDiv;
			res->type = ftype;
		}
		break;
	}
	case '*':
		if (type->flags & tifSmmFloat) res->kind = nkSmmFMul;
		else res->kind = nkSmmMul;
		res->type = type;
		break;
	case '/':
		res->kind = nkSmmFDiv;
		res->type = ftype;
		break;
	case '%':
		res->kind = nkSmmFRem;
		res->type = ftype;
		break;
	default:
		assert(false && "Got unexpected token");
	}
	return res;
}

// Called from parseBinOp for specific binary operators
PSmmAstNode setupAddSubNode(PSmmAstNode res) {
	if (res->token->kind == '+') res->kind = nkSmmAdd;
	else res->kind = nkSmmSub;

	if (res->left->type->kind >= res->right->type->kind) {
		res->type = res->left->type;
	} else {
		res->type = res->right->type;
	}

	if (res->type->kind >= tiSmmFloat32) res->kind++; // Add to FAdd, Sub to FSub

	return res;
}

// Called from parseBinOp for specific binary operators
PSmmAstNode setupLogicOpNode(PSmmAstNode res) {
	assert(false && "Not yet implemented!");
	return res;
}

/********************************************************
API Functions
*********************************************************/

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator) {
	PSmmParser parser = (PSmmParser)allocator->alloc(allocator, sizeof(struct SmmParser));
	parser->lex = lex;
	parser->curToken = smmGetNextToken(lex);
	parser->allocator = allocator;

	// Init idents dict
	parser->idents = smmCreateDict(parser->allocator, SMM_PARSER_IDENTS_DICT_SIZE, NULL, NULL);
	int cnt = sizeof(builtInTypes) / sizeof(struct SmmTypeInfo);
	for (int i = 0; i < cnt; i++) {
		PSmmAstNode typeNode = newSmmAstNode();
		typeNode->kind = nkSmmType;
		typeNode->type = &builtInTypes[i];
		smmAddDictValue(parser->idents, typeNode->type->name, smmHashString(typeNode->type->name), typeNode);
	}

	static struct SmmBinaryOperator mulDivModOp = { setupMulDivModNode, 120 };
	static struct SmmBinaryOperator addSubOp = { setupAddSubNode, 110 };
	static struct SmmBinaryOperator andOp = { setupLogicOpNode, 100 };
	static struct SmmBinaryOperator xorOp = { setupLogicOpNode, 90 };
	static struct SmmBinaryOperator orOp = { setupLogicOpNode, 80 };
	static struct SmmBinaryOperator mulDivMod = { setupMulDivModNode, 120 };

	static PSmmBinaryOperator binOpPrecs[128] = { 0 };
	static bool binOpsInitialized = false;
	if (!binOpsInitialized) {
		// Init binary operator precedences. Index is tokenKind & 0x7f

		binOpsInitialized = true;
		binOpPrecs['+'] = &addSubOp;
		binOpPrecs['-'] = &addSubOp;

		binOpPrecs['*'] = &mulDivModOp;
		binOpPrecs['/'] = &mulDivModOp;
		binOpPrecs[tkSmmIntDiv & 0x7f] = &mulDivModOp;
		binOpPrecs[tkSmmIntMod & 0x7f] = &mulDivModOp;

		binOpPrecs[tkSmmAndOp & 0x7f] = &andOp;
		binOpPrecs[tkSmmXorOp & 0x7f] = &xorOp;
		binOpPrecs[tkSmmOrOp & 0x7f] = &orOp;
	}
	
	parser->operatorPrecedences = binOpPrecs;

	return parser;
}


PSmmAstNode smmParse(PSmmParser parser) {
	PSmmAstNode program = newSmmAstNode();
	program->kind = nkSmmProgram;
	PSmmAstNode lastStmt = program;
	PSmmAstNode lastDecl = program;
	
	while (parser->curToken->kind != tkSmmEof) {
		PSmmAstNode curStmt = parseStatement(parser);
		if (curStmt->kind == nkSmmDecl) {
			curStmt->next = lastDecl->next;
			lastDecl->next = curStmt;
			if (lastDecl == lastStmt) lastStmt = curStmt;
			lastDecl = curStmt;
			curStmt = curStmt->right;
		}
		if (!expect(parser, ';')) curStmt = &errorNode;
		if (curStmt == &errorNode) {
			curStmt = newSmmAstNode();
			*curStmt = errorNode;
		}
		lastStmt->next = curStmt;
		lastStmt = curStmt;
	}
	return program;
}
