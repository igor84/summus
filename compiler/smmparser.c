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
	"cast", "param", "call", "return"
};

static struct SmmTypeInfo builtInTypes[] = {
	{ tiSmmUnknown, 0, "/unknown/"},
	{ tiSmmUInt8, 1, "uint8", tifSmmUnsignedInt }, { tiSmmUInt16, 2, "uint16", tifSmmUnsignedInt },
	{ tiSmmUInt32, 4, "uint32", tifSmmUnsignedInt }, { tiSmmUInt64, 8, "uint64", tifSmmUnsignedInt },
	{ tiSmmInt8, 1, "int8", tifSmmInt }, { tiSmmInt16, 2, "int16", tifSmmInt },
	{ tiSmmInt32, 4, "int32", tifSmmInt }, { tiSmmInt64, 8, "int64", tifSmmInt },
	{ tiSmmFloat32, 4, "float32", tifSmmFloat }, { tiSmmFloat64, 8, "float64", tifSmmFloat },
	{ tiSmmSoftFloat64, 8, "/sfloat64/", tifSmmFloat }, { tiSmmBool, 1, "bool", tifSmmBool }
};

static struct SmmAstNode errorNode = { nkSmmError, 0, NULL, &builtInTypes[0] };

/********************************************************
Private Functions
*********************************************************/

PSmmAstNode parseExpression(PSmmParser parser, bool canBeFuncDefn);
PSmmAstNode parseStatement(PSmmParser parser);

void* newAstNode(PSmmParser parser, SmmAstNodeKind kind) {
	PSmmAstNode res = parser->allocator->alloc(parser->allocator, sizeof(struct SmmAstNode));
	res->kind = kind;
	return res;
}

PSmmAstScopeNode newScopeNode(PSmmParser parser) {
	PSmmAstScopeNode scope = newAstNode(parser, nkSmmScope);
	scope->lastDecl = (PSmmAstNode)scope;
	scope->level = parser->curScope->level + 1;
	scope->prevScope = parser->curScope;
	scope->lastDecl = (PSmmAstNode)scope;
	scope->returnType = parser->curScope->returnType;
	return scope;
}

void getNextToken(PSmmParser parser) {
	parser->prevToken = parser->curToken;
	parser->curToken = smmGetNextToken(parser->lex);
}

bool isTerminatingToken(int tokenKind) {
	return tokenKind == ';' || tokenKind == '{' || tokenKind == tkSmmEof;
}

bool findToken(PSmmParser parser, int tokenKind) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenKind && !isTerminatingToken(curKind)) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	return curKind == tokenKind;
}

