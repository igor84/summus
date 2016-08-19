#include "smmutil.h"
#include "smmmsgs.h"
#include "smmlexer.h"
#include "smmparser.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

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
};

static struct SmmTypeInfo builtInTypes[] = {
	{ tiSmmUnknown, 0, "/unknown/"}, { tiSmmBool, 1, "bool", 0, 0, 0, 1 },
	{ tiSmmUInt8, 1, "uint8", 1, 1 }, { tiSmmUInt16, 2, "uint16", 1, 1 },
	{ tiSmmUInt32, 4, "uint32", 1, 1 }, { tiSmmUInt64, 8, "uint64", 1, 1 },
	{ tiSmmInt8, 1, "int8", 1 }, { tiSmmInt16, 2, "int16", 1 },
	{ tiSmmInt32, 4, "int32", 1 }, { tiSmmInt64, 8, "int64", 1 },
	{ tiSmmFloat32, 4, "float32", 0, 0, 1 }, { tiSmmFloat64, 8, "float64", 0, 0, 1 },
	{ tiSmmSoftFloat64, 8, "/sfloat64/", 0, 0, 1 },
};

// We define temporary needed node kinds with huge start value so they don't overlap with real node kinds
enum {
	nkSmmParamDefinition = 100000
};

static PSmmBinaryOperator binOpPrecs[128] = { 0 };

static struct SmmAstNode errorNode = { nkSmmError, 0, 0, 0, NULL, &builtInTypes[0] };

/********************************************************
Private Functions
*********************************************************/

static PSmmAstNode parseExpression(PSmmParser parser);
static PSmmAstNode parseStatement(PSmmParser parser);

static void* newAstNode(SmmAstNodeKind kind, PSmmAllocator a) {
	PSmmAstNode res = a->alloc(a, sizeof(struct SmmAstNode));
	res->kind = kind;
	return res;
}

static PSmmToken newToken(int kind, const char* repr, struct SmmFilePos filePos, PSmmAllocator a) {
	PSmmToken res = a->alloc(a, sizeof(struct SmmToken));
	res->kind = kind;
	res->repr = repr;
	res->filePos = filePos;
	return res;
}

static PSmmAstScopeNode newScopeNode(PSmmParser parser) {
	PSmmAstScopeNode scope = newAstNode(nkSmmScope, parser->allocator);
	scope->level = parser->curScope->level + 1;
	scope->prevScope = parser->curScope;
	scope->lastDecl = (PSmmAstNode)scope;
	scope->returnType = parser->curScope->returnType;
	parser->curScope = scope;
	return scope;
}

