#include "smmparser.h"
#include "ibsdictionary.h"

#include <assert.h>

/********************************************************
Type Definitions
*********************************************************/

// There should be one string corresponding to each value of SmmAstNodeKind enum
const char* nodeKindToString[] = {
	"error", "Program", "func",
	"Block:", "Scope:",
	"Decl", "Ident", "Const",
	"=",
	"+", "+.",
	"-", "-.",
	"*", "*.",
	"udiv", "sdiv", "/",
	"umod", "smod", "%",
	"-", "type", "int", "float", "bool",
	"cast", "param", "call", "return",
	"and", "xor" , "or",
	"==", "!=", ">", ">=", "<", "<=", "not",
	"if", "while",
};

struct SmmTypeInfo builtInTypes[] = {
	{ tiSmmUnknown, 0, "/unknown/" },{ tiSmmVoid, 0, "/void/" },
	{ tiSmmBool, 1, "bool", 0, 0, 0, 1 },
	{ tiSmmUInt8, 1, "uint8", 1, 1 },{ tiSmmUInt16, 2, "uint16", 1, 1 },
	{ tiSmmUInt32, 4, "uint32", 1, 1 },{ tiSmmUInt64, 8, "uint64", 1, 1 },
	{ tiSmmInt8, 1, "int8", 1 },{ tiSmmInt16, 2, "int16", 1 },
	{ tiSmmInt32, 4, "int32", 1 },{ tiSmmInt64, 8, "int64", 1 },
	{ tiSmmFloat32, 4, "float32", 0, 0, 1 },{ tiSmmFloat64, 8, "float64", 0, 0, 1 },
	{ tiSmmSoftFloat64, 8, "/sfloat64/", 0, 0, 1 },
};

static int binOpPrecs[128] = { 0 };

static union SmmAstNode errorNode = { { nkSmmError, 0, 0, 0, NULL, &builtInTypes[0] } };

/********************************************************
Private Functions
*********************************************************/

static PSmmAstNode parseExpression(PSmmParser parser);
static PSmmAstNode parseStatement(PSmmParser parser);

static PSmmToken newToken(int kind, const char* repr, struct SmmFilePos filePos, PIbsAllocator a) {
	PSmmToken res = ibsAlloc(a, sizeof(struct SmmToken));
	res->kind = kind;
	res->repr = repr;
	res->filePos = filePos;
	return res;
}

static PSmmAstScopeNode newScopeNode(PSmmParser parser) {
	PSmmAstScopeNode scope = smmNewAstNode(nkSmmScope, parser->a);
	scope->level = parser->curScope->level + 1;
	scope->prevScope = parser->curScope;
	scope->lastDecl = (PSmmAstDeclNode)scope;
	scope->returnType = parser->curScope->returnType;
	parser->curScope = scope;
	return scope;
}

static void getNextToken(PSmmParser parser) {
	parser->prevToken = parser->curToken;
	parser->curToken = smmGetNextToken(parser->lex);
}

static bool isTerminatingToken(int tokenKind) {
	return tokenKind == ';' || tokenKind == '{' || tokenKind == '}'
		|| tokenKind == tkSmmEof;
}

/**
* Tries to find given token until it comes to a terminating token.
* Returns true if token is found.
*/
static bool findToken(PSmmParser parser, int tokenKind) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenKind && !isTerminatingToken(curKind)) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	return curKind == tokenKind;
}

/**
* Tries to find one of the given tokens until it comes to a terminating token.
* If one of these tokens is found its kind is returned, otherwise 0 is returned.
*/
static int findEitherToken(PSmmParser parser, int tokenKind1, int tokenKind2) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenKind1 && curKind != tokenKind2 && !isTerminatingToken(curKind)) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	if (curKind == tokenKind1) return tokenKind1;
	if (curKind == tokenKind2) return tokenKind2;
	return 0;
}

static PSmmToken expect(PSmmParser parser, uint32_t kind) {
	PSmmToken token = parser->curToken;
	struct SmmFilePos filePos = token->filePos;
	if (token->kind != kind) {
		if (token->kind != tkSmmErr && token->filePos.lineNumber != parser->lastErrorLine) {
			// If it is smmErr, lexer already reported the error
			char expBuf[4];
			char tmpRepr[2] = { (char)kind, 0 };
			struct SmmToken tmpToken = { kind };
			tmpToken.repr = tmpRepr;
			const char* expected = smmTokenToString(&tmpToken, expBuf);
			if (token->isFirstOnLine && parser->prevToken) {
				filePos = parser->prevToken->filePos;
			}
			smmPostMessage(parser->msgs, errSmmNoExpectedToken, filePos, expected);
		}
		parser->lastErrorLine = filePos.lineNumber;
		return NULL;
	}
	getNextToken(parser);
	return token;
}

