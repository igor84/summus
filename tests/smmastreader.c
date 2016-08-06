#include "smmastwritter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PSmmToken lastToken;
static PSmmDict typeDict;

static void* newAstNode(SmmAstNodeKind kind, PSmmAllocator a) {
	PSmmAstNode res = a->alloc(a, sizeof(struct SmmAstNode));
	res->kind = kind;
	return res;
}

static void processExpression(PSmmAstNode* exprField, PSmmLexer lex, PSmmAllocator a) {
	SmmAstNodeKind kind = 0;
	PSmmToken exprToken = lastToken;
	lastToken = smmGetNextToken(lex);
	switch (exprToken->kind) {
	case '+':
		if (lastToken->kind == '.') {
			kind = nkSmmFAdd;
			lastToken = smmGetNextToken(lex);
		} else {
			kind = nkSmmAdd;
		}
		break;
	case '-':
		if (lastToken->kind == '.') {
			kind = nkSmmFSub;
			lastToken = smmGetNextToken(lex);
		} else {
			kind = nkSmmSub;
		}
		break;
	case '*':
		if (lastToken->kind == '.') {
			kind = nkSmmFMul;
			lastToken = smmGetNextToken(lex);
		} else {
			kind = nkSmmMul;
		}
		break;
	case '/': kind = nkSmmFDiv; break;
	case '%': kind = nkSmmFRem; break;
	case '<': kind = nkSmmLt; break;
	case '>': kind = nkSmmGt; break;
	case tkSmmEq: kind = nkSmmEq; break;
	case tkSmmNotEq: kind = nkSmmNotEq; break;
	case tkSmmLtEq: kind = nkSmmLtEq; break;
	case tkSmmGtEq: kind = nkSmmGtEq; break;
	case '(': kind = nkSmmCall; break;
	case tkSmmNot: kind = nkSmmNot; break;
	case tkSmmAndOp: kind = nkSmmAndOp; break;
	case tkSmmXorOp: kind = nkSmmXorOp; break;
	case tkSmmOrOp: kind = nkSmmOrOp; break;
	case tkSmmIdent:
		switch (exprToken->repr[0]) {
		case 'C': kind = nkSmmConst; break;
		case 'I': kind = nkSmmIdent; break;
		case 'u':
			if (exprToken->repr[1] == 'd') kind = nkSmmUDiv;
			else kind = nkSmmURem;
			break;
		case 's':
			if (exprToken->repr[1] == 'd') kind = nkSmmSDiv;
			else kind = nkSmmSRem;
			break;
		case 'n': kind = nkSmmNeg; break;
		case 'i': kind = nkSmmInt; break;
		case 'f': kind = nkSmmFloat; break;
		case 'b': kind = nkSmmBool; break;
		case 'c': kind = nkSmmCast; break;
		case 'p': kind = nkSmmParam; break;
		default:
			assert(false && "Tried to parse unknown node kind by name");
		}
		break;
	default:
		assert(false && "Tried to parse unknown node kind");
	}

	PSmmAstNode expr = newAstNode(kind, a);
	expr->token = exprToken;
	
	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			expr->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
			smmGetNextToken(lex); // skip ':'
			expr->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
			lastToken = smmGetNextToken(lex);
			processExpression(&expr->left, lex, a);
			lastToken = smmGetNextToken(lex);
			processExpression(&expr->right, lex, a);
			break;
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			expr->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
			smmGetNextToken(lex); // skip ':'
			PSmmToken typeToken = smmGetNextToken(lex);
			expr->type = smmGetDictValue(typeDict, typeToken->repr, false);
			if (expr->kind == nkSmmNeg) expr->token->repr = "-";
			else if (expr->kind == nkSmmCast) expr->token = typeToken;
			lastToken = smmGetNextToken(lex);
			processExpression(&expr->left, lex, a);
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			callNode->token = lastToken;
			smmGetNextToken(lex); // skip ':'
			callNode->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
			smmGetNextToken(lex); // skip ':'
			callNode->returnType = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
			smmGetNextToken(lex); // skip '('
			lastToken = smmGetNextToken(lex);
			PSmmAstNode arg = NULL;
			if (lastToken->kind != ')') {
				processExpression(&arg, lex, a);
				callNode->args = arg;
			}
			lastToken = smmGetNextToken(lex);
			while (lastToken->kind == ',') {
				lastToken = smmGetNextToken(lex);
				processExpression(&arg->next, lex, a);
				lastToken = smmGetNextToken(lex);
				arg = arg->next;
			}
			smmGetNextToken(lex); // Skip additional ')' since it ends with '))'
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		expr->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
		smmGetNextToken(lex); // skip ':'
		expr->token = smmGetNextToken(lex);
		if (expr->token->kind == '-') {
			expr->token = smmGetNextToken(lex);
			if (expr->kind == nkSmmInt) expr->token->sintVal = -expr->token->sintVal;
			else expr->token->floatVal = -expr->token->floatVal;
		}
		smmGetNextToken(lex); // skip ':'
		expr->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
		break;
	default:
		assert(false && "Got unexpected node type in processExpression while parsing AST");
		break;
	}
	*exprField = expr;
}