static PSmmTypeInfo deduceTypeFrom(PSmmAstNode val) {
	// If right value is just another variable or func call just copy its type
	// but if it is expression then try to be a bit smarter.
	if (val->kind == nkSmmIdent || val->kind == nkSmmParam || val->kind == nkSmmCall || !val->type) {
		return val->type;
	} else {
		switch (val->type->kind) {
		case tiSmmSoftFloat64: return &builtInTypes[tiSmmFloat32];
		case tiSmmInt8: case tiSmmInt16: return &builtInTypes[tiSmmInt32];
		case tiSmmUInt8: case tiSmmUInt16: return &builtInTypes[tiSmmUInt32];
		default: return val->type;
		}
	}
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
 * Tries to find one of the given token until it comes to a terminating token.
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

static PSmmToken expect(PSmmParser parser, int type) {
	PSmmToken token = parser->curToken;
	if (token->kind != type) {
		if (token->kind != tkSmmErr && token->filePos.lineNumber != parser->lastErrorLine) {
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
		}
		parser->lastErrorLine = token->filePos.lineNumber;
		return NULL;
	}
	getNextToken(parser);
	return token;
}

#define FUNC_SIGNATURE_LENGTH 4 * 1024

static char* getFuncsSignatureAsString(PSmmAstFuncDefNode funcs, char* buf) {
	PSmmAstFuncDefNode curFunc = funcs;
	size_t len = 0;
	while (curFunc) {
		size_t l = strlen(curFunc->token->repr);
		strncpy(&buf[len], curFunc->token->repr, l);
		len += l;
		buf[len++] = '(';
		PSmmAstParamNode curParam = curFunc->params;
		while (curParam) {
			l = strlen(curParam->type->name);
			strncpy(&buf[len], curParam->type->name, l);
			len += l;
			buf[len++] = ',';
			curParam = curParam->next;
		}
		if (buf[len - 1] != '(') len--;
		buf[len++] = ')';
		buf[len++] = '\n';
		curFunc = curFunc->nextOverload;
	}
	buf[len - 1] = 0;
	return buf;
}

static char* getFuncCallAsString(const char* name, PSmmAstNode args, char* buf) {
	size_t len = strlen(name);
	strncpy(buf, name, len);
	buf[len++] = '(';
	PSmmAstNode curArg = args;
	while (curArg) {
		size_t l = strlen(curArg->type->name);
		strncpy(&buf[len], curArg->type->name, l);
		len += l;
		buf[len++] = ',';
		curArg = curArg->next;
	}
	if (buf[len - 1] != '(') len--;
	buf[len] = ')';
	return buf;
}

static bool isUpcastPossible(PSmmTypeInfo srcType, PSmmTypeInfo dstType) {
	bool bothInts = dstType->isInt && srcType->isInt && (dstType->isUnsigned == srcType->isUnsigned);
	bool bothFloats = dstType->isFloat && srcType->isFloat;
	bool floatAndSoftFloat = srcType->kind == tiSmmSoftFloat64 && dstType->isFloat;
	bool sameKindAndDstBigger = floatAndSoftFloat || ((bothInts || bothFloats) && (dstType->kind > srcType->kind));
	bool intToFloat = srcType->isInt && dstType->isFloat;
	return sameKindAndDstBigger || intToFloat;
}

static PSmmAstFuncDefNode findFuncWithMatchingParams(PSmmAstNode argNode, PSmmAstFuncDefNode curFunc, bool softMatch) {
	PSmmAstFuncDefNode softFunc = NULL;
	while (curFunc) {
		PSmmAstNode curArg = argNode;
		PSmmAstParamNode curParam = curFunc->params;
		PSmmAstFuncDefNode tmpSoftFunc = NULL;
		while (curParam && curArg) {
			bool differentTypes = curParam->type->kind != curArg->type->kind;
			if (differentTypes) {
				if (isUpcastPossible(curArg->type, curParam->type)) {
					tmpSoftFunc = curFunc;
				} else {
					tmpSoftFunc = NULL;
					break;
				}
			}
			curParam = curParam->next;
			curArg = curArg->next;
		}
		if (!curParam && !curArg && !tmpSoftFunc) {
			break;
		} else {
			if (tmpSoftFunc) softFunc = tmpSoftFunc;
			curFunc = curFunc->nextOverload;
		}
	}
	if (curFunc) return curFunc;
	if (!softMatch) return NULL;
	return softFunc;
}

/**
 * When func node is read from identDict it is linked with other overloaded funcs (funcs
 * with same name but different parameters) over the nextOverload pointer. Each func node
 * has a list of params nodes. The given node also has a list of concrete args with which
 * it is called. This function goes through all overloaded funcs and tries to match given
 * arguments with each function's parameters. If exact match is not found but a match
 * where some arguments can be upcast to a bigger type of the same kind (like from int8
 * to int32 but not to uint32) that func will be used. If there are multiple such funcs
 * we will say that it is undefined which one will be called (because compiler
 * implementation can change) and that explicit casts should be used in such cases.
 *
 * Example:
 * func : (int32, float64, bool) -> int8;
 * func : (int32, float32) -> int16;
 * func(1000, 54.234, true);
 * Received node:                      ___node___
 *                              ___func          int16___
 *                      ___int32     |                   softFloat64___
 *               float32        ___func                                bool
 *                      ___int32
 *            ___float64
 *        bool
 * Output node:                        ___node___
 *                             ___int32          int16___
 *                   ___float64                          softFloat64___
 *               bool                                                  bool
 */
static PSmmAstNode resolveCall(PSmmAstCallNode node, PSmmAstFuncDefNode curFunc) {
	PSmmAstFuncDefNode foundFunc = findFuncWithMatchingParams(node->args, curFunc, true);
	if (foundFunc) {
		node->returnType = foundFunc->returnType;
		node->params = foundFunc->params;
		node->token->stringVal = foundFunc->token->stringVal; // Copy mangled name
		return (PSmmAstNode)node;
	}
	// Report error that we got a call with certain arguments but expected one of...
	char callWithArgsBuf[FUNC_SIGNATURE_LENGTH] = { 0 };
	char funcSignatures[8 * FUNC_SIGNATURE_LENGTH] = { 0 };
	char* callWithArgs = getFuncCallAsString(node->token->repr, node->args, callWithArgsBuf);
	char* signatures = getFuncsSignatureAsString(curFunc, funcSignatures);
	smmPostMessage(errSmmGotSomeArgsButExpectedOneOf, node->token->filePos, callWithArgs, signatures);
	return &errorNode;
}

static PSmmAstNode createNewIdent(PSmmParser parser, PSmmToken identToken) {
	if (!identToken->canBeNewSymbol) {
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", "':'");
		return &errorNode;
	}
	PSmmAstIdentNode var = newAstNode(nkSmmIdent, parser->allocator);
	var->isIdent = true;
	var->token = identToken;
	var->level = parser->curScope->level;
	return (PSmmAstNode)var;
}

static PSmmAstNode parseIdentFactor(PSmmParser parser) {
	PSmmAstNode res = &errorNode;
	PSmmToken identToken = parser->curToken;
	getNextToken(parser);
	PSmmAstIdentNode var = smmGetDictValue(parser->idents, identToken->repr, false);

	if (parser->curToken->kind == ':') {
		// This is declaration
		if (var) {
			if (var->kind == nkSmmType) {
				// If type identifier was used as variable identifier
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(errSmmIdentTaken, identToken->filePos, identToken->repr, tokenStr);
			} else if (var->level < parser->curScope->level) {
				res = createNewIdent(parser, identToken);
			} else if (var->kind == nkSmmFunc) {
				//Posible overload
				res = createNewIdent(parser, identToken);
			} else {
				smmPostMessage(errSmmRedefinition, identToken->filePos, identToken->repr);
			}
		} else {
			res = createNewIdent(parser, identToken);
		}
	} else if (parser->curToken->kind == '(') {
		getNextToken(parser);
		if (var) {
			if (var->kind == nkSmmType) {
				// This is a cast
				PSmmAstNode expr = parseExpression(parser);
				if (expr == &errorNode) {
					if (findToken(parser, ')')) getNextToken(parser);
					return expr;
				}
				expect(parser, ')');
				res = newAstNode(nkSmmCast, parser->allocator);
				res->left = expr;
				res->token = identToken;
				res->type = var->type;
				res->isConst = expr->isConst;
			} else if (var->kind == nkSmmFunc) {
				PSmmAstCallNode resCall = newAstNode(nkSmmCall, parser->allocator);
				*resCall = *(PSmmAstCallNode)var;
				resCall->kind = nkSmmCall;
				resCall->token = identToken;
				if (parser->curToken->kind != ')') {
					PSmmAstNode lastArg = parseExpression(parser);
					resCall->args = lastArg;
					if (lastArg == &errorNode) resCall->kind = nkSmmError;
					while (parser->curToken->kind == ',') {
						getNextToken(parser);
						lastArg->next = parseExpression(parser);
						lastArg = lastArg->next;
						if (lastArg == &errorNode) resCall->kind = nkSmmError;
					}
				}
				if (resCall->kind == nkSmmError) {
					findToken(parser, ')');
				}
				if (expect(parser, ')') && resCall->kind != nkSmmError) {
					res = resolveCall(resCall, (PSmmAstFuncDefNode)var);
					res->isConst = false; // We need to reset this because 1 is copied from func def node
				}
				// Otherwise res remains equal to &errorNode
			} else {
				smmPostMessage(errSmmNotAFunction, identToken->filePos, identToken->repr);
				if (findToken(parser, ')')) getNextToken(parser);
			}
		} else {
			smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
			if (findToken(parser, ')')) getNextToken(parser);
		}
	} else {
		if (var) {
			if (var->kind == nkSmmType || !var->isIdent) {
				// if type or keyword is used in place of variable
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(errSmmIdentTaken, identToken->filePos, identToken->repr, tokenStr);
			} else {
				res = newAstNode(nkSmmIdent, parser->allocator);
				*res = *(PSmmAstNode)var;
				res->token = identToken;
			}
		} else {
			smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
		}
	}

	return res;
}

static PSmmAstNode parseFuncParams(PSmmParser parser, PSmmAstParamNode firstParam) {
	assert(firstParam != NULL);
	assert(parser->curToken->kind == ':');
	
	getNextToken(parser); //skip ':'

	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind != tkSmmIdent) {
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "type", got);
		}
		if (!findEitherToken(parser, ',', ')')) return &errorNode;
		typeInfo = &builtInTypes[tiSmmUnknown];
	} else {
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = &builtInTypes[tiSmmUnknown];
		} else {
			typeInfo = typeInfoNode->type;
		}
		getNextToken(parser);
	}

	int paramCount = 1;
	firstParam->kind = nkSmmParamDefinition;
	firstParam->type = typeInfo;
	firstParam->isIdent = true;
	firstParam->level = parser->curScope->level + 1;
	smmPushDictValue(parser->idents, firstParam->token->repr, firstParam);

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
		PSmmToken paramTypeToken = expect(parser, tkSmmIdent);
		if (!paramTypeToken) {
			findEitherToken(parser, ',', ')');
			continue;
		}
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, paramTypeToken->repr, false);
		PSmmTypeInfo paramTypeInfo = NULL;
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, paramTypeToken->filePos, paramTypeToken->repr);
			paramTypeInfo = &builtInTypes[tiSmmUnknown];
		} else {
			paramTypeInfo = typeInfoNode->type;
		}
		PSmmAstParamNode newParam = smmGetDictValue(parser->idents, paramName->repr, false);
		if (newParam) {
			if (newParam->level == parser->curScope->level + 1) {
				smmPostMessage(errSmmRedefinition, paramName->filePos, paramName->repr);
				continue;
			} else if (!newParam->isIdent) {
				const char* tokenStr = nodeKindToString[newParam->kind];
				smmPostMessage(errSmmIdentTaken, paramName->filePos, paramName->repr, tokenStr);
				continue;
			}
		}
		paramCount++;
		newParam = newAstNode(nkSmmParam, parser->allocator);
		newParam->isIdent = true;
		newParam->level = parser->curScope->level + 1;
		newParam->token = paramName;
		newParam->type = paramTypeInfo;
		smmPushDictValue(parser->idents, paramName->repr, newParam);
		param->next = newParam;
		param = newParam;
	}

	firstParam->count = paramCount;

	return (PSmmAstNode)firstParam;
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
	default:
		assert(false && "Got literal of unknown kind!");
		return &builtInTypes[tiSmmUnknown];
	}
}

