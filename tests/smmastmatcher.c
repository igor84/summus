#include "smmastmatcher.h"
#include <assert.h>

static const char* NODES_DONT_MATCH = "Node kinds don't match";
static const char* NODES_TYPES_DONT_MATCH = "Node's types don't match";
static const char* NODES_REPRS_DONT_MATCH = "Nodes representations don't match";

static void processStatement(CuTest* tc, PSmmAstNode exStmt, PSmmAstNode gotStmt);
static void processBlock(CuTest* tc, PSmmAstBlockNode exBlock, PSmmAstBlockNode gotBlock);

static void assertNodeFlagsEqual(CuTest* tc, PSmmAstNode ex, PSmmAstNode got) {
	CuAssertUIntEquals_Msg(tc, "Ident flag doesn't match", ex->isIdent, got->isIdent);
	CuAssertUIntEquals_Msg(tc, "Const flag doesn't match", ex->isConst, got->isConst);
	CuAssertUIntEquals_Msg(tc, "BinOp flag doesn't match", ex->isBinOp, got->isBinOp);
}

static void assertNodesEqual(CuTest* tc, PSmmAstNode ex, PSmmAstNode got) {
	CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, ex->kind, got->kind);
	if (ex->type && got->type) {
		CuAssertIntEquals_Msg(tc, NODES_TYPES_DONT_MATCH, ex->type->kind, got->type->kind);
	} else {
		CuAssertPtrEquals_Msg(tc, NODES_TYPES_DONT_MATCH, ex->type, got->type);
	}
	assertNodeFlagsEqual(tc, ex, got);
	if (got->kind == nkSmmCast) return;
	if (got->token && ex->token) {
		CuAssertStrEquals_Msg(tc, NODES_REPRS_DONT_MATCH, ex->token->repr, got->token->repr);
	} else {
		CuAssertPtrEquals_Msg(tc, "Token presence not matched", ex->token, got->token);
	}
}

static void processExpression(CuTest* tc, PSmmAstNode exExpr, PSmmAstNode gotExpr) {
	assertNodesEqual(tc, exExpr, gotExpr);

	switch (gotExpr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			processExpression(tc, exExpr->left, gotExpr->left);
			processExpression(tc, exExpr->left, gotExpr->left);
			break;
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			processExpression(tc, exExpr->left, gotExpr->left);
			break;
		}
	case nkSmmCall:
		{
			PSmmAstCallNode exCallNode = (PSmmAstCallNode)exExpr;
			PSmmAstCallNode gotCallNode = (PSmmAstCallNode)gotExpr;
			PSmmAstNode exArg = exCallNode->args;
			PSmmAstNode gotArg = gotCallNode->args;
			while (gotArg) {
				CuAssertPtrNotNullMsg(tc, "Got unexpected arg in call", exArg);
				processExpression(tc, exArg, gotArg);
				exArg = exArg->next;
				gotArg = gotArg->next;
			}
			CuAssertPtrEquals_Msg(tc, "Call args don't match", exArg, gotArg);
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
		// No additional matching needed
		break;
	case nkSmmInt:
		CuAssertUIntEquals_Msg(tc, "Int values does not match", exExpr->token->uintVal, gotExpr->token->uintVal);
		break;
	case nkSmmFloat:
		CuAssertDblEquals_Msg(tc, "Float values does not match", exExpr->token->floatVal, gotExpr->token->floatVal, 0);
		break;
	case nkSmmBool:
		CuAssertUIntEquals_Msg(tc, "Bool values does not match", exExpr->token->boolVal, gotExpr->token->boolVal);
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
}

static void processLocalSymbols(CuTest* tc, PSmmAstDeclNode exDecl, PSmmAstDeclNode gotDecl) {
	while (gotDecl) {
		CuAssertPtrNotNullMsg(tc, "Got more global declarations than expected", exDecl);
		CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exDecl->kind, gotDecl->kind);
		CuAssertPtrEquals_Msg(tc, NODES_TYPES_DONT_MATCH, exDecl->left->left->type, gotDecl->left->left->type);
		assertNodeFlagsEqual(tc, (PSmmAstNode)exDecl, (PSmmAstNode)gotDecl);

		assertNodesEqual(tc, exDecl->left, gotDecl->left);
		assertNodesEqual(tc, exDecl->left->left, gotDecl->left->left);
		if (gotDecl->left->left->kind == nkSmmConst) {
			processExpression(tc, exDecl->left->right, gotDecl->left->right);
		}
		exDecl = exDecl->nextDecl;
		gotDecl = gotDecl->nextDecl;
	}
}

static void processAssignment(CuTest* tc, PSmmAstNode exStmt, PSmmAstNode gotStmt) {
	assertNodesEqual(tc, exStmt->left, gotStmt->left);
	processExpression(tc, exStmt->right, gotStmt->right);
}

static void processReturn(CuTest* tc, PSmmAstNode exStmt, PSmmAstNode gotStmt) {
	if (gotStmt->left) {
		CuAssertPtrNotNullMsg(tc, "Got unexpected return expression", exStmt);
		processExpression(tc, exStmt->left, gotStmt->left);
	} else {
		CuAssertPtrEquals_Msg(tc, "No expected return expression", exStmt->left, gotStmt->left);
	}
}

