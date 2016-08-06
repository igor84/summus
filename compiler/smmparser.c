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
	{ tiSmmUnknown, 0, "/unknown/"}, { tiSmmBool, 1, "bool", tifSmmBool },
	{ tiSmmUInt8, 1, "uint8", tifSmmUnsignedInt }, { tiSmmUInt16, 2, "uint16", tifSmmUnsignedInt },
	{ tiSmmUInt32, 4, "uint32", tifSmmUnsignedInt }, { tiSmmUInt64, 8, "uint64", tifSmmUnsignedInt },
	{ tiSmmInt8, 1, "int8", tifSmmInt }, { tiSmmInt16, 2, "int16", tifSmmInt },
	{ tiSmmInt32, 4, "int32", tifSmmInt }, { tiSmmInt64, 8, "int64", tifSmmInt },
	{ tiSmmFloat32, 4, "float32", tifSmmFloat }, { tiSmmFloat64, 8, "float64", tifSmmFloat },
	{ tiSmmSoftFloat64, 8, "/sfloat64/", tifSmmFloat },
};

static PSmmBinaryOperator binOpPrecs[128] = { 0 };

static struct SmmAstNode errorNode = { nkSmmError, 0, NULL, &builtInTypes[0] };

/********************************************************
Private Functions
*********************************************************/

static PSmmAstNode parseExpression(PSmmParser parser);
static PSmmAstNode parseStatement(PSmmParser parser);

static void* newAstNode(PSmmParser parser, SmmAstNodeKind kind) {
	PSmmAstNode res = parser->allocator->alloc(parser->allocator, sizeof(struct SmmAstNode));
	res->kind = kind;
	return res;
}

static PSmmAstScopeNode newScopeNode(PSmmParser parser) {
	PSmmAstScopeNode scope = newAstNode(parser, nkSmmScope);
	scope->level = parser->curScope->level + 1;
	scope->prevScope = parser->curScope;
	scope->lastDecl = (PSmmAstNode)scope;
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
		|| tokenKind == ')' || tokenKind == tkSmmEof;
}

static bool findToken(PSmmParser parser, int tokenKind) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenKind && !isTerminatingToken(curKind)) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	return curKind == tokenKind;
}

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
		}
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
	bool bothInts = (dstType->flags == srcType->flags) && (srcType->flags | tifSmmInt);
	bool bothFloats = dstType->flags & srcType->flags & tifSmmFloat;
	bool floatAndSoftFloat = srcType->kind == tiSmmSoftFloat64 && (dstType->flags & tifSmmFloat);
	return floatAndSoftFloat || ((bothInts || bothFloats) && (dstType->kind > srcType->kind));
}