static PSmmAstNode getLiteralNode(PSmmParser parser) {
	PSmmAstNode res = newAstNode(nkSmmError, parser->allocator);
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

static PSmmToken getUnaryOperator(PSmmParser parser) {
	PSmmToken res;
	switch (parser->curToken->kind) {
	case '!':
		smmPostMessage(errSmmBangUsedAsNot, parser->curToken->filePos);
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
				PSmmAstParamNode param = newAstNode(nkSmmParamDefinition, parser->allocator);
				param->count = 0;
				return (PSmmAstNode)param;
			}
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "expression", "')'");
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
			res = parseFuncParams(parser, (PSmmAstParamNode)res);
		}
		if (!expect(parser, ')')) {
			int tk = parser->curToken->kind;
			if (res->kind != nkSmmParamDefinition || (tk != tkSmmRArrow && tk != '{' && tk != ';')) {
				if (findToken(parser, ')')) getNextToken(parser);
				return &errorNode;
			}
			// If res is param and we encounter '->', '{' or ';' we assume this is func def
			// with forgoten ')' so we continue parsing under that assumption.
		}
	} else {
		switch (parser->curToken->kind) {
		case tkSmmIdent: res = parseIdentFactor(parser); break;
		case tkSmmUInt: case tkSmmFloat: case tkSmmBool:
			res = getLiteralNode(parser);
			break;
		default: 
			if (parser->curToken->kind != tkSmmErr) {
				char gotBuf[4];
				const char* got = smmTokenToString(parser->curToken, gotBuf);
				smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "identifier or literal", got);
			}
			break;
		}
	}

	if (res == &errorNode || !unary) return res;

	switch (unary->kind) {
	case '-':
		if (res->kind == nkSmmInt) {
			res->token->kind = tkSmmInt;
			res->token->sintVal = -(int64_t)res->token->uintVal;
			switch (res->type->kind) {
			case tiSmmInt16: if (res->token->sintVal == INT8_MIN) res->type = &builtInTypes[tiSmmInt8]; break;
			case tiSmmInt32: if (res->token->sintVal == INT16_MIN) res->type = &builtInTypes[tiSmmInt16]; break;
			case tiSmmInt64: if (res->token->sintVal == INT32_MIN) res->type = &builtInTypes[tiSmmInt32]; break;
			case tiSmmUInt64:
				if (res->token->sintVal != INT64_MIN) {
					smmPostMessage(wrnSmmConversionDataLoss,
						res->token->filePos,
						res->type->name,
						builtInTypes[tiSmmInt64].name);
				}
				res->type = &builtInTypes[tiSmmInt64];
				break;
			default: break;
			}
		} else if (res->kind == nkSmmFloat) {
			res->token->floatVal = -res->token->floatVal;
		} else {
			PSmmAstNode neg = newAstNode(nkSmmNeg, parser->allocator);
			neg->left = res;
			neg->type = res->type;
			if (neg->type->isUnsigned) {
				neg->type = &builtInTypes[neg->type->kind - tiSmmUInt8 + tiSmmInt8];
			} else if (neg->type->kind == tiSmmBool) {
				neg->type = &builtInTypes[tiSmmInt32]; // Sem pass should handle this
			}
			neg->isConst = res->isConst;
			neg->token = unary;
			res = neg;
		}
		break;
	case tkSmmNot:
		{
			PSmmAstNode not = newAstNode(nkSmmNot, parser->allocator);
			not->left = res;
			not->type = &builtInTypes[tiSmmBool];
			not->isConst = res->isConst;
			not->token = unary;
			res = not;
			break;
		}
	}
	return res;
}