static PSmmTypeInfo parseType(PSmmParser parser) {
	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind != tkSmmIdent) {
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "type", got);
		}
		typeInfo = &builtInTypes[tiSmmUnknown];
	} else {
		PSmmAstNode typeInfoNode = ibsDictGet(parser->idents, parser->curToken->repr);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(parser->msgs, errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = &builtInTypes[tiSmmUnknown];
		} else {
			typeInfo = typeInfoNode->type;
		}
		getNextToken(parser);
	}
	return typeInfo;
}

static PSmmAstNode createNewIdent(PSmmParser parser, PSmmToken identToken) {
	if (!identToken->canBeNewSymbol) {
		smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", "':'");
		return &errorNode;
	}
	PSmmAstIdentNode var = smmNewAstNode(nkSmmIdent, parser->a);
	var->isIdent = true;
	var->token = identToken;
	var->level = parser->curScope->level;
	return (PSmmAstNode)var;
}

static PSmmTypeInfo getLiteralTokenType(PSmmToken token) {
	switch (token->kind) {
	case tkSmmBool: return &builtInTypes[tiSmmBool];
	case tkSmmFloat: return &builtInTypes[tiSmmSoftFloat64];
	case tkSmmUInt:
		if (token->uintVal <= INT8_MAX) return &builtInTypes[tiSmmInt8];
		if (token->uintVal <= INT16_MAX) return &builtInTypes[tiSmmInt16];
		if (token->uintVal <= INT32_MAX) return &builtInTypes[tiSmmInt32];
		if (token->uintVal <= INT64_MAX) return &builtInTypes[tiSmmInt64];
		return &builtInTypes[tiSmmUInt64];
	case tkSmmInt:
		if (INT8_MIN <= token->sintVal && token->sintVal <= INT8_MAX) return &builtInTypes[tiSmmInt8];
		if (INT16_MIN <= token->sintVal && token->sintVal <= INT16_MAX) return &builtInTypes[tiSmmInt16];
		if (INT32_MIN <= token->sintVal && token->sintVal <= INT32_MAX) return &builtInTypes[tiSmmInt32];
		return &builtInTypes[tiSmmInt64];
	default:
		assert(false && "Got literal of unknown kind!");
		return &builtInTypes[tiSmmUnknown];
	}
}

static PSmmAstNode getLiteralNode(PSmmParser parser) {
	PSmmAstNode res = smmNewAstNode(nkSmmError, parser->a);
	res->type = getLiteralTokenType(parser->curToken);
	if (res->type->isInt) res->kind = nkSmmInt;
	else if (res->type->isFloat) res->kind = nkSmmFloat;
	else if (res->type->isBool) res->kind = nkSmmBool;
	else assert(false && "Got unimplemented literal type");
	res->token = parser->curToken;
	res->isConst = true;
	getNextToken(parser);
	return res;
}

static PSmmAstNode parseIdentFactor(PSmmParser parser) {
	PSmmAstNode res = &errorNode;
	PSmmToken identToken = parser->curToken;
	getNextToken(parser);
	PSmmAstIdentNode var = ibsDictGet(parser->idents, identToken->repr);

	if (parser->curToken->kind == ':') {
		// This is declaration
		if (var) {
			if (var->kind == nkSmmType) {
				// If type identifier was used as variable identifier
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(parser->msgs, errSmmIdentTaken, identToken->filePos, identToken->repr, tokenStr);
			} else if (var->level < parser->curScope->level) {
				res = createNewIdent(parser, identToken);
			} else if (var->kind == nkSmmFunc) {
				//Posible overload
				res = createNewIdent(parser, identToken);
			} else {
				smmPostMessage(parser->msgs, errSmmRedefinition, identToken->filePos, identToken->repr);
			}
		} else {
			res = createNewIdent(parser, identToken);
		}
	} else if (parser->curToken->kind == '(') {
		getNextToken(parser);
		if (var && var->kind == nkSmmType) {
			// This is a cast
			PSmmAstNode expr = parseExpression(parser);
			if (expr == &errorNode) {
				if (findToken(parser, ')')) getNextToken(parser);
				return expr;
			}
			expect(parser, ')');
			res = smmNewAstNode(nkSmmCast, parser->a);
			res->left = expr;
			res->token = identToken;
			res->type = var->type;
		} else {
			PSmmAstNode resCall = smmNewAstNode(nkSmmCall, parser->a);
			resCall->isIdent = true;
			resCall->token = identToken;
			if (parser->curToken->kind != ')') {
				PSmmAstNode lastArg = parseExpression(parser);
				resCall->asCall.args = lastArg;
				if (lastArg == &errorNode) resCall->kind = nkSmmError;
				while (parser->curToken->kind == ',') {
					getNextToken(parser);
					lastArg->next = parseExpression(parser);
					lastArg = lastArg->next;
					if (lastArg == &errorNode) resCall->kind = nkSmmError;
				}
			}
			if (!expect(parser, ')') || resCall->kind == nkSmmError) {
				findToken(parser, ')');
			} else {
				res = resCall;
			}
		}
	} else {
		if (var) {
			if (var->kind == nkSmmType || !var->isIdent) {
				// if type or keyword is used in place of variable
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(parser->msgs, errSmmIdentTaken, identToken->filePos, identToken->repr, tokenStr);
			} else if (var->kind == nkSmmFunc) {
				smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "(", parser->curToken->repr);
			} else {
				res = smmNewAstNode(nkSmmIdent, parser->a);
				*res = *(PSmmAstNode)var;
				res->token = identToken;
			}
		} else {
			res = smmNewAstNode(nkSmmIdent, parser->a);
			res->isIdent = true;
			res->token = identToken;
		}
	}

	return res;
}