static void processLocalSymbols(PSmmAstScopeNode scope, PSmmLexer lex, PSmmAllocator a) {
	PSmmAstNode decl = NULL;
	if (lastToken->kind == ':') {
		decl = newAstNode(nkSmmDecl, a);
		scope->decls = decl;
	}
	while (decl) {
		decl->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
		decl->left = newAstNode(nkSmmIdent, a);
		decl->left->token = smmGetNextToken(lex);
		smmGetNextToken(lex); // skip ':'
		decl->left->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
		smmGetNextToken(lex); // skip ':'
		decl->left->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
		decl->type = decl->left->type;
		if (decl->left->flags & nfSmmConst) {
			decl->left->kind = nkSmmConst;
			smmGetNextToken(lex); // We skip '='
			lastToken = smmGetNextToken(lex);
			processExpression(&decl->right, lex, a);
		}
		lastToken = smmGetNextToken(lex);
		if (lastToken->kind == ':') {
			decl->next = newAstNode(nkSmmDecl, a);
		}
		decl = decl->next;
	}
}

static void processAssignment(PSmmAstNode stmt, PSmmLexer lex, PSmmAllocator a) {
	stmt->kind = nkSmmAssignment;
	stmt->token = lastToken;
	stmt->left = newAstNode(nkSmmIdent, a);
	stmt->left->token = smmGetNextToken(lex);
	smmGetNextToken(lex); // skip ':'
	stmt->left->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
	smmGetNextToken(lex); // skip ':'
	stmt->left->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
	stmt->type = stmt->left->type;
	lastToken = smmGetNextToken(lex);
	processExpression(&stmt->right, lex, a);
	lastToken = smmGetNextToken(lex);
}

static void processReturn(PSmmAstNode stmt, PSmmLexer lex, PSmmAllocator a) {
	stmt->kind = nkSmmReturn;
	stmt->token = lastToken;
	lastToken = smmGetNextToken(lex);
	if (lastToken->kind == ':') {
		stmt->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
		lastToken = smmGetNextToken(lex);
		processExpression(&stmt->left, lex, a);
		lastToken = smmGetNextToken(lex);
	}
}

static void processBlock(PSmmAstBlockNode block, PSmmLexer lex, PSmmAllocator a) {
	PSmmAstNode* stmt = &block->stmts;
	while (lastToken->kind != tkSmmEof && lastToken->kind != '}') {
		switch (lastToken->kind) {
		case '{':
			{
				PSmmAstBlockNode newBlock = newAstNode(nkSmmBlock, a);
				*stmt = (PSmmAstNode)newBlock;
				newBlock->scope = newAstNode(nkSmmScope, a);
				lastToken = smmGetNextToken(lex);
				processLocalSymbols(newBlock->scope, lex, a);
				processBlock(newBlock, lex, a);
				lastToken = smmGetNextToken(lex);
				break;
			}
		case '=': 
			*stmt = newAstNode(nkSmmBlock, a);
			processAssignment(*stmt, lex, a);
			break;
		case tkSmmReturn:
			*stmt = newAstNode(nkSmmBlock, a);
			processReturn(*stmt, lex, a);
			break;
		default:
			processExpression(stmt, lex, a);
			lastToken = smmGetNextToken(lex);
			break;
		}
		stmt = &(*stmt)->next;
	}
}

