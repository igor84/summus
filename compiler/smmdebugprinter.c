#include "smmdebugprinter.h"

#include <assert.h>
#include <stdio.h>

static bool needsParatheses(SmmAstNodeKind kind) {
	switch (kind) {
	case nkSmmNeg: case nkSmmNot: case nkSmmCast: case nkSmmCall:
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		return false;
	default:
		return true;
	}
}

static void processExpression(PSmmAstNode expr, PSmmAllocator a) {
	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			bool leftNeedsPs = needsParatheses(expr->left->kind);
			if (leftNeedsPs) fputs("(", stdout);
			processExpression(expr->left, a);
			if (leftNeedsPs) fputs(")", stdout);
			
			printf(" %s:%s ", nodeKindToString[expr->kind], expr->type->name);

			bool rightNeedsPs = needsParatheses(expr->right->kind);
			if (rightNeedsPs) fputs("(", stdout);
			processExpression(expr->right, a);
			if (rightNeedsPs) fputs(")", stdout);
			break;
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			printf("%s:%s(", nodeKindToString[expr->kind], expr->type->name);
			processExpression(expr->left, a);
			fputs(")", stdout);
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			printf("%s:%s(", callNode->token->repr, callNode->returnType->name);
			if (callNode->params) {
				size_t paramCount = callNode->params->count;
				PSmmAstNode astArg = callNode->args;
				processExpression(astArg, a);
				astArg = astArg->next;
				for (size_t i = 1; i < paramCount; i++) {
					fputs(", ", stdout);
					processExpression(astArg, a);
					astArg = astArg->next;
				}
				fputs(")", stdout);
			}
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		printf("%s:%s", expr->token->repr, expr->type->name);
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
}

static void processLocalSymbols(PSmmAstNode decl, int level, PSmmAllocator a) {
	while (decl) {
		if (level) printf("%*s", level, " ");
		if (decl->left->kind == nkSmmIdent) {
			printf("%s:%s", decl->left->token->repr, decl->type->name);
		} else if (decl->left->kind == nkSmmConst) {
			printf("%s:%s = ", decl->left->token->repr, decl->type->name);
			processExpression(decl->right, a);
		} else {
			assert(false && "Declaration of unknown node kind");
		}
		puts("");
		decl = decl->next;
	}
}

static void processAssignment(PSmmAstNode stmt, PSmmAllocator a) {
	printf("%s:%s =:%s ", stmt->left->token->repr, stmt->left->type->name, stmt->type->name);
	processExpression(stmt->right, a);
	puts("");
}

static void processReturn(PSmmAstNode stmt, PSmmAllocator a) {
	fputs("return ", stdout);
	if (stmt->left)	processExpression(stmt->left, a);
	puts("");
}

static void processBlock(PSmmAstBlockNode block, int level, PSmmAllocator a) {
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		if (level) printf("%*s", level, " ");
		switch (stmt->kind) {
		case nkSmmBlock:
			{
				PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
				puts("{");
				processLocalSymbols(newBlock->scope->decls, level + 4, a);
				processBlock(newBlock, level + 4, a);
				puts("}");
				break;
			}
		case nkSmmAssignment: processAssignment(stmt, a); break;
		case nkSmmReturn: processReturn(stmt, a); break;
		default:
			processExpression(stmt, a);
			puts("");
			break;
		}
		stmt = stmt->next;
	}
}

static void processGlobalSymbols(PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		if (decl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;
			printf("%s:%s(", funcNode->token->repr, funcNode->returnType->name);

			PSmmAstParamNode param = funcNode->params;
			if (param) {
				printf("%s:%s", param->token->repr, param->type->name);
				param = param->next;
				while (param) {
					printf(", %s:%s", param->token->repr, param->type->name);
					param = param->next;
				}
			}
			fputs(")\n", stdout);
			if (funcNode->body) {
				puts("{");
				processLocalSymbols(funcNode->body->scope->decls, 4, a);
				processBlock(funcNode->body, 4, a);
				puts("}");
			}
		} else {
			assert(decl->right && "Global var must have initializer");
			printf("%s:%s =:%s ", decl->left->token->repr, decl->left->type->name, decl->type->name);
			processExpression(decl->right, a);
			puts("");
		}
		decl = decl->next;
	}
}

void smmExecuteDebugPrintPass(PSmmAstNode module, PSmmAllocator a) {
	const char* moduleName = module->token->repr;
	printf("Module: %s\n", moduleName);
	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(globalBlock->scope->decls, a);

	puts("MAIN CODE:");
	processBlock(globalBlock, 0, a);
}