static PSmmAstNode parseFuncParams(PSmmParser parser, PSmmAstParamNode firstParam) {
	assert(firstParam != NULL);
	assert(parser->curToken->kind == ':');

	getNextToken(parser); //skip ':'

	PSmmTypeInfo typeInfo = parseType(parser);
	if (typeInfo->kind == tiSmmUnknown && !findEitherToken(parser, ',', ')')) return &errorNode;

	int paramCount = 1;
	firstParam->kind = nkSmmParamDefinition;
	firstParam->type = typeInfo;
	firstParam->isIdent = true;
	firstParam->level = parser->curScope->level + 1;
	ibsDictPush(parser->idents, firstParam->token->repr, firstParam);

	PSmmAstParamNode param = firstParam;

	while (parser->curToken->kind == ',') {
		getNextToken(parser);
		PSmmToken paramName = expect(parser, tkSmmIdent);
		if (!paramName) {
			findEitherToken(parser, ',', ')');
			continue;
		}
		if (!expect(parser, ':')) {
			findEitherToken(parser, ',', ')');
			continue;
		}
		PSmmTypeInfo paramTypeInfo = parseType(parser);
		if (paramTypeInfo->kind == tiSmmUnknown) {
			findEitherToken(parser, ',', ')');
		}
		PSmmAstParamNode newParam = ibsDictGet(parser->idents, paramName->repr);
		if (newParam) {
			if (newParam->level == parser->curScope->level + 1) {
				smmPostMessage(parser->msgs, errSmmRedefinition, paramName->filePos, paramName->repr);
				continue;
			} else if (!newParam->isIdent) {
				const char* tokenStr = nodeKindToString[newParam->kind];
				smmPostMessage(parser->msgs, errSmmIdentTaken, paramName->filePos, paramName->repr, tokenStr);
				continue;
			}
		}
		paramCount++;
		newParam = smmNewAstNode(nkSmmParam, parser->a);
		newParam->isIdent = true;
		newParam->level = parser->curScope->level + 1;
		newParam->token = paramName;
		newParam->type = paramTypeInfo;
		ibsDictPush(parser->idents, paramName->repr, newParam);
		param->next = newParam;
		param = newParam;
	}

	firstParam->count = paramCount;

	return (PSmmAstNode)firstParam;
}

static PSmmToken getUnaryOperator(PSmmParser parser) {
	PSmmToken res;
	switch (parser->curToken->kind) {
	case '!':
		smmPostMessage(parser->msgs, errSmmBangUsedAsNot, parser->curToken->filePos);
		parser->curToken->kind = tkSmmNot;
		// fallthrough
	case '-': case tkSmmNot: case '+':
		res = parser->curToken;
		getNextToken(parser);
		break;
	default:
		res = NULL;
		break;
	}
	return res;
}