int findEitherToken(PSmmParser parser, int tokenKind1, int tokenKind2) {
	int curKind = parser->curToken->kind;
	while (curKind != tokenKind1 && curKind != tokenKind2 && !isTerminatingToken(curKind)) {
		getNextToken(parser);
		curKind = parser->curToken->kind;
	}
	if (curKind == tokenKind1) return tokenKind1;
	if (curKind == tokenKind2) return tokenKind2;
	return 0;
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

#define FUNC_SIGNATURE_LENGTH 4 * 1024

char* getFuncsSignatureAsString(PSmmAstFuncDefNode funcs, char* buf) {
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

char* getFuncCallAsString(const char* name, PSmmAstArgNode args, char* buf) {
	size_t len = strlen(name);
	strncpy(buf, name, len);
	buf[len++] = '(';
	PSmmAstArgNode curArg = args;
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

bool isUpcastPossible(PSmmTypeInfo srcType, PSmmTypeInfo dstType) {
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
PSmmAstNode resolveCall(PSmmParser parser, PSmmAstCallNode node) {
	PSmmAstFuncDefNode curFunc = (PSmmAstFuncDefNode)node->params;
	PSmmAstFuncDefNode softFunc = NULL;
	while (curFunc) {
		PSmmAstArgNode curArg = node->args;
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

PSmmAstNode parseIdentFactor(PSmmParser parser) {
	PSmmToken identToken = NULL;
	PSmmTypeInfo castType = NULL;
	PSmmAstNode res = &errorNode;
	PSmmAstNode var = smmGetDictValue(parser->idents, parser->curToken->repr, false);
	if (!var) {
		identToken = parser->curToken;
	} else if (var->kind == nkSmmType) {
		identToken = parser->curToken;
		castType = var->type;
	} else if ((var->flags & nfSmmIdent) == 0) {
		const char* tokenStr = nodeKindToString[var->kind];
		smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
	} else if (!var->type) {
		smmPostMessage(errSmmUndefinedIdentifier, parser->curToken->filePos, parser->curToken->repr);
	} else {
		res = newAstNode(parser, nkSmmIdent);
		*res = *var;
		res->token = parser->curToken;
	}
	getNextToken(parser);
	if (parser->curToken->kind == '(') {
		// Function call or cast
		if (castType) {
			// Since we pass '(' to parse expression it will also handle ')'
			PSmmAstNode expr = parseExpression(parser, false);
			if (expr == &errorNode) return expr;
			res = newAstNode(parser, nkSmmCast);
			res->left = expr;
			res->token = identToken;
			res->type = castType;
			res->flags = expr->flags & nfSmmConst;
		} else if (res != &errorNode) {
			getNextToken(parser); // Skip '('
			PSmmAstCallNode resCall = (PSmmAstCallNode)res;
			resCall->kind = nkSmmCall;
			resCall->params = (PSmmAstParamNode)var;
			resCall->zzNotUsed1 = NULL; // We must set this to NULL so it doesn't carry the reference to next overloaded call
			if (parser->curToken->kind != ')') {
				PSmmAstArgNode lastArg = (PSmmAstArgNode)parseExpression(parser, false);
				resCall->args = lastArg;
				while (parser->curToken->kind == ',') {
					getNextToken(parser);
					lastArg->next = (PSmmAstArgNode)parseExpression(parser, false);
					lastArg = lastArg->next;
				}
			}
			if (!expect(parser, ')')) return &errorNode;
			res = resolveCall(parser, resCall);
		} else {
			if (identToken) {
				smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
			}
			findToken(parser, ')'); // Skip all until closing parantheses
		}
	} else if (identToken && !castType) {
		if (parser->curToken->kind == ':') {
			// This is declaration
			res = newAstNode(parser, nkSmmIdent);
			res->flags = nfSmmIdent;
			res->token = identToken;
			smmAddDictValue(parser->idents, res->token->repr, res);
		} else {
			smmPostMessage(errSmmUndefinedIdentifier, identToken->filePos, identToken->repr);
		}
	} else if (castType) {
		const char* tokenStr = nodeKindToString[var->kind];
		smmPostMessage(errSmmIdentTaken, parser->curToken->filePos, parser->curToken->repr, tokenStr);
	}
	return res;
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
		assert(false && "Got literal of unknown kind!");
		return &builtInTypes[tiSmmUnknown];
	}
}

PSmmAstNode parseFuncParams(PSmmParser parser, PSmmAstParamNode firstParam) {
	assert(firstParam != NULL);
	assert(parser->curToken->kind == ':');

	if (firstParam->kind == nkSmmError) {
		findToken(parser, ';');
		return &errorNode;
	}

	if (firstParam->flags != nfSmmIdent) {
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", parser->curToken->repr);
		findToken(parser, ';');
		return &errorNode;
	}

	getNextToken(parser); //skip ':'

	PSmmTypeInfo typeInfo = NULL;
	if (parser->curToken->kind != tkSmmIdent) {
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "type", parser->curToken->repr);
		int foundToken = findEitherToken(parser, ',', ')');
		if (!foundToken) return &errorNode;
		typeInfo = &builtInTypes[tiSmmUnknown];
	}
	if (typeInfo == NULL) {
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, parser->curToken->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			return &errorNode;
		}
		typeInfo = typeInfoNode->type;
	}

	int paramCount = 1;
	firstParam->kind = nkSmmParam;
	firstParam->type = typeInfo;
	firstParam->flags = nfSmmIdent;
	firstParam->level = parser->curScope->level + 1;
	smmPushDictValue(parser->idents, firstParam->token->repr, firstParam);

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
		PSmmToken paramType = expect(parser, tkSmmIdent);
		if (!paramType) {
			findEitherToken(parser, ',', ')');
			continue;
		}
		PSmmAstNode typeInfoNode = smmGetDictValue(parser->idents, paramType->repr, false);
		if (!typeInfoNode || typeInfoNode->kind != nkSmmType) {
			smmPostMessage(errSmmUnknownType, parser->curToken->filePos, parser->curToken->repr);
			typeInfoNode = smmGetDictValue(parser->idents, builtInTypes[tiSmmUnknown].name, false);
		}
		PSmmAstParamNode newParam = smmGetDictValue(parser->idents, paramName->repr, false);
		if (newParam) {
			if (newParam->kind == nkSmmParam && newParam->level == parser->curScope->level + 1) {
				smmPostMessage(errSmmRedefinition, paramName->filePos, paramName->repr);
				continue;
			} else if (newParam->kind != nkSmmIdent) {
				const char* tokenStr = nodeKindToString[newParam->kind];
				smmPostMessage(errSmmIdentTaken, paramName->filePos, paramName->repr, tokenStr);
				continue;
			}
		}
		newParam = newAstNode(parser, nkSmmParam);
		newParam->flags = nfSmmIdent;
		newParam->level = parser->curScope->level + 1;
		newParam->token = paramName;
		newParam->type = typeInfoNode->type;
		param->next = newParam;
		param = newParam;
		smmPushDictValue(parser->idents, newParam->token->repr, newParam);
	}

	firstParam->count = paramCount;

	return (PSmmAstNode)firstParam;
}

PSmmAstNode getLiteralNode(PSmmParser parser) {
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

bool isNegFactor(PSmmParser parser) {
	bool isNeg = false;
	if (parser->curToken->kind == '-') {
		isNeg = true;
		getNextToken(parser);
	} else if (parser->curToken->kind == '+') {
		getNextToken(parser);
	}
	return isNeg;
}

PSmmAstNode parseFactor(PSmmParser parser, bool canBeFuncDefn) {
	PSmmAstNode res = &errorNode;
	int doNeg = isNegFactor(parser);
	canBeFuncDefn = canBeFuncDefn && !doNeg;
	if (parser->curToken->kind == '(') {
		getNextToken(parser);
		if (parser->curToken->kind == ')') {
			if (canBeFuncDefn) {
				PSmmAstParamNode param = newAstNode(parser, nkSmmParam);
				param->count = 0;
				return (PSmmAstNode)param;
			}
			smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "expression", ")");
			findToken(parser, ';');
			return &errorNode;
		}
		res = parseExpression(parser, false);
		if (parser->curToken->kind == ':') {
			if (canBeFuncDefn) {
				res = parseFuncParams(parser, (PSmmAstParamNode)res);
			} else {
				// TODO(igors): Fix all GotUnexpected token to be equaly quoted
				smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "operator", ":");
				findToken(parser, ';');
				return &errorNode;
			}
		}
		if (res == &errorNode || (!expect(parser, ')') && parser->curToken->kind != tkSmmRArrow)) {
			// TODO(igors): What if we ecounter expression like a := (b + c -> d;
			return &errorNode;
		}
	} else {
		bool reportedError = parser->lastErrorLine == parser->curToken->filePos.lineNumber;
			
		switch (parser->curToken->kind) {
		case tkSmmIdent: res = parseIdentFactor(parser); break;
		case tkSmmUInt: case tkSmmFloat: case tkSmmBool:
			res = getLiteralNode(parser);
			break;
		default: 
			if (!reportedError && parser->curToken->kind != tkSmmErr) {
				char gotBuf[4];
				const char* got = smmTokenToString(parser->curToken, gotBuf);
				smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "identifier or literal", got);
			}
			break;
		}
		if (res == &errorNode) findToken(parser, ';');
	}

	if (res == &errorNode) return res;

	if (doNeg == 1) {
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
		PSmmAstNode right = parseFactor(parser, false);
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

		PSmmAstNode res = newAstNode(parser, nkSmmError);
		res->left = left;
		res->right = right;
		res->token = resToken;
		res->flags = left->flags & right->flags & nfSmmConst;
		left = binOp->setupNode(res);
	}
}