static PSmmAstNode parseBinOp(PSmmParser parser, PSmmAstNode left, int prec) {
	while (true) {
		PSmmBinaryOperator binOp = binOpPrecs[parser->curToken->kind & 0x7f];
		if (!binOp || binOp->precedence < prec) {
			return left;
		}

		PSmmToken opToken = parser->curToken;

		getNextToken(parser);
		PSmmAstNode right = parseFactor(parser);
		if (right == &errorNode) return &errorNode;

		PSmmBinaryOperator nextBinOp = binOpPrecs[parser->curToken->kind & 0x7f];
		if (nextBinOp && nextBinOp->precedence > binOp->precedence) {
			right = parseBinOp(parser, right, binOp->precedence + 1);
			if (right == &errorNode) return &errorNode;
		}

		PSmmAstNode res = newAstNode(nkSmmError, parser->allocator);
		res->left = left;
		res->right = right;
		res->token = opToken;
		res->isConst = left->isConst && right->isConst;
		res->isBinOp = true;
		left = binOp->setupNode(parser, res);
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
	PSmmAstNode curDecl = parser->curScope->decls;
	while (curDecl) {
		smmPopDictValue(parser->idents, curDecl->left->token->repr);
		curDecl = curDecl->next;
	}
	PSmmAstScopeNode prevScope = parser->curScope->prevScope;
	if (prevScope->returnType == &builtInTypes[tiSmmUnknown]) {
		// In case we used return statement inside the block to guess func return type
		prevScope->returnType = parser->curScope->returnType;
	}
	parser->curScope = prevScope;
}

static PSmmAstBlockNode parseBlock(PSmmParser parser, PSmmTypeInfo curFuncReturnType, bool isFuncBlock) {
	assert(parser->curToken->kind == '{');
	getNextToken(parser); // Skip '{'
	PSmmAstBlockNode block = newAstNode(nkSmmBlock, parser->allocator);
	block->scope = newScopeNode(parser);
	block->scope->returnType = curFuncReturnType;
	PSmmAstNode* nextStmt = &block->stmts;
	PSmmAstNode curStmt = NULL;
	while (parser->curToken->kind != tkSmmEof && parser->curToken->kind != '}') {
		if (curStmt && curStmt->kind == nkSmmReturn) {
			smmPostMessage(errSmmUnreachableCode, parser->curToken->filePos);
		}
		curStmt = parseStatement(parser);
		if (curStmt != NULL && curStmt != &errorNode) {
			*nextStmt = curStmt;
			nextStmt = &curStmt->next;
		}
	}

	block->endsWithReturn = curStmt && (curStmt->kind == nkSmmReturn || (curStmt->kind == nkSmmBlock && ((PSmmAstBlockNode)curStmt)->endsWithReturn));

	if (isFuncBlock) {
		if (curFuncReturnType && curFuncReturnType->kind != tiSmmUnknown && !block->endsWithReturn && curStmt != &errorNode) {
			smmPostMessage(errSmmFuncMustReturnValue, parser->curToken->filePos);
		} else if ((!curFuncReturnType || curFuncReturnType->kind == tiSmmUnknown) && !block->endsWithReturn) {
			PSmmAstNode retNode = newAstNode(nkSmmReturn, parser->allocator);
			retNode->token = newToken(tkSmmReturn, "return", parser->curToken->filePos, parser->allocator);
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
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "one of '->', '{' or ';'", got);
		}
		if (!parser->curToken->isFirstOnLine) {
			do {
				getNextToken(parser);
				curKind = parser->curToken->kind;
			} while (curKind != tkSmmRArrow && curKind != '{' && curKind != ';' && curKind != tkSmmEof);
		} 
		// Otherwise assume ';' was forgotten
		ignoreMissingSemicolon = true;
	}
	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind == tkSmmRArrow) {
		ignoreMissingSemicolon = false;
		getNextToken(parser);
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			if (parser->curToken->kind != tkSmmErr) {
				smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			}
			typeInfo = &builtInTypes[tiSmmUnknown]; // We may try to guess return type later if there is a body
		} else {
			typeInfo = typeInfoNode->type;
		}
		if (parser->curToken->kind != '{') getNextToken(parser);
	}
	func->returnType = typeInfo;
	if (parser->curToken->kind == '{') {
		func->body = parseBlock(parser, typeInfo, true);
		if (func->returnType == &builtInTypes[tiSmmUnknown]) {
			func->returnType = func->body->scope->returnType;
		}
	} else if (parser->curToken->kind != ';') {
		if (!ignoreMissingSemicolon && parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "{ or ;", got);
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
		smmPopDictValue(parser->idents, param->token->repr);
		param = param->next;
	}
	return (PSmmAstNode)func;
}

