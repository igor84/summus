/*
 * This file is used only as a template for implementing new passes. It has nicely organized code
 * that properly walks the AST. You use it by copying the entire file into smmSpecificPass.c where
 * you then modify each function so you can do the processing you need. For example files like
 * smmsempass.c, smmllvmcodegen.c and smmdebugprinter.c are made by copying and modifing this file.
 * Also if new node types are added this class should be modified with proper code to walk through
 * new nodes.
 *
 * In the beggining semPass and llvmcodegen were implemented completely separately and debugPrinter
 * existed only as one function full of hacks so it works properly for specific nodes. At that
 * point there was a clear feeling that it would make things much easier if walking an AST was
 * extracted to a resusable algorithm. First attempt was to create a Pass "class" that will do the
 * walking and call supplied functions for processing each node but it was soon realized that
 * different passes had too many different requirements for processing. Not only that some passes
 * needed pre-order, some in-order and some post-order processing or a combination of these but
 * also needed to pass some custom data around easily. So it was decided to make a template that
 * can be easily copied and modified for specific needs.
 */
#include "smmcommon.h"
#include "smmutil.h"
#include "smmparser.h"

#include <assert.h>
#include <stdio.h> // -TODO: Remove this if not needed (template uses printf function to avoid variable not used warnings)

static void* processExpression(PSmmAstNode expr, PSmmAllocator a) {
	// -TODO: Process expr node here
	void* res = NULL;

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			void* left = processExpression(expr->left, a);
			void* right = processExpression(expr->right, a);
			printf("Left operand: %p, Right operand: %p", left, right);
			// -TODO: Process binary expression here
			break;
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			void* operand = processExpression(expr->left, a);
			printf("Operand: %p", operand);
			// -TODO: Process unary expression here
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			if (callNode->params) {
				size_t paramCount = callNode->params->count;
				PSmmAstParamNode astParam = callNode->params;
				PSmmAstNode astArg = callNode->args;
				for (size_t i = 0; i < paramCount; i++) {
					processExpression(astArg, a);
					astArg = astArg->next;
					astParam = astParam->next;
					// -TODO: Process args here
				}
			}
			// -TODO: process call node here
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
		// -TODO: Process symbol here
		break;
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		// -TODO: Process literal here
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
	return res;
}

static void processLocalSymbols(PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		// -TODO: process local symbol decl->left
		if (decl->left->kind == nkSmmIdent) {
			// -TODO: process variable
		} else if (decl->left->kind == nkSmmConst) {
			processExpression(decl->right, a);
			// -TODO: process constant
		} else {
			assert(false && "Declaration of unknown node kind");
		}
		decl = decl->next;
	}
}

static void processAssignment(PSmmAstNode stmt, PSmmAllocator a) {
	// -TODO: Process assignment to assignment->left here
	void* val = processExpression(stmt->right, a);
	printf("Assignment value: %p", val);
}

static void processReturn(PSmmAstNode stmt, PSmmAllocator a) {
	// -TODO: Process return statement here
	if (stmt->left)	processExpression(stmt->left, a);
}

static void processBlock(PSmmAstBlockNode block, PSmmAllocator a) {
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		switch (stmt->kind) {
		case nkSmmBlock:
			{
				PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
				processLocalSymbols(newBlock->scope->decls, a);
				processBlock(newBlock, a);
				break;
			}
		case nkSmmAssignment: processAssignment(stmt, a); break;
		case nkSmmReturn: processReturn(stmt, a); break;
		default:
			processExpression(stmt, a); break;
		}
		stmt = stmt->next;
	}
}

static void processGlobalSymbols(PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		if (decl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;
			if (funcNode->body) {
				processLocalSymbols(funcNode->body->scope->decls, a);
				processBlock(funcNode->body, a);
			}
		} else {
			assert(decl->right && "Global var must have initializer");
			void* val = processExpression(decl->right, a);
			printf("Assignment value: %p", val);
			// -TODO: Process global symbol decl->left here
		}
		decl = decl->next;
	}
}

void smmExecuteXPass(PSmmAstNode module, PSmmAllocator a) {
	const char* moduleName = module->token->repr;
	printf("Module name: %s", moduleName);
	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(globalBlock->scope->decls, a);

	processBlock(globalBlock, a);
}