static void processGlobalSymbols(PSmmAstScopeNode scope, PSmmLexer lex, PSmmAllocator a) {
	PSmmAstNode decl = NULL;
	if (lastToken->kind == ':') {
		decl = newAstNode(nkSmmDecl, a);
		scope->decls = decl;
	}
	while (decl) {
		decl->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
		decl->left = newAstNode(nkSmmIdent, a);
		decl->left->token = smmGetNextToken(lex);
		smmGetNextToken(lex); // skip ':'
		decl->left->flags = (uint32_t)smmGetNextToken(lex)->uintVal;
		smmGetNextToken(lex); // skip ':'
		decl->left->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
		decl->type = decl->left->type;
		lastToken = smmGetNextToken(lex);
		if (lastToken->kind == '(') {
			decl->left->kind = nkSmmFunc;
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;

			PSmmAstParamNode param = NULL;
			uintptr_t paramCount = 0;
			lastToken = smmGetNextToken(lex);
			if (lastToken->kind != ')') {
				param = newAstNode(nkSmmParam, a);
				funcNode->params = param;
			}
			while (param) {
				paramCount++;
				param->token = lastToken;
				smmGetNextToken(lex); // skip ':'
				param->type = smmGetDictValue(typeDict, smmGetNextToken(lex)->repr, false);
				lastToken = smmGetNextToken(lex);
				if (lastToken->kind == ',') {
					param->next = newAstNode(nkSmmParam, a);
					lastToken = smmGetNextToken(lex);
				}
				param = param->next;
			}
			funcNode->params->count = paramCount;
			lastToken = smmGetNextToken(lex);
			if (lastToken->kind == '{') {
				funcNode->body = newAstNode(nkSmmBlock, a);
				funcNode->body->scope = newAstNode(nkSmmScope, a);
				lastToken = smmGetNextToken(lex);
				processLocalSymbols(funcNode->body->scope, lex, a);
				processBlock(funcNode->body, lex, a);
				lastToken = smmGetNextToken(lex);
			}
		} else {
			if (lastToken->kind == ':') decl->left->kind = nkSmmConst;
			lastToken = smmGetNextToken(lex);
			processExpression(&decl->right, lex, a);
			lastToken = smmGetNextToken(lex);
		}
		if (lastToken->kind == ':') {
			decl->next = newAstNode(nkSmmDecl, a);
		}
		decl = decl->next;
	}
}

void initTypeDict() {
	if (typeDict != NULL) return;

	PSmmAllocator a = smmCreatePermanentAllocator("TYPEDICT", 4 * 1024);
	typeDict = smmCreateDict(a, NULL, NULL);
	typeDict->storeKeyCopy = false;
	PSmmTypeInfo typeInfo = smmGetBuiltInTypes();
	typeInfo->name = "unknown"; // Instead of "/unknown/"
	do {
		smmAddDictValue(typeDict, typeInfo->name, typeInfo);
		typeInfo++;
	} while (typeInfo->kind != tiSmmSoftFloat64);
	smmAddDictValue(typeDict, "sfloat64", typeInfo);
}

PSmmAstNode smmLoadAst(PSmmLexer lex, PSmmAllocator a) {
	initTypeDict();
	PSmmAstNode module = newAstNode(nkSmmProgram, a);
	module->token = smmGetNextToken(lex);


	PSmmAstBlockNode globalBlock = newAstNode(nkSmmBlock, a);
	module->next = (PSmmAstNode)globalBlock;
	globalBlock->scope = newAstNode(nkSmmScope, a);

	lastToken = smmGetNextToken(lex);
	processGlobalSymbols(globalBlock->scope, lex, a);

	processBlock(globalBlock, lex, a);

	return module;
}
