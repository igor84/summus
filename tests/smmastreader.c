#include "smmastwritter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void processStatement(PSmmAstNode* stmt, PSmmLexer lex, PIbsAllocator a);
static void processBlock(PSmmAstBlockNode block, PSmmLexer lex, PIbsAllocator a);

static PSmmToken lastToken;
static PIbsDict typeDict;

static void readFlags(PSmmAstNode node, PSmmToken token) {
	// We must do it this way instead of having a union with uint32 field
	// because bit positions are compiler dependent
	uint32_t flags = (uint32_t)token->uintVal;
	node->isIdent = flags & 1;
	node->isConst = (flags & 2) > 0;
	node->isBinOp = (flags & 4) > 0;
}

static void processExpression(PSmmAstNode* exprField, PSmmLexer lex, PIbsAllocator a) {
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
	case '!': case tkSmmNot: kind = nkSmmNot; break;
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

	PSmmAstNode expr = smmNewAstNode(kind, a);
	expr->token = exprToken;
	if (expr->kind == nkSmmUDiv || expr->kind == nkSmmSDiv) expr->token->repr = "div";
	else if (expr->kind == nkSmmURem || expr->kind == nkSmmSRem) expr->token->repr = "mod";
	
	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			readFlags(expr, smmGetNextToken(lex));
			smmGetNextToken(lex); // skip ':'
			expr->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
			lastToken = smmGetNextToken(lex);
			processExpression(&expr->left, lex, a);
			lastToken = smmGetNextToken(lex);
			processExpression(&expr->right, lex, a);
			break;
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			readFlags(expr, smmGetNextToken(lex));
			smmGetNextToken(lex); // skip ':'
			PSmmToken typeToken = smmGetNextToken(lex);
			expr->type = ibsDictGet(typeDict, typeToken->repr);
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
			readFlags(expr, smmGetNextToken(lex));
			smmGetNextToken(lex); // skip ':'
			callNode->returnType = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
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
			if (callNode->args) smmGetNextToken(lex); // Skip one more ')'
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		{
			readFlags(expr, smmGetNextToken(lex));
			smmGetNextToken(lex); // skip ':'
			expr->token = smmGetNextToken(lex);
			if (expr->kind == nkSmmFloat && expr->token->kind != tkSmmFloat) {
				expr->token->floatVal = (double)expr->token->uintVal;
			}
			smmGetNextToken(lex); // skip ':'
			expr->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
			break;
		}
	default:
		assert(false && "Got unexpected node type in processExpression while parsing AST");
		break;
	}
	*exprField = expr;
}

static void processLocalSymbols(PSmmAstScopeNode scope, PSmmLexer lex, PIbsAllocator a) {
	PSmmAstDeclNode decl = NULL;
	if (lastToken->kind == ':') {
		decl = smmNewAstNode(nkSmmDecl, a);
		scope->decls = decl;
	}
	while (decl) {
		decl->left = smmNewAstNode(nkSmmAssignment, a);
		decl->left->left = smmNewAstNode(nkSmmIdent, a);
		decl->left->left->token = smmGetNextToken(lex);
		smmGetNextToken(lex); // skip ':'
		readFlags(decl->left->left, smmGetNextToken(lex));
		smmGetNextToken(lex); // skip ':'
		decl->left->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
		decl->left->left->type = decl->left->type;
		if (decl->left->left->isConst) {
			decl->left->left->kind = nkSmmConst;
			decl->left->token = smmGetNextToken(lex); // We assign '=' but change it
			decl->left->token->repr = ":";
			lastToken = smmGetNextToken(lex);
			processExpression(&decl->left->right, lex, a);
		} else {
			decl->left->token = ibsAlloc(a, sizeof(struct SmmToken));
			decl->left->token->kind = '=';
			decl->left->token->repr = "=";
		}
		lastToken = smmGetNextToken(lex);
		if (lastToken->kind == ':') {
			decl->nextDecl = smmNewAstNode(nkSmmDecl, a);
		}
		
		decl = decl->nextDecl;
	}
}