/**
 * When func node is read from identDict it is copied to a new node whose params pointer
 * is then setup to point to original func node and is given here. Original func node is
 * linked with other overloaded funcs (funcs with same name but different parameters) over
 * the nextOverload pointer. Each func node has a list of params nodes. The given node
 * also has a list of concrete args with which it is called. This function goes through
 * all overloaded funcs and tries to match given arguments with each function's parameters.
 * If exact match is not found but a match where some arguments can be upcast to a bigger
 * type of the same kind (like from int8 to int32 but not to uint32) that func will be
 * used. If there are multiple such funcs we will say that it is undefined which one will
 * be called (because compiler implementation can change) and that explicit casts should
 * be used in such cases.
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
static PSmmAstNode resolveCall(PSmmParser parser, PSmmAstCallNode node, PSmmAstFuncDefNode curFunc) {
	PSmmAstFuncDefNode softFunc = NULL;
	while (curFunc) {
		PSmmAstNode curArg = node->args;
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
	if (!curFunc && softFunc) curFunc = softFunc;
	if (curFunc) {
		node->returnType = curFunc->returnType;
		node->params = curFunc->params;
		return (PSmmAstNode)node;
	}
	// Report error that we got a call with certain arguments but expected one of...
	char callWithArgsBuf[FUNC_SIGNATURE_LENGTH] = { 0 };
	char funcSignatures[8 * FUNC_SIGNATURE_LENGTH] = { 0 };
	char* callWithArgs = getFuncCallAsString(node->token->repr, node->args, callWithArgsBuf);
	char* signatures = getFuncsSignatureAsString((PSmmAstFuncDefNode)node->params, funcSignatures);
	smmPostMessage(errSmmGotSomeArgsButExpectedOneOf, node->token->filePos, callWithArgs, signatures);
	return &errorNode;
}

static PSmmAstNode createNewIdent(PSmmParser parser, PSmmToken identToken) {
	PSmmAstIdentNode var = newAstNode(parser, nkSmmIdent);
	var->flags = nfSmmIdent;
	var->token = identToken;
	var->level = parser->curScope->level;
	smmPushDictValue(parser->idents, var->token->repr, var);
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
				smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
			} else if (!(var->flags & nfSmmIdent)) {
				// If variable name equals some keyword
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
			} else if (!var->type) {
				smmPostMessage(errSmmUndefinedIdentifier, parser->curToken->filePos, parser->curToken->repr);
			} else if (var->level < parser->curScope->level) {
				res = createNewIdent(parser, identToken);
			} else {
				smmPostMessage(errSmmRedefinition, identToken->filePos, identToken->repr);
				findToken(parser, ';');
			}
		} else {
			res = createNewIdent(parser, identToken);
		}
	} else if (parser->curToken->kind == '(') {
		if (var) {
			if (var->kind == nkSmmType) {
				// This is a cast
				getNextToken(parser);
				PSmmAstNode expr = parseExpression(parser);
				if (expr == &errorNode) return expr;
				expect(parser, ')');
				res = newAstNode(parser, nkSmmCast);
				res->left = expr;
				res->token = identToken;
				res->type = var->type;
				res->flags = expr->flags & nfSmmConst;
			} else if (var->kind == nkSmmFunc) {
				getNextToken(parser);
				PSmmAstCallNode resCall = newAstNode(parser, nkSmmCall);
				*resCall = *(PSmmAstCallNode)var;
				resCall->kind = nkSmmCall;
				resCall->token = identToken;
				// When read from identDict this contains func def body and since this
				// is a call node we don't need definition body so we set it to NULL
				resCall->zzNotUsed1 = NULL;
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
				if (expect(parser, ')') && resCall->kind != nkSmmError) {
					res = resolveCall(parser, resCall, (PSmmAstFuncDefNode)var);
				}
				// Otherwise res remains equal to &errorNode
			} else {
				smmPostMessage(errSmmNotAFunction, identToken->filePos, identToken->repr);
				findToken(parser, ';');
			}
		} else {
			smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
			findToken(parser, ';');
		}
	} else {
		if (var) {
			if (var->kind == nkSmmType || !(var->flags & nfSmmIdent)) {
				// if type or keyword is used in place of variable
				const char* tokenStr = nodeKindToString[var->kind];
				smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
				findToken(parser, ';');
			} else {
				res = newAstNode(parser, nkSmmIdent);
				*res = *(PSmmAstNode)var;
				res->token = identToken;
			}
		} else {
			smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
			findToken(parser, ';');
		}
	}

	return res;
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

static PSmmAstNode parseFuncParams(PSmmParser parser, PSmmAstParamNode firstParam) {
	assert(firstParam != NULL);
	assert(parser->curToken->kind == ':');

	if (firstParam->kind == nkSmmError) {
		findToken(parser, ';');
		return &errorNode;
	}

	if (firstParam->flags != nfSmmIdent) {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", got);
		findToken(parser, ';');
		return &errorNode;
	}

	getNextToken(parser); //skip ':'

	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind != tkSmmIdent) {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "type", got);
		int foundToken = findEitherToken(parser, ',', ')');
		if (!foundToken) return &errorNode;
		typeInfo = &builtInTypes[tiSmmUnknown];
	} else {
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = &builtInTypes[tiSmmUnknown];
		} else {
			typeInfo = typeInfoNode->type;
		}
	}

	int paramCount = 1;
	firstParam->kind = nkSmmParam;
	firstParam->type = typeInfo;
	firstParam->flags = nfSmmIdent;
	firstParam->level = parser->curScope->level + 1;

	PSmmAstParamNode param = firstParam;

	getNextToken(parser);
	while (parser->curToken->kind == ',') {
		paramCount++;
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
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			paramTypeInfo = &builtInTypes[tiSmmUnknown];
		} else {
			paramTypeInfo = typeInfoNode->type;
		}
		PSmmAstParamNode newParam = smmGetDictValue(parser->idents, paramName->repr, false);
		if (newParam) {
			if (newParam->kind == nkSmmParam && newParam->level == parser->curScope->level + 1) {
				smmPostMessage(errSmmRedefinition, paramName->filePos, paramName->repr);
				continue;
			} else if (!(newParam->flags & nfSmmIdent)) {
				const char* tokenStr = nodeKindToString[newParam->kind];
				smmPostMessage(errSmmIdentTaken, paramName->filePos, paramName->repr, tokenStr);
				continue;
			}
		}
		newParam = newAstNode(parser, nkSmmParam);
		newParam->flags = nfSmmIdent;
		newParam->level = parser->curScope->level + 1;
		newParam->token = paramName;
		newParam->type = paramTypeInfo;
		param->next = newParam;
		param = newParam;
	}

	firstParam->count = paramCount;

	return (PSmmAstNode)firstParam;
}

static PSmmAstNode getLiteralNode(PSmmParser parser) {
	PSmmAstNode res = newAstNode(parser, nkSmmError);
	res->type = getLiteralTokenType(parser->curToken);
	if (res->type->flags & tifSmmInt) res->kind = nkSmmInt;
	else if (res->type->flags & tifSmmFloat) res->kind = nkSmmFloat;
	else if (res->type->flags & tifSmmBool) res->kind = nkSmmBool;
	else assert(false && "Got unimplemented literal type");
	res->token = parser->curToken;
	res->flags = nfSmmConst;
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
			if (canBeFuncDefn) {
				PSmmAstParamNode param = newAstNode(parser, nkSmmParam);
				param->count = 0;
				return (PSmmAstNode)param;
			}
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "expression", "')'");
			findToken(parser, ';');
			return &errorNode;
		}
		res = parseExpression(parser);
		// In case expression is followed by ':' it might be it is just ident and thus first param of func declaration
		if (parser->curToken->kind == ':') {
			if (canBeFuncDefn) {
				res = parseFuncParams(parser, (PSmmAstParamNode)res);
			} else {
				smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", "':'");
				findToken(parser, ';');
				return &errorNode;
			}
		}
		if (!expect(parser, ')')) {
			int tk = parser->curToken->kind;
			if (res->kind != nkSmmParam || (tk != tkSmmRArrow && tk != '{' && tk != ';')) {
				findToken(parser, ';');
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
		if (res == &errorNode) findToken(parser, ';');
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
			PSmmAstNode neg = newAstNode(parser, nkSmmNeg);
			neg->left = res;
			neg->type = res->type;
			if (neg->type->flags & tifSmmUnsigned) {
				neg->type = &builtInTypes[neg->type->kind - tiSmmUInt8 + tiSmmInt8];
			}
			neg->flags |= res->flags & nfSmmConst;
			res = neg;
		}
		break;
	case tkSmmNot:
		{
			PSmmAstNode not = newAstNode(parser, nkSmmNot);
			not->left = res;
			not->type = &builtInTypes[tiSmmBool];
			not->flags |= res->flags & nfSmmConst;
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

		PSmmToken resToken = parser->curToken;

		getNextToken(parser);
		PSmmAstNode right = parseFactor(parser);
		if (right == &errorNode) {
			return right;
		}

		PSmmBinaryOperator nextBinOp = binOpPrecs[parser->curToken->kind & 0x7f];
		if (nextBinOp && nextBinOp->precedence > binOp->precedence) {
			right = parseBinOp(parser, right, binOp->precedence + 1);
			if (right == &errorNode) {
				return right;
			}
		}

		PSmmAstNode res = newAstNode(parser, nkSmmError);
		res->left = left;
		res->right = right;
		res->token = resToken;
		res->flags = left->flags & right->flags & nfSmmConst;
		res->flags |= nfSmmBinOp;
		left = binOp->setupNode(parser, res);
	}
}

static PSmmAstNode parseExpression(PSmmParser parser) {
	PSmmAstNode left = parseFactor(parser);
	if (left != &errorNode) {
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
	if (prevScope->returnType && prevScope->returnType->kind == tiSmmUnknown) {
		// In case we used return statement inside the block to guess func return type
		prevScope->returnType = parser->curScope->returnType;
	}
	parser->curScope = prevScope;
}

static PSmmAstBlockNode parseBlock(PSmmParser parser, PSmmTypeInfo curFuncReturnType, bool isFuncBlock) {
	assert(parser->curToken->kind == '{');
	getNextToken(parser); // Skip '{'
	PSmmAstBlockNode block = newAstNode(parser, nkSmmBlock);
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

	if (isFuncBlock && curFuncReturnType && curStmt && curStmt->kind != nkSmmReturn) {
		smmPostMessage(errSmmFuncMustReturnValue, parser->curToken->filePos);
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
	int curKind = parser->curToken->kind;
	if (curKind != tkSmmRArrow && curKind != '{' && curKind != ';') {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "one of '->', '{' or ';'", got);
		do {
			getNextToken(parser);
			curKind = parser->curToken->kind;
		} while (curKind != tkSmmRArrow && curKind != '{' && curKind != ';');

	}
	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind == tkSmmRArrow) {
		getNextToken(parser);
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = &builtInTypes[tiSmmUnknown]; // We may try to guess return type later if there is a body
		} else {
			typeInfo = typeInfoNode->type;
		}
		getNextToken(parser);
	}
	func->returnType = typeInfo;
	if (parser->curToken->kind == '{') {
		PSmmAstParamNode param = func->params;
		while (param) {
			smmPushDictValue(parser->idents, param->token->repr, param);
			param = param->next;
		}
		func->body = parseBlock(parser, typeInfo, true);
		if (func->returnType && func->returnType->kind == tiSmmUnknown) {
			func->returnType = func->body->scope->returnType;
		}
		param = func->params;
		while (param) {
			smmPopDictValue(parser->idents, param->token->repr);
			param = param->next;
		}
	} else if (parser->curToken->kind != ';') {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "{ or ;", got);
		if (!parser->curToken->isFirstOnLine) {
			// if illegal token is in the same line then we will skip all until terminating token
			findToken(parser, ';');
		}
		// Otherwise we assume ';' is forgotten so we don't do findToken here hoping normal stmt starts next
		return &errorNode;
	}
	return (PSmmAstNode)func;
}

static PSmmAstNode getZeroValNode(PSmmParser parser, PSmmTypeInfo varType) {
	PSmmAstNode zero = newAstNode(parser, nkSmmInt);
	zero->flags = nfSmmConst;
	zero->type = varType;
	zero->token = parser->allocator->alloc(parser->allocator, sizeof(struct SmmToken));
	zero->token->filePos = parser->curToken->filePos;
	if (varType->flags & tifSmmInt) {
		zero->token->kind = tkSmmInt;
		zero->token->repr = "0";
	} else if (varType->flags & tifSmmFloat) {
		zero->kind = nkSmmFloat;
		zero->token->kind = tkSmmFloat;
		zero->token->repr = "0";
	} else if (varType->flags & tifSmmBool) {
		zero->kind = nkSmmBool;
		zero->token->kind = tkSmmBool;
		zero->token->repr = "false";
	} else {
		assert(false && "Unsupported variable type!");
	}
	return zero;
}

static PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	bool isTopLevelDecl = ((parser->prevToken->kind == ':')
		|| (parser->prevToken->kind == tkSmmIdent && parser->prevToken != lval->token))
		&& parser->curScope->level == 0;

	PSmmToken eqToken = parser->curToken;
	if (lval->kind == nkSmmConst && eqToken->kind == '=') {
		smmPostMessage(errCantAssignToConst, eqToken->filePos);
		findToken(parser, ';');
		return &errorNode;
	}
	getNextToken(parser);
	PSmmAstNode val = parseExpression(parser);
	if (lval == &errorNode || val == &errorNode) return &errorNode;
	if (!lval->type) {
		// If right value is just another variable or func call just copy its type
		// but if it is expression then try to be a bit smarter.
		if (val->kind == nkSmmIdent) lval->type = val->type;
		else {
			switch (val->type->kind) {
			case tiSmmSoftFloat64: lval->type = &builtInTypes[tiSmmFloat64]; break;
			case tiSmmInt8: case tiSmmInt16: lval->type = &builtInTypes[tiSmmInt32]; break;
			case tiSmmUInt8: case tiSmmUInt16: lval->type = &builtInTypes[tiSmmUInt32]; break;
			default: lval->type = val->type; break;
			}
		}
	}
	// If lval is const or global var which just needs initializer we just return the val directly.
	if (lval->kind == nkSmmConst || ((val->flags & nfSmmConst) && isTopLevelDecl)) return val;
	
	// Otherwise we will actually need assignment statement
	PSmmAstNode assignment = newAstNode(parser, nkSmmAssignment);
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

	bool typeErrorReported = false;
	PSmmAstNode typeInfo = NULL;
	if (parser->curToken->kind == tkSmmIdent) {
		//Type is given in declaration so use it
		typeInfo = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfo || typeInfo->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfo = NULL;
		}
		getNextToken(parser);
	}
	
	PSmmAstNode spareNode = NULL;
	PSmmAstNode expr = NULL;
	if (parser->curToken->kind == '=') {
		expr = parseAssignment(parser, lval);
	} else if (parser->curToken->kind == ':') {
		PSmmToken constAssignToken = parser->curToken;
		lval->kind = nkSmmConst;
		lval->flags |= nfSmmConst;
		expr = parseAssignment(parser, lval);
		if (expr->kind == nkSmmParam) {
			if (parser->curScope->level > 0) {
				smmPostMessage(errSmmFuncUnderScope, lval->token->filePos);
				findToken(parser, ';');
				return &errorNode;
			}
			lval->kind = nkSmmFunc;
			PSmmAstFuncDefNode func = (PSmmAstFuncDefNode)lval;
			func->params = (PSmmAstParamNode)expr;
			if (func->params->count == 0) {
				spareNode = (PSmmAstNode)func->params;
				spareNode->kind = nkSmmDecl;
				func->params = NULL;
			}
			lval = parseFunction(parser, func);
			expr = NULL;
		} else if (expr != &errorNode && !(expr->flags & nfSmmConst)) {
			smmPostMessage(errNonConstInConstExpression, constAssignToken->filePos);
			expr = NULL;
		}
	} else if (parser->curToken->kind != ';') {
		expr = &errorNode;
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "':', '=' or type", got);
	} else if (typeInfo == NULL) {
		if (!typeErrorReported) {
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "type", "';'");
		}
		return &errorNode;
	}

	if (expr == &errorNode || lval == &errorNode) {
		if (spareNode) parser->allocator->free(parser->allocator, spareNode);
		return &errorNode;
	}

	PSmmAstNode decl = spareNode ? spareNode : newAstNode(parser, nkSmmDecl);
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
		bool isConstExpr = expr && (expr->flags & nfSmmConst);

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
				expr = newAstNode(parser, nkSmmAssignment);
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
		if ((lval->kind == nkSmmIdent) && (lval->type == NULL)) {
			smmPostMessage(errSmmUndefinedIdentifier, lval->token->filePos, lval->token->repr);
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
		PSmmTypeInfo retType = parser->curScope->returnType;
		if (retType->kind == tiSmmUnknown) {
			// We had problems trying to parse func return type so now we will assume type
			// of this return stmt is return type of the function
			parser->curScope->returnType = lval->type;
		} else if (lval->type != retType && !isUpcastPossible(lval->type, retType)) {
			smmPostMessage(errSmmBadReturnStmtType, retToken->filePos, lval->type->name, retType->name);
		}
	} else {
		lval = NULL;
		if (parser->curScope->returnType) {
			smmPostMessage(errSmmGotUnexpectedToken, retToken->filePos, "expression", "';'");
		}
	}
	PSmmAstNode res = newAstNode(parser, nkSmmReturn);
	res->left = lval;
	res->token = retToken;
	res->type = parser->curScope->returnType;
	expect(parser, ';');
	return res;
}

static PSmmAstNode parseExpressionStmt(PSmmParser parser) {
	PSmmAstNode lval;

	struct SmmFilePos fpos = parser->curToken->filePos;
	lval = parseExpression(parser);

	if (lval == &errorNode) {
		if (parser->curToken->kind == ';') getNextToken(parser);
		return lval;
	}

	bool justCreatedLValIdent = (lval->kind == nkSmmIdent) && (lval->type == NULL);

	if (!(lval->flags & nfSmmIdent) && (parser->curToken->kind == ':' || parser->curToken->kind == '=')) {
		smmPostMessage(errSmmOperandMustBeLVal, fpos);
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}

	if (parser->curToken->kind == ':') {
		lval = parseDeclaration(parser, lval);
	} else if (parser->curToken->kind == '=') {
		lval = parseAssignment(parser, lval);
	} else if (justCreatedLValIdent) {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "':' or '='", got);
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}
	if (lval) {
		bool isJustIdent = (lval->flags & nfSmmIdent) && (lval->kind != nkSmmCall) && (lval->kind != nkSmmError);
		bool isAnyBinOpExceptLogical = (lval->flags & nfSmmBinOp) && lval->kind != nkSmmAndOp && lval->kind != nkSmmOrOp;
		if (isJustIdent || isAnyBinOpExceptLogical) {
			smmPostMessage(wrnSmmNoEffectStmt, lval->token->filePos);
			if (isJustIdent) lval = NULL;
		}
	}
	if (parser->prevToken->kind != '}' && (lval != &errorNode || parser->curToken->kind == ';')) expect(parser, ';');
	return lval;
}

static PSmmAstNode parseStatement(PSmmParser parser) {
	switch (parser->curToken->kind) {
	case tkSmmReturn: return parseReturnStmt(parser);
	case '{': return (PSmmAstNode)parseBlock(parser, parser->curScope->returnType, false);
	case tkSmmIdent: return parseExpressionStmt(parser);
	case tkSmmErr:
		if (findToken(parser, ';')) {
			getNextToken(parser);
		}
		return NULL;
	default:
		{
			char gotBuf[4];
			const char* got = smmTokenToString(parser->curToken, gotBuf);
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "valid statement", got);
			if (findToken(parser, ';')) {
				getNextToken(parser);
			}
			return &errorNode;
		}
	}
}

static PSmmTypeInfo getCommonTypeFromOperands(PSmmAstNode res) {
	PSmmTypeInfo type;
	if (res->left->type->flags & res->right->type->flags & tifSmmInt) {
		// if both are ints we need to select bigger type but if only one is signed result should be signed
		bool leftUnsigned = res->left->type->flags & tifSmmUnsigned;
		bool rightUnsigned = res->right->type->flags & tifSmmUnsigned;
		type = (res->left->type->sizeInBytes > res->right->type->sizeInBytes) ? res->left->type : res->right->type;
		if (leftUnsigned != rightUnsigned && (type->flags & tifSmmUnsigned)) {
			type = &builtInTypes[type->kind - tiSmmUInt8 + tiSmmInt8];
		}
	} else {
		// Otherwise floats are always considered bigger then int
		type = (res->left->type->kind > res->right->type->kind) ? res->left->type : res->right->type;
	}
	return type;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupMulDivModNode(PSmmParser parser, PSmmAstNode res) {
	PSmmTypeInfo type = getCommonTypeFromOperands(res);
	PSmmTypeInfo ftype = type->kind < tiSmmFloat32 ? &builtInTypes[tiSmmSoftFloat64] : type;
	switch (res->token->kind) {
	case tkSmmIntDiv: case tkSmmIntMod: {
		res->kind = (type->flags & tifSmmUnsigned) ? nkSmmUDiv : nkSmmSDiv;
		if (res->token->kind == tkSmmIntMod) res->kind += nkSmmSRem - nkSmmSDiv;
		res->type = type;
		if (type->kind >= tiSmmFloat32) {
			char buf[4];
			smmPostMessage(errSmmBadOperandsType, res->token->filePos, smmTokenToString(res->token, buf), type->name);
			if (res->token->kind == tkSmmIntMod) res->kind = nkSmmFRem;
			else res->kind = nkSmmFDiv;
			// This way of handling it could possibly cause loss of precision warning as well.
			// We can add casting to int instead if we want to avoid that, but which int?
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
static PSmmAstNode setupAddSubNode(PSmmParser parser, PSmmAstNode res) {
	if (res->token->kind == '+') res->kind = nkSmmAdd;
	else res->kind = nkSmmSub;

	res->type = getCommonTypeFromOperands(res);

	if (res->type->kind >= tiSmmFloat32) res->kind++; // Add to FAdd, Sub to FSub

	return res;
}

// Called from parseBinOp for specific binary operators
static PSmmAstNode setupLogicOpNode(PSmmParser parser, PSmmAstNode res) {
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

	bool bothOperandsInts = res->left->type->flags & res->right->type->flags & tifSmmInt;
	if (!bothOperandsInts) return res;

	bool onlyOneSigned = (res->left->type->flags & tifSmmUnsigned) != (res->right->type->flags & tifSmmUnsigned);
	if (!onlyOneSigned) return res;

	smmPostMessage(wrnSmmComparingSignedAndUnsigned, res->token->filePos);

	PSmmAstNode castNode = newAstNode(parser, nkSmmCast);
	castNode->flags = res->flags & nfSmmConst;
	castNode->type = getCommonTypeFromOperands(res);
	if (res->left->type->flags & tifSmmUnsigned) {
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
		PSmmAstNode typeNode = newAstNode(parser, nkSmmType);
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
	PSmmAstNode program = newAstNode(parser, nkSmmProgram);
	PSmmAstBlockNode block = newAstNode(parser, nkSmmBlock);
	parser->curScope = newAstNode(parser, nkSmmScope);
	parser->curScope->lastDecl = (PSmmAstNode)parser->curScope;
	parser->curScope->returnType = &builtInTypes[tiSmmInt32];
	block->scope = parser->curScope;
	program->next = (PSmmAstNode)block;
	PSmmAstNode* nextStmt = &block->stmts;
	
	while (parser->curToken->kind != tkSmmEof) {
		PSmmAstNode curStmt = parseStatement(parser);
		if (curStmt != NULL && curStmt != &errorNode) {
			*nextStmt = curStmt;
			nextStmt = &curStmt->next;
		}
	}

	program->token = parser->allocator->alloc(parser->allocator, sizeof(struct SmmToken));
	program->token->repr = parser->lex->filePos.filename;
	return program;
}

PSmmTypeInfo smmGetBuiltInTypes() {
	return builtInTypes;
}