static PSmmAstNode parseFactor(PSmmParser parser) {
	PSmmAstNode res = &errorNode;
	bool canBeFuncDefn = parser->prevToken && parser->prevToken->kind == ':';
	PSmmToken unary = getUnaryOperator(parser);
	canBeFuncDefn = canBeFuncDefn && !unary;
	if (parser->curToken->kind == '(') {
		getNextToken(parser);
		if (parser->curToken->kind == ')') {
			getNextToken(parser);
			if (canBeFuncDefn) {
				PSmmAstNode param = smmNewAstNode(nkSmmParamDefinition, parser->a);
				param->asParam.count = 0;
				return param;
			}
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "expression", "')'");
			findToken(parser, ';');
			return &errorNode;
		}
		parser->curToken->canBeNewSymbol = canBeFuncDefn;
		res = parseExpression(parser);
		if (res == &errorNode) {
			if (findToken(parser, ')')) getNextToken(parser);
			return &errorNode;
		}
		// In case expression is followed by ':' it must be just ident and thus first param of func declaration
		if (parser->curToken->kind == ':') {
			assert(canBeFuncDefn && res->isIdent);
			res = parseFuncParams(parser, &res->asParam);
		}
		if (!expect(parser, ')')) {
			int tk = parser->curToken->kind;
			PSmmAstNode param = res;
			switch (res->kind) {
			case nkSmmParamDefinition:
				while (param) {
					ibsDictPop(parser->idents, param->token->repr);
					param = param->next;
				}
				// If it seems only ')' was forgotten in func definition we want to coninue parsing.
				// Otherwise we want to fallthrough to error handling.
				if (tk == tkSmmRArrow || tk == '{' || tk == ';') break;
			default:
				if (findToken(parser, ')')) getNextToken(parser);
				return &errorNode;
			}
		}
	} else {
		switch (parser->curToken->kind) {
		case tkSmmIdent: res = parseIdentFactor(parser); break;
		case tkSmmUInt: case tkSmmInt: case tkSmmFloat: case tkSmmBool:
			res = getLiteralNode(parser);
			break;
		default:
			if (parser->curToken->kind != tkSmmErr) {
				char gotBuf[4];
				const char* got = smmTokenToString(parser->curToken, gotBuf);
				smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "identifier or literal", got);
			}
			break;
		}
	}

	if (res == &errorNode || !unary) return res;

	switch (unary->kind) {
	case '-':
		if (res->kind == nkSmmInt || res->kind == nkSmmFloat) {
			assert(false && "Lexer should have handled this case!");
		} else {
			PSmmAstNode neg = smmNewAstNode(nkSmmNeg, parser->a);
			neg->left = res;
			neg->token = unary;
			res = neg;
		}
		break;
	case tkSmmNot:
		{
			PSmmAstNode not = smmNewAstNode(nkSmmNot, parser->a);
			not->left = res;
			not->type = &builtInTypes[tiSmmBool];
			not->token = unary;
			res = not;
			break;
		}
	}
	return res;
}

static PSmmAstNode parseBinOp(PSmmParser parser, PSmmAstNode left, int prec) {
	while (true) {
		int precedence = binOpPrecs[parser->curToken->kind & 0x7f];
		if (!precedence || precedence < prec) {
			return left;
		}

		PSmmToken opToken = parser->curToken;

		getNextToken(parser);
		PSmmAstNode right = parseFactor(parser);
		if (right == &errorNode) return &errorNode;

		int nextPrecedence = binOpPrecs[parser->curToken->kind & 0x7f];
		if (nextPrecedence && nextPrecedence > precedence) {
			right = parseBinOp(parser, right, precedence + 1);
			if (right == &errorNode) return &errorNode;
		}

		PSmmAstNode res = smmNewAstNode(nkSmmError, parser->a);
		res->left = left;
		res->right = right;
		res->token = opToken;
		res->isBinOp = true;

		switch (res->token->kind) {
		case tkSmmIntDiv: res->kind = nkSmmSDiv; break; // Second pass might change this to unsigned version
		case tkSmmIntMod: res->kind = nkSmmSRem; break;
		case '*': res->kind = nkSmmMul; break;
		case '/': res->kind = nkSmmFDiv; break;
		case '%': res->kind = nkSmmFRem; break;
		case '+': res->kind = nkSmmAdd; break;
		case '-': res->kind = nkSmmSub; break;
		case '>': res->kind = nkSmmGt; break;
		case '<': res->kind = nkSmmLt; break;
		case tkSmmEq: res->kind = nkSmmEq; break;
		case tkSmmNotEq: res->kind = nkSmmNotEq; break;
		case tkSmmGtEq: res->kind = nkSmmGtEq; break;
		case tkSmmLtEq: res->kind = nkSmmLtEq; break;
		case tkSmmAndOp: res->kind = nkSmmAndOp; break;
		case tkSmmXorOp: res->kind = nkSmmXorOp; break;
		case tkSmmOrOp: res->kind = nkSmmOrOp; break;
		default:
			assert(false && "Got unexpected token for binary operation");
			break;
		}

		switch (res->token->kind) {
		case tkSmmAndOp: case tkSmmXorOp: case tkSmmOrOp:
		case tkSmmEq: case tkSmmNotEq: case tkSmmGtEq: case tkSmmLtEq:
		case '>': case '<':
			res->type = &builtInTypes[tiSmmBool];
			break;
		}
		left = res;
	}
}