static void processAssignment(PSmmAstNode stmt, PSmmLexer lex, PIbsAllocator a) {
	stmt->kind = nkSmmAssignment;
	stmt->token = lastToken;
	stmt->left = smmNewAstNode(nkSmmIdent, a);
	stmt->left->token = smmGetNextToken(lex);
	smmGetNextToken(lex); // skip ':'
	readFlags(stmt->left, smmGetNextToken(lex));
	smmGetNextToken(lex); // skip ':'
	if (stmt->left->isConst) stmt->left->kind = nkSmmConst;
	stmt->left->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
	stmt->type = stmt->left->type;
	lastToken = smmGetNextToken(lex);
	processExpression(&stmt->right, lex, a);
	lastToken = smmGetNextToken(lex);
}

static void processReturn(PSmmAstNode stmt, PSmmLexer lex, PIbsAllocator a) {
	stmt->kind = nkSmmReturn;
	stmt->token = lastToken;
	lastToken = smmGetNextToken(lex);
	if (lastToken->kind == ':') {
		stmt->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
		lastToken = smmGetNextToken(lex);
		if (!lastToken->isFirstOnLine) {
			processExpression(&stmt->left, lex, a);
			lastToken = smmGetNextToken(lex);
		}
	}
}

static void processStatement(PSmmAstNode* stmt, PSmmLexer lex, PIbsAllocator a) {
	switch (lastToken->kind) {
	case '{':
		{
			PSmmAstBlockNode newBlock = smmNewAstNode(nkSmmBlock, a);
			*stmt = (PSmmAstNode)newBlock;
			newBlock->scope = smmNewAstNode(nkSmmScope, a);
			lastToken = smmGetNextToken(lex);
			processLocalSymbols(newBlock->scope, lex, a);
			processBlock(newBlock, lex, a);
			lastToken = smmGetNextToken(lex);
			break;
		}
	case '=':
		*stmt = smmNewAstNode(nkSmmAssignment, a);
		processAssignment(*stmt, lex, a);
		break;
	case ':':
		*stmt = smmNewAstNode(nkSmmDecl, a);
		(*stmt)->token = lastToken;
		(*stmt)->left = smmNewAstNode(nkSmmAssignment, a);
		lastToken = smmGetNextToken(lex);
		processAssignment((*stmt)->left, lex, a);
		break;
	case tkSmmReturn:
		*stmt = smmNewAstNode(nkSmmBlock, a);
		processReturn(*stmt, lex, a);
		break;
	default:
		processExpression(stmt, lex, a);
		lastToken = smmGetNextToken(lex);
		break;
	}
}

static void processBlock(PSmmAstBlockNode block, PSmmLexer lex, PIbsAllocator a) {
	PSmmAstNode* stmt = &block->stmts;
	assert(lastToken->kind == tkSmmIdent && lastToken->repr[0] == 'b');
	smmGetNextToken(lex); // Skip ':'
	readFlags((PSmmAstNode)block, smmGetNextToken(lex));
	lastToken = smmGetNextToken(lex);
	while (lastToken->kind != tkSmmEof && lastToken->kind != '}' &&
			!(lastToken->kind == tkSmmIdent && strcmp(lastToken->repr, "ENDMODULE") == 0)) {
		processStatement(stmt, lex, a);
		stmt = &(*stmt)->next;
	}
}