static PSmmAstNode getZeroValNode(PSmmParser parser, PSmmTypeInfo varType) {
	PSmmAstNode zero = newAstNode(nkSmmInt, parser->allocator);
	zero->isConst = true;
	zero->type = varType;
	zero->token = parser->allocator->alloc(parser->allocator, sizeof(struct SmmToken));
	zero->token->filePos = parser->curToken->filePos;
	if (varType->isInt || varType->kind == tiSmmUnknown) {
		//Unknown type means there was some earlier error so we just default to int 0
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

static PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	bool prevTokenIsType = (parser->prevToken->kind == tkSmmIdent && parser->prevToken != lval->token);
	bool isDecl = (parser->prevToken->kind == ':' || prevTokenIsType);
	bool isTopLevelDecl = parser->curScope->level == 0 && isDecl;

	PSmmToken eqToken = parser->curToken;
	if (lval->kind == nkSmmConst && eqToken->kind == '=') {
		smmPostMessage(errCantAssignToConst, eqToken->filePos);
	}
	getNextToken(parser);
	PSmmAstNode val = parseExpression(parser);
	if (lval != &errorNode && !lval->type) {
		lval->type = deduceTypeFrom(val);
	}
	if (lval == &errorNode || val == &errorNode) {
		findToken(parser, ';');
		return &errorNode;
	}
	// If lval is const or global var which just needs initializer we just return the val directly.
	if (lval->kind == nkSmmConst || (val->isConst && isTopLevelDecl)) return val;
	
	// Otherwise we will actually need assignment statement
	PSmmAstNode assignment = newAstNode(nkSmmAssignment, parser->allocator);
	assignment->left = lval;
	assignment->right = val;
	assignment->type = lval->type;
	assignment->token = eqToken;
	return assignment;
}

static char* getMangledName(PSmmAstFuncDefNode func, PSmmAllocator a) {
	size_t count = 1;
	PSmmAstParamNode param = func->params;
	if (param) count += param->count;
	size_t* lengths = a->alloca(a, count * sizeof(size_t));
	int part = 0;
	size_t totalLen = 0;
	lengths[part] = strlen(func->token->repr);
	totalLen += lengths[part++] + 1;
	while (param) {
		lengths[part] = strlen(param->type->name);
		totalLen += lengths[part++] + 1;
		param = param->next;
	}

	char* buf = a->alloc(a, totalLen);
	part = 0;
	size_t pos = lengths[part++];
	memcpy(buf, func->token->repr, pos);
	param = func->params;
	while (param) {
		buf[pos++] = '_';
		memcpy(&buf[pos], param->type->name, lengths[part]);
		pos += lengths[part];
		part++;
		param = param->next;
	}
	a->freea(a, lengths);
	return buf;
}

static PSmmAstNode parseDeclaration(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken declToken = parser->curToken;
	assert(parser->curToken->kind == ':');
	getNextToken(parser);

	bool typeErrorReported = false;
	PSmmAstNode typeInfo = NULL;
	if (parser->curToken->kind == tkSmmIdent) {
		//Type is given in declaration so use it
		typeInfo = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfo || typeInfo->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = NULL;
			typeErrorReported = true;
		}
		getNextToken(parser);
	}
	
	PSmmAstNode spareNode = NULL;
	PSmmAstNode expr = NULL;
	if (parser->curToken->kind == '=') {
		expr = parseAssignment(parser, lval);
		PSmmAstNode existing = smmGetDictValue(parser->idents, lval->token->repr, false);
		if (existing && ((PSmmAstIdentNode)existing)->level == parser->curScope->level) {
			// This can happen if existing is a function and only here we find out lval isn't overload
			smmPostMessage(errSmmRedefinition, lval->token->filePos, lval->token->repr);
			expr = &errorNode;
		} else {
			// even if expr returns error we want to add it to dict but its type might be unknown
			smmPushDictValue(parser->idents, lval->token->repr, lval);
		}
	} else if (parser->curToken->kind == ':') {
		PSmmToken constAssignToken = parser->curToken;
		lval->kind = nkSmmConst;
		lval->isConst = true;
		expr = parseAssignment(parser, lval);
		if (expr->kind == nkSmmParamDefinition) {
			expr->kind = nkSmmParam;
			if (parser->curScope->level > 0) {
				smmPostMessage(errSmmFuncUnderScope, lval->token->filePos, lval->token->repr);
			}
			lval->kind = nkSmmFunc;
			PSmmAstFuncDefNode func = (PSmmAstFuncDefNode)lval;
			func->params = (PSmmAstParamNode)expr;
			if (func->params->count == 0) {
				spareNode = expr;
				spareNode->kind = nkSmmDecl;
				func->params = NULL;
			}
			PSmmAstFuncDefNode overload = smmGetDictValue(parser->idents, func->token->repr, false);
			if (overload && overload->kind != nkSmmFunc) overload = NULL;
			PSmmAstFuncDefNode redefinition = NULL;
			if (overload) {
				redefinition = findFuncWithMatchingParams((PSmmAstNode)func->params, overload, false);
			}
			smmPushDictValue(parser->idents, lval->token->repr, lval);
			lval = parseFunction(parser, func);
			expr = NULL;
			if (redefinition) {
				smmPostMessage(errSmmFuncRedefinition, func->token->filePos);
			}
			if (parser->curScope->level > 0 || redefinition) {
				smmPopDictValue(parser->idents, lval->token->repr); // Remove func name from idents
				return NULL; // Skip adding the func to any scope
			}
			func->nextOverload = overload;
			func->token->stringVal = getMangledName(func, parser->allocator);
		} else if (expr != &errorNode && !expr->isConst) {
			PSmmAstNode existing = smmGetDictValue(parser->idents, lval->token->repr, false);
			if (existing && ((PSmmAstIdentNode)existing)->level == parser->curScope->level) {
				// This can happen if existing is a function and only here we find out lval isn't overload
				smmPostMessage(errSmmRedefinition, lval->token->filePos, lval->token->repr);
				expr = &errorNode;
			} else {
				smmPushDictValue(parser->idents, lval->token->repr, lval);
				smmPostMessage(errNonConstInConstExpression, constAssignToken->filePos);
				expr = NULL;
			}
		} else {
			PSmmAstNode existing = smmGetDictValue(parser->idents, lval->token->repr, false);
			if (existing && ((PSmmAstIdentNode)existing)->level == parser->curScope->level) {
				// This can happen if existing is a function and only here we find out lval isn't overload
				smmPostMessage(errSmmRedefinition, lval->token->filePos, lval->token->repr);
				expr = &errorNode;
			} else {
				smmPushDictValue(parser->idents, lval->token->repr, lval);
			}
		}
	} else if (parser->curToken->kind != ';') {
		expr = &errorNode;
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "':', '=' or type", got);
		}
		findToken(parser, ';');
	} else if (typeInfo == NULL) {
		expr = &errorNode;
		if (!typeErrorReported) {
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "type", "';'");
		}
	} else {
		smmPushDictValue(parser->idents, lval->token->repr, lval);
	}

	if (expr == &errorNode || lval == &errorNode) {
		if (spareNode) parser->allocator->free(parser->allocator, spareNode);
		return &errorNode;
	}

	PSmmAstNode decl = spareNode ? spareNode : newAstNode(nkSmmDecl, parser->allocator);
	decl->token = declToken;
	decl->left = lval;

	if (typeInfo) {
		// We do this here so that parseAssignment and parseExpression above
		// can detect usage of undeclared identifier in expressions like x : int8 = x + 1;
		lval->type = typeInfo->type;
		if (expr && expr->kind == nkSmmAssignment) {
			expr->type = lval->type;
		}
	}
	decl->type = lval->type;
	
	if (lval->kind != nkSmmFunc) {
		bool isConstExpr = expr && expr->isConst;

		if (parser->curScope->level == 0) {
			//This is global var or constant
			if (isConstExpr) {
				decl->right = expr;
				expr = NULL;
			} else {
				decl->right = getZeroValNode(parser, lval->type);
			}
		} else if (!expr) {
			if (lval->kind == nkSmmConst) {
				// We got variable expression so we just set 0 as val in order to parse the rest
				// without errors that this const is not defined
				decl->right = getZeroValNode(parser, lval->type);
			} else {
				// Local var without initializator, so we add stmt that will initialize it to 0
				expr = newAstNode(nkSmmAssignment, parser->allocator);
				expr->left = lval;
				expr->right = getZeroValNode(parser, lval->type);
				expr->type = lval->type;
				expr->token = parser->allocator->alloc(parser->allocator, sizeof(struct SmmToken));
				expr->token->kind = '=';
				expr->token->repr = "=";
				expr->token->filePos = parser->curToken->filePos;
			}
		} else if (lval->kind == nkSmmConst) {
			decl->right = expr;
			expr = NULL;
		}
	} // endif lval not func

	parser->curScope->lastDecl->next = decl;
	parser->curScope->lastDecl = decl;

	return expr;
}