static void processStatement(CuTest* tc, PSmmAstNode exStmt, PSmmAstNode gotStmt) {
	CuAssertPtrNotNullMsg(tc, "Got more statements than expected", exStmt);
	assertNodesEqual(tc, exStmt, gotStmt);
	switch (gotStmt->kind) {
	case nkSmmBlock:
		{
			PSmmAstBlockNode newExBlock = (PSmmAstBlockNode)exStmt;
			PSmmAstBlockNode newGotBlock = (PSmmAstBlockNode)gotStmt;
			CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, newExBlock->scope->kind, newGotBlock->scope->kind);
			processLocalSymbols(tc, newExBlock->scope->decls, newGotBlock->scope->decls);
			processBlock(tc, newExBlock, newGotBlock);
			break;
		}
	case nkSmmAssignment: processAssignment(tc, exStmt, gotStmt); break;
	case nkSmmDecl:
		assertNodesEqual(tc, exStmt->left, gotStmt->left);
		processAssignment(tc, exStmt->left, gotStmt->left);
		break;
	case nkSmmReturn: processReturn(tc, exStmt, gotStmt); break;
	default:
		processExpression(tc, exStmt, gotStmt); break;
	}
}

static void processBlock(CuTest* tc, PSmmAstBlockNode exBlock, PSmmAstBlockNode gotBlock) {
	PSmmAstNode exStmt = exBlock->stmts;
	PSmmAstNode gotStmt = gotBlock->stmts;
	while (gotStmt) {
		processStatement(tc, exStmt, gotStmt);
		exStmt = exStmt->next;
		gotStmt = gotStmt->next;
	}
}

static void processGlobalSymbols(CuTest* tc, PSmmAstDeclNode exDecl, PSmmAstDeclNode gotDecl) {
	while (gotDecl) {
		CuAssertPtrNotNullMsg(tc, "Got more global declarations than expected", exDecl);
		CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exDecl->kind, gotDecl->kind);

		assertNodesEqual(tc, exDecl->left, gotDecl->left);
		if (gotDecl->left->kind == nkSmmFunc) {
			assertNodesEqual(tc, exDecl->left, gotDecl->left);
			PSmmAstFuncDefNode exFuncNode = (PSmmAstFuncDefNode)exDecl->left;
			PSmmAstFuncDefNode gotFuncNode = (PSmmAstFuncDefNode)gotDecl->left;
			PSmmAstParamNode exParam = exFuncNode->params;
			PSmmAstParamNode gotParam = gotFuncNode->params;
			while (gotParam) {
				CuAssertPtrNotNullMsg(tc, "Got more parameters than expected", exDecl);
				assertNodesEqual(tc, (PSmmAstNode)exParam, (PSmmAstNode)gotParam);
				exParam = exParam->next;
				gotParam = gotParam->next;
			}
			CuAssertPtrEquals_Msg(tc, "Unexpected number of parameters", exParam, gotParam);
			if (exFuncNode->params) {
				CuAssertUIntEquals_Msg(tc, "Unexpected parameters count", exFuncNode->params->count, gotFuncNode->params->count);
			}
			if (gotFuncNode->body) {
				CuAssertPtrNotNullMsg(tc, "Unexpected function body", exFuncNode->body);
				CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exFuncNode->body->kind, gotFuncNode->body->kind);
				CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exFuncNode->body->scope->kind, gotFuncNode->body->scope->kind);
				processLocalSymbols(tc, exFuncNode->body->scope->decls, gotFuncNode->body->scope->decls);
				processBlock(tc, exFuncNode->body, gotFuncNode->body);
			} else {
				CuAssertPtrEquals_Msg(tc, "No expected function body", exFuncNode->body, gotFuncNode->body);
			}
		} else {
			CuAssertPtrEquals_Msg(tc, NODES_TYPES_DONT_MATCH, exDecl->left->left->type, gotDecl->left->left->type);
			assertNodesEqual(tc, exDecl->left->left, gotDecl->left->left);
			CuAssertPtrNotNullMsg(tc, "Global decl must have initializer", gotDecl->left->right);
			if (exDecl->left->left->isConst) {
				processExpression(tc, exDecl->left->right, gotDecl->left->right);
			}
		}
		exDecl = exDecl->nextDecl;
		gotDecl = gotDecl->nextDecl;
	}
}

void smmAssertASTEquals(CuTest* tc, PSmmAstNode ex, PSmmAstNode got) {
	CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, ex->kind, got->kind);
	CuAssertStrEquals_Msg(tc, "Module names don't match", ex->token->repr, got->token->repr);
	PSmmAstBlockNode exBlock = (PSmmAstBlockNode)ex->next;
	PSmmAstBlockNode gotBlock = (PSmmAstBlockNode)got->next;
	CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exBlock->kind, gotBlock->kind);
	CuAssertIntEquals_Msg(tc, NODES_DONT_MATCH, exBlock->scope->kind, gotBlock->scope->kind);
	processGlobalSymbols(tc, exBlock->scope->decls, gotBlock->scope->decls);

	processBlock(tc, exBlock, gotBlock);
}