static PSmmAstNode parseExpression(PSmmParser parser) {
	PSmmAstNode left = parseFactor(parser);
	if (left != &errorNode && left->kind != nkSmmParamDefinition) {
		left = parseBinOp(parser, left, 0);
	}
	return left;
}

static void removeScopeVars(PSmmParser parser) {
	PSmmAstDeclNode curDecl = parser->curScope->decls;
	while (curDecl) {
		ibsDictPop(parser->idents, curDecl->left->left->token->repr);
		curDecl = curDecl->nextDecl;
	}
	PSmmAstScopeNode prevScope = parser->curScope->prevScope;
	parser->curScope = prevScope;
}

static PSmmAstBlockNode parseBlock(PSmmParser parser, PSmmTypeInfo curFuncReturnType, bool isFuncBlock) {
	assert(parser->curToken->kind == '{');
	getNextToken(parser); // Skip '{'
	PSmmAstBlockNode block = smmNewAstNode(nkSmmBlock, parser->a);
	block->scope = newScopeNode(parser);
	block->scope->returnType = curFuncReturnType;
	PSmmAstNode* nextStmt = &block->stmts;
	PSmmAstNode curStmt = NULL;
	while (parser->curToken->kind != tkSmmEof && parser->curToken->kind != '}') {
		if (curStmt && curStmt->kind == nkSmmReturn) {
			smmPostMessage(parser->msgs, errSmmUnreachableCode, parser->curToken->filePos);
		}
		curStmt = parseStatement(parser);
		if (curStmt != NULL && curStmt != &errorNode) {
			*nextStmt = curStmt;
			nextStmt = &curStmt->next;
		}
	}

	if (curStmt) {
		bool isLastStmtReturn = curStmt->kind == nkSmmReturn;
		bool isLastStmtReturningBlock = curStmt->kind == nkSmmBlock && curStmt->asBlock.endsWithReturn;
		block->endsWithReturn = isLastStmtReturn || isLastStmtReturningBlock;
	}

	if (isFuncBlock) {
		bool funcHasReturnType = curFuncReturnType->kind != tiSmmUnknown && curFuncReturnType->kind != tiSmmVoid;
		if (funcHasReturnType && !block->endsWithReturn && curStmt != &errorNode) {
			smmPostMessage(parser->msgs, errSmmFuncMustReturnValue, parser->curToken->filePos);
		} else if (!funcHasReturnType && !block->endsWithReturn) {
			// We add empty return statement
			PSmmAstNode retNode = smmNewAstNode(nkSmmReturn, parser->a);
			retNode->token = newToken(tkSmmReturn, "return", parser->curToken->filePos, parser->a);
			retNode->type = curFuncReturnType;
			*nextStmt = retNode;
		}
	}

	expect(parser, '}');
	removeScopeVars(parser);

	return block;
}

/**
* This is called after we already parsed parameters so we expect optional
* arrow and type and then also optional function body. Func node should
* have kind, token and params set.
*/
static PSmmAstNode parseFunction(PSmmParser parser, PSmmAstFuncDefNode func) {
	assert(func->kind == nkSmmFunc && func->token);
	bool ignoreMissingSemicolon = false;
	int curKind = parser->curToken->kind;
	if (curKind != tkSmmRArrow && curKind != '{' && curKind != ';') {
		if (curKind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "one of '->', '{' or ';'", got);
		}
		if (!parser->curToken->isFirstOnLine) {
			findToken(parser, tkSmmRArrow);
		}
		// Otherwise assume ';' was forgotten
		ignoreMissingSemicolon = true;
	}
	PSmmTypeInfo typeInfo = &builtInTypes[tiSmmVoid];
	if (parser->curToken->kind == tkSmmRArrow) {
		ignoreMissingSemicolon = false;
		getNextToken(parser);
		typeInfo = parseType(parser);
	}
	func->returnType = typeInfo;
	if (parser->curToken->kind == '{') {
		func->body = parseBlock(parser, typeInfo, true);
	} else if (parser->curToken->kind != ';') {
		if (!ignoreMissingSemicolon && parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "{ or ;", got);
		}
		if (!parser->curToken->isFirstOnLine) {
			// if illegal token is in the same line then we will skip all until terminating token
			findToken(parser, ';');
		}
		// Otherwise we assume ';' is forgotten so we don't do findToken here hoping normal stmt starts next
		return &errorNode;
	}
	PSmmAstParamNode param = func->params;
	while (param) {
		ibsDictPop(parser->idents, param->token->repr);
		param = param->next;
	}
	return (PSmmAstNode)func;
}

static PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken eqToken = parser->curToken;
	getNextToken(parser);
	PSmmAstNode val = parseExpression(parser);
	if (val == &errorNode) {
		findToken(parser, ';');
		return &errorNode;
	}

	if (val->kind == nkSmmParamDefinition) return val;

	PSmmAstNode assignment = smmNewAstNode(nkSmmAssignment, parser->a);
	assignment->left = lval;
	assignment->right = val;
	assignment->type = lval->type;
	assignment->token = eqToken;
	return assignment;
}

static PSmmAstNode parseDeclaration(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken declToken = parser->curToken;
	assert(parser->curToken->kind == ':');
	getNextToken(parser);

	if (parser->curToken->kind == tkSmmIdent) {
		//Type is given in declaration so use it
		lval->type = parseType(parser);
	} else if (parser->curToken->kind == ';') {
		// In case of a statement like 'a :;'
		smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "type", "';'");
		lval->type = &builtInTypes[tiSmmUnknown];
	}

	PSmmAstDeclNode spareNode = NULL;
	PSmmAstNode expr = NULL;
	if (parser->curToken->kind == '=') {
		expr = parseAssignment(parser, lval);
	} else if (parser->curToken->kind == ':') {
		lval->kind = nkSmmConst;
		lval->isConst = true;
		expr = parseAssignment(parser, lval);
		if (expr->kind == nkSmmParamDefinition) {
			expr->kind = nkSmmParam;
			if (parser->curScope->level > 0) {
				smmPostMessage(parser->msgs, errSmmFuncUnderScope, lval->token->filePos, lval->token->repr);
			}
			lval->kind = nkSmmFunc;
			lval->asFunc.params = (PSmmAstParamNode)expr;
			if (lval->asFunc.params->count == 0) {
				spareNode = &expr->asDecl;
				spareNode->kind = nkSmmDecl;
				lval->asFunc.params = NULL;
			}
			lval = parseFunction(parser, &lval->asFunc);
			if (parser->curScope->level > 0) {
				lval = &errorNode;
			}
			expr = lval;
		}
	} else if (parser->curToken->kind != ';') {
		expr = &errorNode;
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "':', '=' or type", got);
		}
		findToken(parser, ';');
	}

	if (lval == &errorNode) {
		return &errorNode;
	}

	PSmmAstNode existing = ibsDictGet(parser->idents, lval->token->repr);
	if (existing && existing->asIdent.level == lval->asIdent.level && lval->kind != nkSmmFunc) {
		assert(existing->kind == nkSmmFunc);
		smmPostMessage(parser->msgs, errSmmRedefinition, lval->token->filePos, lval->token->repr);
		return &errorNode;
	}

	ibsDictPush(parser->idents, lval->token->repr, lval);
	PSmmAstDeclNode decl = spareNode ? spareNode : smmNewAstNode(nkSmmDecl, parser->a);
	decl->token = declToken;
	if (expr == &errorNode) expr = NULL;

	parser->curScope->lastDecl->nextDecl = decl;
	parser->curScope->lastDecl = decl;

	if (lval->kind == nkSmmFunc) {
		decl->left = lval;
		return NULL;
	}

	if (!expr) {
		expr = smmNewAstNode(nkSmmAssignment, parser->a);
		expr->left = lval;
		expr->right = smmGetZeroValNode(parser->curToken->filePos, lval->type, parser->a);
		expr->type = lval->type;
		expr->token = ibsAlloc(parser->a, sizeof(struct SmmToken));
		if (lval->isConst) {
			expr->token->repr = ":";
		} else {
			expr->token->repr = "=";
		}
		expr->token->kind = expr->token->repr[0];
		expr->token->filePos = parser->curToken->filePos;
	}

	decl->left = expr;

	if (lval->isConst) return NULL;

	return (PSmmAstNode)decl;
}

