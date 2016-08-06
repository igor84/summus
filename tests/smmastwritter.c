#include "smmastwritter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void processExpression(PSmmAstNode expr, FILE* f, PSmmAllocator a) {
	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			fprintf(f, "%s:%u:%s ", nodeKindToString[expr->kind], expr->flags, expr->type->name);

			processExpression(expr->left, f, a);
			processExpression(expr->right, f, a);
			break;
		}
	case nkSmmNeg:
		{
			fprintf(f, "neg:%u:%s ", expr->flags, expr->type->name);
			processExpression(expr->left, f, a);
			break;
		}
	case nkSmmNot: case nkSmmCast:
		{
			fprintf(f, "%s:%u:%s ", nodeKindToString[expr->kind], expr->flags, expr->type->name);
			processExpression(expr->left, f, a);
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			fprintf(f, "(%s:%u:%s(", callNode->token->repr, callNode->flags, callNode->returnType->name);
			if (callNode->params) {
				size_t paramCount = callNode->params->count;
				PSmmAstNode astArg = callNode->args;
				processExpression(astArg, f, a);
				astArg = astArg->next;
				for (size_t i = 1; i < paramCount; i++) {
					fputs(", ", f);
					processExpression(astArg, f, a);
					astArg = astArg->next;
				}
				fputs(")) ", f);
			}
			break;
		}
	case nkSmmInt:
		if (expr->type->flags & tifSmmUnsigned || expr->token->sintVal >= 0) {
			fprintf(f, "%s:%u:%s:%s ", nodeKindToString[expr->kind], expr->flags, expr->token->repr, expr->type->name);
		} else {
			fprintf(f, "%s:%u:-%s:%s ", nodeKindToString[expr->kind], expr->flags, expr->token->repr, expr->type->name);
		}
		break;
	case nkSmmFloat:
		if (expr->token->floatVal > 0) {
			fprintf(f, "%s:%u:%s:%s ", nodeKindToString[expr->kind], expr->flags, expr->token->repr, expr->type->name);
		} else {
			fprintf(f, "%s:%u:-%s:%s ", nodeKindToString[expr->kind], expr->flags, expr->token->repr, expr->type->name);
		}
		break;
	case nkSmmParam: case nkSmmIdent: case nkSmmConst: case nkSmmBool:
		fprintf(f, "%s:%u:%s:%s ", nodeKindToString[expr->kind], expr->flags, expr->token->repr, expr->type->name);
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
}

static void processLocalSymbols(PSmmAstNode decl, int level, FILE* f, PSmmAllocator a) {
	while (decl) {
		if (level) fprintf(f, "%*s", level, " ");
		fprintf(f, ":%u ", decl->flags);
		if (decl->left->kind == nkSmmIdent) {
			fprintf(f, "%s:%u:%s", decl->left->token->repr, decl->left->flags, decl->type->name);
		} else if (decl->left->kind == nkSmmConst) {
			fprintf(f, "%s:%u:%s = ", decl->left->token->repr, decl->left->flags, decl->type->name);
			processExpression(decl->right, f, a);
		} else {
			assert(false && "Declaration of unknown node kind");
		}
		fputs("\n", f);
		decl = decl->next;
	}
}

static void processAssignment(PSmmAstNode stmt, FILE* f, PSmmAllocator a) {
	fprintf(f, "= %s:%u:%s  ", stmt->left->token->repr, stmt->left->flags, stmt->left->type->name);
	processExpression(stmt->right, f, a);
	fputs("\n", f);
}

static void processReturn(PSmmAstNode stmt, FILE* f, PSmmAllocator a) {
	if (stmt->left) {
		fprintf(f, "%s:%s ", nodeKindToString[stmt->kind], stmt->type->name);
		processExpression(stmt->left, f, a);
	} else {
		fprintf(f, "%s", nodeKindToString[stmt->kind]);
	}
	fputs("\n", f);
}

static void processBlock(PSmmAstBlockNode block, int level, FILE* f, PSmmAllocator a) {
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		if (level) fprintf(f, "%*s", level, " ");
		switch (stmt->kind) {
		case nkSmmBlock:
			{
				PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
				fputs("{\n", f);
				processLocalSymbols(newBlock->scope->decls, level + 4, f, a);
				processBlock(newBlock, level + 4, f, a);
				fputs("}\n", f);
				break;
			}
		case nkSmmAssignment: processAssignment(stmt, f, a); break;
		case nkSmmReturn: processReturn(stmt, f, a); break;
		default:
			processExpression(stmt, f, a);
			fputs("\n", f);
			break;
		}
		stmt = stmt->next;
	}
}

static void processGlobalSymbols(PSmmAstNode decl, FILE* f, PSmmAllocator a) {
	while (decl) {
		fprintf(f, ":%u ", decl->flags);
		if (decl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;
			fprintf(f, "%s:%u:%s(", funcNode->token->repr, funcNode->flags, funcNode->returnType->name);

			PSmmAstParamNode param = funcNode->params;
			if (param) {
				fprintf(f, "%s:%s", param->token->repr, param->type->name);
				param = param->next;
				while (param) {
					fprintf(f, ", %s:%s", param->token->repr, param->type->name);
					param = param->next;
				}
			}
			fputs(")\n", f);
			if (funcNode->body) {
				fputs("{\n", f);
				processLocalSymbols(funcNode->body->scope->decls, 4, f, a);
				processBlock(funcNode->body, 4, f, a);
				fputs("}\n", f);
			}
		} else {
			assert(decl->right && "Global var must have initializer");
			fprintf(f, "%s:%u:%s ", decl->left->token->repr, decl->left->flags, decl->left->type->name);
			if (decl->left->kind == nkSmmConst) fputs(": ", f);
			else fputs("= ", f);
			processExpression(decl->right, f, a);
			fputs("\n", f);
		}
		decl = decl->next;
	}
}

void smmOutputAst(PSmmAstNode module, FILE* f, PSmmAllocator a) {
	PSmmTypeInfo builtInTypes = smmGetBuiltInTypes();
	builtInTypes[tiSmmSoftFloat64].name = "sfloat64"; // This is how we want it written instead of "/sfloat64/"
	const char* moduleName = module->token->repr;
	fprintf(f, "MODULE %s\n", moduleName);

	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(globalBlock->scope->decls, f, a);

	processBlock(globalBlock, 0, f, a);
}