static PSmmAstNode parseReturnStmt(PSmmParser parser) {
	assert(parser->curToken->kind == tkSmmReturn);

	PSmmAstNode lval;

	PSmmToken retToken = parser->curToken;
	getNextToken(parser);
	if (parser->curToken->kind != ';') {
		lval = parseExpression(parser);
		if (lval == &errorNode) {
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
		if (lval->type == NULL) {
			// This can happen if we use return funcThatReturnsNothing();
			smmPostMessage(errSmmInvalidExprUsed, lval->token->filePos);
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
		PSmmTypeInfo retType = parser->curScope->returnType;
		if (retType == &builtInTypes[tiSmmUnknown]) {
			// We had problems trying to parse func return type so now we will assume type
			// of this return stmt is return type of the function
			parser->curScope->returnType = deduceTypeFrom(lval);
		} else if (!retType) {
			smmPostMessage(errSmmNoReturnValueNeeded, retToken->filePos);
		} else if (lval->type != retType && !isUpcastPossible(lval->type, retType) && lval->type->kind != tiSmmUnknown) {
			PSmmTypeInfo ltype = lval->type;
			if (ltype == &builtInTypes[tiSmmSoftFloat64]) ltype -= 2;
			smmPostMessage(errSmmBadReturnStmtType, retToken->filePos, ltype->name, retType->name);
		}
	} else {
		lval = NULL;
		if (parser->curScope->returnType) {
			smmPostMessage(errSmmFuncMustReturnValue, retToken->filePos);
		}
	}
	PSmmAstNode res = newAstNode(nkSmmReturn, parser->allocator);
	res->type = parser->curScope->returnType;
	if (res->type) res->left = lval;
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
		smmPostMessage(errSmmOperandMustBeLVal, fpos);
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
			smmPostMessage(wrnSmmNoEffectStmt, lval->token->filePos);
			if (isJustIdent) lval = NULL;
		}
	}
	if (parser->prevToken->kind != '}' && (lval != &errorNode || parser->curToken->kind == ';')) {
		expect(parser, ';');
	}
	return lval;
}

static PSmmAstNode parseStatement(PSmmParser parser) {
	switch (parser->curToken->kind) {
	case tkSmmReturn: return parseReturnStmt(parser);
	case '{': return (PSmmAstNode)parseBlock(parser, parser->curScope->returnType, false);
	case tkSmmIdent: case '(': case '-': case '+': case tkSmmNot:
	case tkSmmInt: case tkSmmFloat: case tkSmmBool:
		return parseExpressionStmt(parser);
	case tkSmmErr:
		if (findToken(parser, ';')) getNextToken(parser);
		return NULL;
	case ';': return NULL; // Just skip empty statements
	default:
		if (parser->curToken->kind != tkSmmErr) {
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "valid statement", got);
		}
		getNextToken(parser); // Skip the bad character
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}
}