static PSmmAstNode parseReturnStmt(PSmmParser parser) {
	assert(parser->curToken->kind == tkSmmReturn);

	PSmmAstNode expr = NULL;

	PSmmToken retToken = parser->curToken;
	getNextToken(parser);
	if (parser->curToken->kind != ';') {
		expr = parseExpression(parser);
		if (expr == &errorNode) {
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
	}

	PSmmAstNode res = smmNewAstNode(nkSmmReturn, parser->a);
	res->type = parser->curScope->returnType;
	res->left = expr;
	res->token = retToken;
	expect(parser, ';');
	return res;
}

static PSmmAstNode parseExpressionStmt(PSmmParser parser) {
	PSmmAstNode lval;

	struct SmmFilePos fpos = parser->curToken->filePos;
	parser->curToken->canBeNewSymbol = true;
	lval = parseExpression(parser);

	if (lval == &errorNode) {
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}

	if (!lval->isIdent && (parser->curToken->kind == ':' || parser->curToken->kind == '=')) {
		smmPostMessage(parser->msgs, errSmmOperandMustBeLVal, fpos);
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}

	if (parser->curToken->kind == ':') {
		lval = parseDeclaration(parser, lval);
	} else if (parser->curToken->kind == '=') {
		lval = parseAssignment(parser, lval);
	}
	if (lval) {
		bool isJustIdent = lval->isIdent && (lval->kind != nkSmmCall) && (lval->kind != nkSmmError);
		bool isAnyBinOpExceptLogical = lval->isBinOp && lval->kind != nkSmmAndOp && lval->kind != nkSmmOrOp;
		if (isJustIdent || isAnyBinOpExceptLogical) {
			smmPostMessage(parser->msgs, wrnSmmNoEffectStmt, lval->token->filePos);
			if (isJustIdent) lval = NULL;
		}
	}
	if (parser->prevToken->kind != '}' && (lval != &errorNode || parser->curToken->kind == ';')) {
		expect(parser, ';');
	}
	return lval;
}

static PSmmAstNode parseIfWhileStmt(PSmmParser parser) {
	PSmmToken iftoken = parser->curToken;
	SmmAstNodeKind kind = nkSmmIf;
	int condTerm = tkSmmThen;
	if (iftoken->kind == tkSmmWhile) {
		kind = nkSmmWhile;
		condTerm = tkSmmDo;
	}
	getNextToken(parser);
	PSmmAstNode cond = parseExpression(parser);
	expect(parser, condTerm);
	PSmmAstNode body = parseStatement(parser);
	PSmmAstIfWhileNode ifstmt = smmNewAstNode(kind, parser->a);
	ifstmt->body = body;
	ifstmt->cond = cond;
	ifstmt->token = iftoken;
	if (parser->curToken->kind == tkSmmElse) {
		getNextToken(parser);
		ifstmt->elseBody = parseStatement(parser);
	}
	return (PSmmAstNode)ifstmt;
}

static PSmmAstNode parseStatement(PSmmParser parser) {
	switch (parser->curToken->kind) {
	case tkSmmReturn:
		return parseReturnStmt(parser);
	case '{':
		return (PSmmAstNode)parseBlock(parser, parser->curScope->returnType, false);
	case tkSmmIdent: case '(': case '-': case '+': case tkSmmNot:
	case tkSmmInt: case tkSmmFloat: case tkSmmBool:
		return parseExpressionStmt(parser);
	case tkSmmIf: case tkSmmWhile: return parseIfWhileStmt(parser);
	case tkSmmErr:
		if (findToken(parser, ';')) getNextToken(parser);
		return NULL;
	case ';':
		return NULL; // Just skip empty statements
	default:
		if (parser->lastErrorLine != parser->curToken->filePos.lineNumber) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(parser->msgs, errSmmGotUnexpectedToken, parser->curToken->filePos, "valid statement", got);
		}
		getNextToken(parser); // Skip the bad character
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}
}

/********************************************************
API Functions
*********************************************************/

PSmmAstNode smmGetZeroValNode(struct SmmFilePos filePos, PSmmTypeInfo varType, PIbsAllocator a) {
	PSmmAstNode zero = smmNewAstNode(nkSmmInt, a);
	if (!varType || varType->kind == tiSmmUnknown) varType = &builtInTypes[tiSmmInt32];
	zero->isConst = true;
	zero->type = varType;
	zero->token = ibsAlloc(a, sizeof(struct SmmToken));
	zero->token->filePos = filePos;
	if (varType->isInt) {
		zero->token->kind = tkSmmUInt;
		zero->token->repr = "0";
	} else if (varType->isFloat) {
		zero->kind = nkSmmFloat;
		zero->token->kind = tkSmmFloat;
		zero->token->repr = "0";
	} else if (varType->isBool) {
		zero->kind = nkSmmBool;
		zero->token->kind = tkSmmBool;
		zero->token->repr = "false";
	} else {
		assert(false && "Unsupported variable type!");
	}
	return zero;
}