PSmmAstNode parseExpression(PSmmParser parser, bool canBeFuncDefn) {
	PSmmAstNode left = parseFactor(parser, canBeFuncDefn);
	if (left != &errorNode) {
		left = parseBinOp(parser, left, 0);
	}
	return left;
}

PSmmAstNode parseAssignment(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken eqToken = parser->curToken;
	if (lval->kind == nkSmmConst && eqToken->kind == '=') {
		smmPostMessage(errCantAssignToConst, eqToken->filePos);
		findToken(parser, ';');
		return &errorNode;
	}
	getNextToken(parser);
	PSmmAstNode val = parseExpression(parser, true);
	if (lval == &errorNode || val == &errorNode) return &errorNode;
	if (!lval->type) {
		if (val->type->kind == tiSmmSoftFloat64) {
			lval->type = &builtInTypes[tiSmmFloat64];
		} else {
			lval->type = val->type;
		}
	}
	if (lval->kind == nkSmmConst) return val;
	PSmmAstNode assignment = newAstNode(parser, nkSmmAssignment);
	assignment->left = lval;
	assignment->right = val;
	assignment->type = lval->type;
	assignment->token = eqToken;
	return assignment;
}

void removeScopeVars(PSmmParser parser, PSmmAstScopeNode scope) {
	PSmmAstNode curDecl = scope->decls;
	while (curDecl) {
		smmPopDictValue(parser->idents, curDecl->left->token->repr);
		curDecl = curDecl->next;
	}
}