static PSmmTypeInfo getCommonTypeFromOperands(PSmmAstNode res) {
	PSmmTypeInfo type;
	if (res->left->type->isInt && res->right->type->isInt) {
		// if both are ints we need to select bigger type but if only one is signed result should be signed
		bool leftUnsigned = res->left->type->isUnsigned;
		bool rightUnsigned = res->right->type->isUnsigned;
		type = (res->left->type->sizeInBytes > res->right->type->sizeInBytes) ? res->left->type : res->right->type;
		if (leftUnsigned != rightUnsigned && (type->isUnsigned)) {
			type = &builtInTypes[type->kind - tiSmmUInt8 + tiSmmInt8];
		}
	} else {
		// Otherwise floats are always considered bigger then int
		type = (res->left->type->kind > res->right->type->kind) ? res->left->type : res->right->type;
		if (type->kind == tkSmmBool) type = &builtInTypes[tiSmmUInt8];
	}
	return type;
}

static void fixDivModOperandTypes(PSmmParser parser, PSmmAstNode res) {
	PSmmAstNode* goodField = NULL;
	PSmmAstNode* badField = NULL;
	if (res->left->type->isInt) {
		goodField = &res->left;
		badField = &res->right;
	} else if (res->right->type->isInt) {
		goodField = &res->right;
		badField = &res->left;
	}

	if (!goodField) {
		// Neither operand is int so we cast both to int32
		PSmmAstNode cast = newAstNode(nkSmmCast, parser->allocator);
		cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, res->left->token->filePos, parser->allocator);
		cast->left = res->left;
		res->left = cast;
		cast = newAstNode(nkSmmCast, parser->allocator);
		cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, res->right->token->filePos, parser->allocator);
		cast->left = res->right;
		res->right = cast;
	} else if ((*badField)->kind == nkSmmFloat) {
		// if bad node is literal we need to convert it
		PSmmAstNode bad = *badField;
		PSmmAstNode good = *goodField;
		bad->kind = nkSmmInt;
		bad->token->kind = tkSmmInt;
		if (bad->token->floatVal >= 0 && good->type->isUnsigned) {
			bad->token->uintVal = (uint64_t)bad->token->floatVal;
			bad->type = good->type;
		} else {
			bad->token->sintVal = (int64_t)bad->token->floatVal;
			if (good->type->sizeInBytes > 4) {
				bad->type = &builtInTypes[tiSmmInt64];
			} else {
				bad->type = &builtInTypes[tiSmmInt32];
			}
		}
	} else {
		// Otherwise we need to cast it
		PSmmAstNode cast = newAstNode(nkSmmCast, parser->allocator);
		cast->type = (*goodField)->type;
		if (cast->type->sizeInBytes < 4) cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, (*badField)->token->filePos, parser->allocator);
		cast->left = *badField;
		*badField = cast;
	}
}

/**
 * Checks if both operands have type and reports error if any is missing.
 * Returns true if any operand has type == NULL.
 */