void* smmNewAstNode(SmmAstNodeKind kind, PIbsAllocator a) {
	PSmmAstNode res = ibsAlloc(a, sizeof(union SmmAstNode));
	res->kind = kind;
	return res;
}

PSmmParser smmCreateParser(PSmmLexer lex, PSmmMsgs msgs, PIbsAllocator a) {
	assert(nodeKindToString[nkSmmTerminator - 1]); //Check if names for all node kinds are defined
	PSmmParser parser = ibsAlloc(a, sizeof(struct SmmParser));
	parser->lex = lex;
	parser->curToken = smmGetNextToken(lex);
	parser->a = a;
	parser->msgs = msgs;

	// Init idents dict
	parser->idents = ibsDictCreate(parser->a);
	int cnt = sizeof(builtInTypes) / sizeof(struct SmmTypeInfo);
	for (int i = 0; i < cnt; i++) {
		PSmmAstNode typeNode = smmNewAstNode(nkSmmType, parser->a);
		typeNode->type = &builtInTypes[i];
		ibsDictPut(parser->idents, typeNode->type->name, typeNode);
	}

	ibsDictPut(parser->idents, "int", ibsDictGet(parser->idents, "int32"));
	ibsDictPut(parser->idents, "uint", ibsDictGet(parser->idents, "uint32"));
	ibsDictPut(parser->idents, "float", ibsDictGet(parser->idents, "float32"));

	static bool binOpsInitialized = false;
	if (!binOpsInitialized) {
		// Init binary operator precedences. Index is tokenKind & 0x7f so its value must be less then 289
		// which is after this operation equal to '!', first operator character in ascii map

		binOpsInitialized = true;
		binOpPrecs['+'] = 100;
		binOpPrecs['-'] = 100;

		binOpPrecs['*'] = 120;
		binOpPrecs['/'] = 120;
		binOpPrecs[tkSmmIntDiv & 0x7f] = 120;
		binOpPrecs[tkSmmIntMod & 0x7f] = 120;

		binOpPrecs[tkSmmEq & 0x7f] = 110;
		binOpPrecs[tkSmmNotEq & 0x7f] = 110;
		binOpPrecs['>'] = 110;
		binOpPrecs[tkSmmGtEq & 0x7f] = 110;
		binOpPrecs['<'] = 110;
		binOpPrecs[tkSmmLtEq & 0x7f] = 110;

		binOpPrecs[tkSmmAndOp & 0x7f] = 90;
		binOpPrecs[tkSmmXorOp & 0x7f] = 80;
		binOpPrecs[tkSmmOrOp & 0x7f] = 80;
	}

	return parser;
}

PSmmAstNode smmParse(PSmmParser parser) {
	if (parser->curToken->kind == tkSmmEof) return NULL;
	PSmmAstNode program = smmNewAstNode(nkSmmProgram, parser->a);
	PSmmAstBlockNode block = smmNewAstNode(nkSmmBlock, parser->a);
	parser->curScope = smmNewAstNode(nkSmmScope, parser->a);
	parser->curScope->lastDecl = (PSmmAstDeclNode)parser->curScope;
	parser->curScope->returnType = &builtInTypes[tiSmmInt32];
	block->scope = parser->curScope;
	program->next = (PSmmAstNode)block;
	PSmmAstNode* nextStmt = &block->stmts;

	PSmmAstNode curStmt = NULL;
	while (parser->curToken->kind != tkSmmEof) {
		curStmt = parseStatement(parser);
		if (curStmt != NULL && curStmt != &errorNode) {
			*nextStmt = curStmt;
			nextStmt = &curStmt->next;
		}
	}

	bool isReturnMissing = true;
	if (curStmt) {
		if (curStmt->kind == nkSmmBlock) isReturnMissing = !curStmt->asBlock.endsWithReturn;
		else isReturnMissing = curStmt->kind != nkSmmReturn;
	}
	// Add return stmt if missing
	if (isReturnMissing) {
		curStmt = smmNewAstNode(nkSmmReturn, parser->a);
		struct SmmFilePos fp = parser->curToken->filePos;
		fp.lineNumber++;
		fp.lineOffset = 0;
		curStmt->token = newToken(tkSmmReturn, "return", fp, parser->a);
		curStmt->type = parser->curScope->returnType;
		curStmt->left = smmGetZeroValNode(parser->curToken->filePos, curStmt->type, parser->a);
		*nextStmt = curStmt;
	}

	program->token = ibsAlloc(parser->a, sizeof(struct SmmToken));
	program->token->repr = parser->lex->filePos.filename;
	return program;
}