PSmmAstBlockNode parseBlock(PSmmParser parser, PSmmTypeInfo curFuncReturnType) {
	getNextToken(parser); // Skip '{'
	PSmmAstBlockNode block = newAstNode(parser, nkSmmBlock);
	block->scope = newScopeNode(parser);
	block->scope->returnType = curFuncReturnType;
	PSmmAstNode* nextStmt = &block->stmts;
	while (parser->curToken->kind != tkSmmEof && parser->curToken->kind != '}') {
		PSmmAstNode curStmt = parseStatement(parser);
		if (curStmt != NULL && curStmt != &errorNode) {
			*nextStmt = curStmt;
			nextStmt = &curStmt->next;
		}
	}
	expect(parser, '}');
	removeScopeVars(parser, block->scope);
	return block;
}

/**
 * This is called after we already parsed parameters so we expect optional
 * arrow and type and then also optional function body. Func node should
 * have kind, token and params set.
 */
PSmmAstNode parseFunction(PSmmParser parser, PSmmAstFuncDefNode func) {
	if (parser->curToken->kind != tkSmmRArrow && parser->curToken->kind != '{' && parser->curToken->kind != ';') {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "one of '->', '{' or ';'", got);
		while (parser->curToken->kind != tkSmmRArrow && parser->curToken->kind != '{' && parser->curToken->kind != ';') {
			getNextToken(parser);
		}
	}
	PSmmTypeInfo typeInfo = NULL; // TODO(igors): Maybe make a void type
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
	if (parser->curToken->kind == '{' || parser->curToken->kind == ';') {
		if (parser->curToken->kind == '{') {
			func->body = parseBlock(parser, typeInfo);
		}
		PSmmAstParamNode param = func->params;
		while (param) {
			smmPopDictValue(parser->idents, param->token->repr);
			param = param->next;
		}
	} else {
		char gotBuf[4];
		const char* got = smmTokenToString(parser->curToken, gotBuf);
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "{ or ;", got);
		// TODO(igors): Should I return errorNode here?
	}
	return (PSmmAstNode)func;
}