static bool isAnyOperandBad(PSmmAstNode res) {
	PSmmToken badOperand = NULL;
	if (!res->left->type) badOperand = res->left->token;
	else if (!res->right->type) badOperand = res->right->token;
	if (badOperand) {
		char buf[4];
		smmPostMessage(errSmmBadOperandsType, badOperand->filePos, smmTokenToString(res->token, buf), "none");
		return true;
	}
	return false;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupMulDivModNode(PSmmParser parser, PSmmAstNode res) {
	if (res == &errorNode || isAnyOperandBad(res)) return res;
	PSmmTypeInfo type = getCommonTypeFromOperands(res);
	switch (res->token->kind) {
	case tkSmmIntDiv: case tkSmmIntMod: {
		res->kind = type->isUnsigned ? nkSmmUDiv : nkSmmSDiv;
		if (res->token->kind == tkSmmIntMod) res->kind += nkSmmSRem - nkSmmSDiv;
		res->type = type;
		if (type->kind >= tiSmmFloat32) {
			char buf[4];
			smmPostMessage(errSmmBadOperandsType, res->token->filePos, smmTokenToString(res->token, buf), type->name);
			fixDivModOperandTypes(parser, res);
			res->type = getCommonTypeFromOperands(res);
		}
		break;
	}
	case '*':
		if (type->isFloat) res->kind = nkSmmFMul;
		else res->kind = nkSmmMul;
		res->type = type;
		break;
	case '/':
		res->kind = nkSmmFDiv;
		res->type = type->kind < tiSmmFloat32 ? &builtInTypes[tiSmmSoftFloat64] : type;
		break;
	case '%':
		res->kind = nkSmmFRem;
		res->type = type->kind < tiSmmFloat32 ? &builtInTypes[tiSmmSoftFloat64] : type;
		break;
	default:
		assert(false && "Got unexpected token");
	}
	return res;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupAddSubNode(PSmmParser parser, PSmmAstNode res) {
	if (res == &errorNode || isAnyOperandBad(res)) return res;
	if (res->token->kind == '+') res->kind = nkSmmAdd;
	else res->kind = nkSmmSub;

	res->type = getCommonTypeFromOperands(res);

	if (res->type->kind >= tiSmmFloat32) res->kind++; // Add to FAdd, Sub to FSub

	return res;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupLogicOpNode(PSmmParser parser, PSmmAstNode res) {
	if (res == &errorNode || isAnyOperandBad(res)) return res;
	switch (res->token->kind)
	{
	case tkSmmAndOp: res->kind = nkSmmAndOp; break;
	case tkSmmXorOp: res->kind = nkSmmXorOp; break;
	case tkSmmOrOp: res->kind = nkSmmOrOp; break;
	default:
		assert(false && "Got unexpected token for LogicOp");
		break;
	}
	res->type = &builtInTypes[tiSmmBool];
	return res;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupRelOpNode(PSmmParser parser, PSmmAstNode res) {
	if (res == &errorNode || isAnyOperandBad(res)) return res;
	switch (res->token->kind)
	{
	case tkSmmEq: res->kind = nkSmmEq; break;
	case tkSmmNotEq: res->kind = nkSmmNotEq; break;
	case '>': res->kind = nkSmmGt; break;
	case tkSmmGtEq: res->kind = nkSmmGtEq; break;
	case '<': res->kind = nkSmmLt; break;
	case tkSmmLtEq: res->kind = nkSmmLtEq; break;
	default:
		assert(false && "Got unexpected token for LogicOp");
		break;
	}
	res->type = &builtInTypes[tiSmmBool];

	bool bothOperandsInts = res->left->type->isInt && res->right->type->isInt;
	if (!bothOperandsInts) return res;

	bool onlyOneSigned = res->left->type->isUnsigned != res->right->type->isUnsigned;
	if (!onlyOneSigned) return res;

	smmPostMessage(wrnSmmComparingSignedAndUnsigned, res->token->filePos);

	PSmmAstNode castNode = newAstNode(nkSmmCast, parser->allocator);
	castNode->isConst = res->isConst;
	castNode->type = getCommonTypeFromOperands(res);
	if (res->left->type->isUnsigned) {
		castNode->left = res->left;
		castNode->token = res->left->token;
		res->left = castNode;
	} else {
		castNode->left = res->right;
		castNode->token = res->right->token;
		res->right = castNode;
	}

	return res;
}

/********************************************************
API Functions
*********************************************************/

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator) {
	assert(nodeKindToString[nkSmmTerminator - 1]); //Check if names for all node kinds are defined
	PSmmParser parser = allocator->alloc(allocator, sizeof(struct SmmParser));
	parser->lex = lex;
	parser->curToken = smmGetNextToken(lex);
	parser->allocator = allocator;

	// Init idents dict
	parser->idents = smmCreateDict(parser->allocator, NULL, NULL);
	int cnt = sizeof(builtInTypes) / sizeof(struct SmmTypeInfo);
	for (int i = 0; i < cnt; i++) {
		PSmmAstNode typeNode = newAstNode(nkSmmType, parser->allocator);
		typeNode->type = &builtInTypes[i];
		smmAddDictValue(parser->idents, typeNode->type->name, typeNode);
	}

	smmAddDictValue(parser->idents, "int", smmGetDictValue(parser->idents, "int32", false));
	smmAddDictValue(parser->idents, "uint", smmGetDictValue(parser->idents, "uint32", false));
	smmAddDictValue(parser->idents, "float", smmGetDictValue(parser->idents, "float32", false));


	static struct SmmBinaryOperator mulDivModOp = { setupMulDivModNode, 120 };
	static struct SmmBinaryOperator relOp = { setupRelOpNode, 110 };
	static struct SmmBinaryOperator addSubOp = { setupAddSubNode, 100 };
	static struct SmmBinaryOperator andOp = { setupLogicOpNode, 90 };
	static struct SmmBinaryOperator orXorOp = { setupLogicOpNode, 80 };

	static bool binOpsInitialized = false;
	if (!binOpsInitialized) {
		// Init binary operator precedences. Index is tokenKind & 0x7f so its value must be less then 289
		// which is after this operation equal to '!', first operator character in ascii map

		binOpsInitialized = true;
		binOpPrecs['+'] = &addSubOp;
		binOpPrecs['-'] = &addSubOp;

		binOpPrecs['*'] = &mulDivModOp;
		binOpPrecs['/'] = &mulDivModOp;
		binOpPrecs[tkSmmIntDiv & 0x7f] = &mulDivModOp;
		binOpPrecs[tkSmmIntMod & 0x7f] = &mulDivModOp;

		binOpPrecs[tkSmmEq & 0x7f] = &relOp;
		binOpPrecs[tkSmmNotEq & 0x7f] = &relOp;
		binOpPrecs['>'] = &relOp;
		binOpPrecs[tkSmmGtEq & 0x7f] = &relOp;
		binOpPrecs['<'] = &relOp;
		binOpPrecs[tkSmmLtEq & 0x7f] = &relOp;

		binOpPrecs[tkSmmAndOp & 0x7f] = &andOp;
		binOpPrecs[tkSmmXorOp & 0x7f] = &orXorOp;
		binOpPrecs[tkSmmOrOp & 0x7f] = &orXorOp;
	}

	return parser;
}

PSmmAstNode smmParse(PSmmParser parser) {
	if (parser->curToken->kind == tkSmmEof) return NULL;
	PSmmAstNode program = newAstNode(nkSmmProgram, parser->allocator);
	PSmmAstBlockNode block = newAstNode(nkSmmBlock, parser->allocator);
	parser->curScope = newAstNode(nkSmmScope, parser->allocator);
	parser->curScope->lastDecl = (PSmmAstNode)parser->curScope;
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
		if (curStmt->kind == nkSmmBlock) isReturnMissing = !((PSmmAstBlockNode)curStmt)->endsWithReturn;
		else isReturnMissing = curStmt->kind != nkSmmReturn;
	}
	// Add return stmt if missing
	if (isReturnMissing) {
		curStmt = newAstNode(nkSmmReturn, parser->allocator);
		struct SmmFilePos fp = parser->curToken->filePos;
		fp.lineNumber++;
		fp.lineOffset = 0;
		curStmt->token = newToken(tkSmmReturn, "return", fp, parser->allocator);
		curStmt->type = parser->curScope->returnType;
		curStmt->left = getZeroValNode(parser, curStmt->type);
		*nextStmt = curStmt;
	}

	program->token = parser->allocator->alloc(parser->allocator, sizeof(struct SmmToken));
	program->token->repr = parser->lex->filePos.filename;
	return program;
}

PSmmTypeInfo smmGetBuiltInTypes() {
	return builtInTypes;
}