static void processGlobalSymbols(PSmmAstScopeNode scope, PSmmLexer lex, PIbsAllocator a) {
	PSmmAstDeclNode decl = NULL;
	if (lastToken->kind == ':') {
		decl = smmNewAstNode(nkSmmDecl, a);
		scope->decls = decl;
	}
	while (decl) {
		decl->left = smmNewAstNode(nkSmmAssignment, a);
		decl->left->token = smmGetNextToken(lex);
		smmGetNextToken(lex); // skip ':'
		readFlags(decl->left, smmGetNextToken(lex));
		lastToken = smmGetNextToken(lex); // skip ':'
		if (lastToken->kind == ':') {
			decl->left->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
			lastToken = smmGetNextToken(lex);
		}
		if (lastToken->kind == '(') {
			decl->left->kind = nkSmmFunc;
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;

			PSmmAstParamNode param = NULL;
			uintptr_t paramCount = 0;
			lastToken = smmGetNextToken(lex);
			if (lastToken->kind != ')') {
				param = smmNewAstNode(nkSmmParam, a);
				funcNode->params = param;
			}
			while (param) {
				paramCount++;
				param->token = lastToken;
				smmGetNextToken(lex); // skip ':'
				readFlags((PSmmAstNode)param, smmGetNextToken(lex));
				smmGetNextToken(lex); // skip ':'
				param->type = ibsDictGet(typeDict, smmGetNextToken(lex)->repr);
				lastToken = smmGetNextToken(lex);
				if (lastToken->kind == ',') {
					param->next = smmNewAstNode(nkSmmParam, a);
					lastToken = smmGetNextToken(lex);
				}
				param = param->next;
			}
			if (paramCount > 0) funcNode->params->count = paramCount;
			lastToken = smmGetNextToken(lex);
			if (lastToken->kind == '{') {
				funcNode->body = smmNewAstNode(nkSmmBlock, a);
				funcNode->body->scope = smmNewAstNode(nkSmmScope, a);
				lastToken = smmGetNextToken(lex);
				processLocalSymbols(funcNode->body->scope, lex, a);
				processBlock(funcNode->body, lex, a);
				lastToken = smmGetNextToken(lex);
			} else if (lastToken->kind == ';') {
				lastToken = smmGetNextToken(lex);
			} else {
				assert(false && "Got function that is followed by unknown token");
			}
		} else {
			PSmmAstNode newLeft = smmNewAstNode(nkSmmAssignment, a);
			newLeft->left = decl->left;
			decl->left = newLeft;
			decl->left->type = decl->left->left->type;
			if (lastToken->repr[0] == '=') {
				decl->left->left->kind = nkSmmConst;
				decl->left->token = lastToken;
				decl->left->token->repr = ":";
				lastToken = smmGetNextToken(lex);
				processExpression(&decl->left->right, lex, a);
				lastToken = smmGetNextToken(lex);
			} else {
				decl->left->left->kind = nkSmmIdent;
				decl->left->token = ibsAlloc(a, sizeof(struct SmmToken));
				decl->left->token->kind = '=';
				decl->left->token->repr = "=";
			}
		}
		if (lastToken->kind == ':') {
			decl->nextDecl = smmNewAstNode(nkSmmDecl, a);
		}
		decl = decl->nextDecl;
	}
}

void initTypeDict() {
	if (typeDict != NULL) return;

	PIbsAllocator a = ibsSimpleAllocatorCreate("TYPEDICT", 4 * 1024);
	typeDict = ibsDictCreate(a);
	PSmmTypeInfo typeInfo = &builtInTypes[0];
	typeInfo->name = "unknown"; // Instead of "/unknown/"
	builtInTypes[tiSmmVoid].name = "void"; // Instead of "/void/"
	do {
		ibsDictPut(typeDict, typeInfo->name, typeInfo);
		typeInfo++;
	} while (typeInfo->kind != tiSmmSoftFloat64);
	ibsDictPut(typeDict, "sfloat64", typeInfo);
}

PSmmAstNode smmLoadAst(PSmmLexer lex, PIbsAllocator a) {
	initTypeDict();
	PSmmAstNode module = smmNewAstNode(nkSmmProgram, a);
	module->token = smmGetNextToken(lex);

	PSmmAstBlockNode globalBlock = smmNewAstNode(nkSmmBlock, a);
	module->next = (PSmmAstNode)globalBlock;
	globalBlock->scope = smmNewAstNode(nkSmmScope, a);

	lastToken = smmGetNextToken(lex);
	processGlobalSymbols(globalBlock->scope, lex, a);

	processBlock(globalBlock, lex, a);

	ibsDictGet(typeDict, "whatever"); // Just to reset internal lastKey var so it doesn't point to invalid memory

	return module;
}