PSmmAstNode parseDeclaration(PSmmParser parser, PSmmAstNode lval) {
	PSmmToken declToken = parser->curToken;
	expect(parser, ':');
	if (lval->type) {
		smmPostMessage(errSmmRedefinition, declToken->filePos, lval->token->repr);
		findToken(parser, ';');
		return &errorNode;
	}

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
		if (expr->kind == nkSmmParam) { // TODO(igors): Maybe check if parser->curScope->level == 0 and report error for declaring func in sub block
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
		} else if (expr != &errorNode && (expr->flags & nfSmmConst) == 0) {
			smmPostMessage(errNonConstInConstExpression, constAssignToken->filePos);
		}
	} else if (parser->curToken->kind != ';') {
		expr = &errorNode;
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, "':', '=' or type", parser->curToken->repr);
	}

	if (expr == &errorNode) return expr;

	PSmmAstNode decl = spareNode ? spareNode : newAstNode(parser, nkSmmDecl);
	decl->token = declToken;
	decl->left = lval;
	decl->right = expr;

	if (typeInfo) {
		// We do this here so that parseAssignment and parseExpression above
		// can detect usage of undeclared identifier in expressions like x : int8 = x + 1;
		lval->type = typeInfo->type;
		if (decl->right && decl->right->kind == nkSmmAssignment) {
			decl->right->type = lval->type;
		}
	}
	decl->type = lval->type;
	parser->curScope->lastDecl->next = decl;
	parser->curScope->lastDecl = decl;

	if (lval->kind == nkSmmConst) return NULL;
	return expr;
}

PSmmAstNode parseStatement(PSmmParser parser) {
	PSmmAstNode lval;

	if (parser->curToken->kind == tkSmmReturn) {
		PSmmToken retToken = parser->curToken;
		getNextToken(parser);
		lval = parseExpression(parser, false);
		if (lval == &errorNode) {
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
		if ((lval->kind == nkSmmIdent) && (lval->type == NULL)) {
			smmPostMessage(errSmmUndefinedIdentifier, lval->token->filePos, lval->token->repr);
			if (findToken(parser, ';')) getNextToken(parser);
			return &errorNode;
		}
		if (lval->type != parser->curScope->returnType && !isUpcastPossible(lval->type, parser->curScope->returnType)) {
			smmPostMessage(errSmmBadReturnStmtType, retToken->filePos, lval->type->name, parser->curScope->returnType);
		}
		PSmmAstNode res = newAstNode(parser, nkSmmReturn);
		res->left = lval;
		res->token = retToken;
		res->type = parser->curScope->returnType;
		expect(parser, ';');
		return res;
	}

	struct SmmFilePos fpos = parser->curToken->filePos;
	lval = parseExpression(parser, false);

	bool justCreatedLValIdent = (lval->kind == nkSmmIdent) && (lval->type == NULL);
	
	if ((lval->flags & nfSmmIdent) == 0 && (parser->curToken->kind == ':' || parser->curToken->kind == '=')) {
		smmPostMessage(errSmmOperandMustBeLVal, fpos);
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}

	// TODO(igors): Handle case when there is functionCall; with msg "expected '('"
	if (parser->curToken->kind == ':') {
		lval = parseDeclaration(parser, lval);
	} else if (parser->curToken->kind == '=') {
		lval = parseAssignment(parser, lval);
	} else if (justCreatedLValIdent) {
		smmPostMessage(errSmmGotUnexpectedToken, parser->curToken->filePos, ": or =", parser->curToken->repr);
		if (findToken(parser, ';')) getNextToken(parser);
		return &errorNode;
	}
	if (parser->prevToken->kind != '}') expect(parser, ';');
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

	static struct SmmBinaryOperator mulDivModOp = { setupMulDivModNode, 120 };
	static struct SmmBinaryOperator addSubOp = { setupAddSubNode, 110 };
	static struct SmmBinaryOperator andOp = { setupLogicOpNode, 100 };
	static struct SmmBinaryOperator xorOp = { setupLogicOpNode, 90 };
	static struct SmmBinaryOperator orOp = { setupLogicOpNode, 80 };

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
	return program;
}
