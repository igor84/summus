#include "smmsempass.h"

#include <assert.h>

static PSmmAstNode getCastNode(PSmmAllocator a, PSmmAstNode node, PSmmTypeInfo parentType) {
	assert(parentType->kind != tiSmmSoftFloat64);
	PSmmAstNode cast = a->alloc(a, sizeof(struct SmmAstNode));
	cast->kind = nkSmmCast;
	cast->left = node;
	cast->type = parentType;
	cast->next = node->next;
	node->next = NULL;
	return cast;
}

static PSmmAstNode* fixExpressionTypes(PSmmAstNode* nodeField, PSmmTypeInfo parentType, bool isParentCast, PSmmAllocator a) {
	PSmmAstNode cast = NULL;
	PSmmAstNode node = *nodeField;
	if (parentType->isInt && node->type->isFloat) {
		//if parent is int and node is float then warning and cast
		PSmmTypeInfo type = node->type;
		// If we need to cast arbitrary float expression to int we will treat expression as float32
		if (type->kind == tiSmmSoftFloat64) type -= 2;
		if (!isParentCast) {
			cast = getCastNode(a, node, parentType);
			smmPostMessage(wrnSmmConversionDataLoss, node->token->filePos, type->name, parentType->name);
		}
	} else if (parentType->isFloat && node->type->isInt) {
		// if parent is float and node is int change it if it is literal or cast it otherwise
		if (node->kind == nkSmmInt) {
			node->kind = nkSmmFloat;
			node->type = parentType;
			node->token->floatVal = (double)node->token->uintVal;
		} else {
			if (!isParentCast) cast = getCastNode(a, node, parentType);
		}
	} else if (parentType->isInt && node->type->isInt) {
		// if both are ints just fix the sizes
		if (parentType->isUnsigned == node->type->isUnsigned) {
			if (parentType->kind > node->type->kind) {
				if (node->kind == nkSmmInt || node->isBinOp) {
					node->type = parentType; // if literal or operator
				} else {
					if (!isParentCast) cast = getCastNode(a, node, parentType);
				}
			} else { // if parent type < node type
				if (node->kind == nkSmmInt) {
					switch (parentType->kind) {
					case tiSmmUInt8: node->token->uintVal = (uint8_t)node->token->uintVal; break;
					case tiSmmUInt16: node->token->uintVal = (uint16_t)node->token->uintVal; break;
					case tiSmmUInt32: node->token->uintVal = (uint32_t)node->token->uintVal; break;
					case tiSmmInt8: node->token->sintVal = (int8_t)node->token->sintVal; break;
					case tiSmmInt16: node->token->sintVal = (int16_t)node->token->sintVal; break;
					case tiSmmInt32: node->token->sintVal = (int32_t)node->token->sintVal; break;
					default: break;
					}
					smmPostMessage(wrnSmmConversionDataLoss, node->token->filePos,
						node->type->name, parentType->name);
					node->type = parentType;
				} else {
					// No warning because operations on big numbers can give small numbers
					if (!isParentCast) cast = getCastNode(a, node, parentType);
				}
			}
		} else { // if one is uint and other is int
			if (node->kind != nkSmmInt) { // If it is not int literal
				if (!isParentCast) cast = getCastNode(a, node, parentType);
			} else {
				int64_t oldVal = node->token->sintVal;
				switch (parentType->kind) {
				case tiSmmUInt8: node->token->uintVal = (uint8_t)node->token->sintVal; break;
				case tiSmmUInt16: node->token->uintVal = (uint16_t)node->token->sintVal; break;
				case tiSmmUInt32: node->token->uintVal = (uint32_t)node->token->sintVal; break;
				case tiSmmInt8: node->token->sintVal = (int8_t)node->token->uintVal; break;
				case tiSmmInt16: node->token->sintVal = (int16_t)node->token->uintVal; break;
				case tiSmmInt32: node->token->sintVal = (int32_t)node->token->uintVal; break;
				default: break;
				}
				if (oldVal < 0 || oldVal != node->token->sintVal) {
					smmPostMessage(wrnSmmConversionDataLoss, node->token->filePos, node->type->name, parentType->name);
				}
				node->token->kind = tkSmmUInt;
				node->type = parentType;
			}
		}
	} else if ((parentType->isFloat && node->type->isFloat)) {
		// if both are floats just fix the sizes
		if (node->type->kind == tiSmmSoftFloat64) {
			node->type = parentType;
		} else { // if they are different size
			if (!isParentCast) cast = getCastNode(a, node, parentType);
		}
	} else if (parentType->kind == tiSmmBool && node->type->kind != tiSmmBool) {
		// If parent is bool but node is not bool we need to add compare with 0
		switch (node->kind) {
		case nkSmmInt: case nkSmmFloat:
			node->type = parentType;
			node->token->boolVal = node->token->sintVal != 0;
			break;
		default:
			{
				PSmmToken zeroToken = a->alloc(a, sizeof(struct SmmToken));
				zeroToken->filePos = node->token->filePos;
				zeroToken->kind = tkSmmInt;
				zeroToken->repr = "0";

				PSmmToken notEqToken = a->alloc(a, sizeof(struct SmmToken));
				zeroToken->filePos = node->token->filePos;
				zeroToken->kind = tkSmmNotEq;
				zeroToken->repr = "!=";

				PSmmAstNode zeroNode = a->alloc(a, sizeof(struct SmmAstNode));
				zeroNode->isConst = true;
				zeroNode->kind = nkSmmInt;
				zeroNode->token = zeroToken;
				zeroNode->type = node->type;
				if (zeroNode->type->isFloat) {
					zeroToken->kind = tkSmmFloat;
				}

				PSmmAstNode notEqNode = a->alloc(a, sizeof(struct SmmAstNode));
				notEqNode->isBinOp = true;
				notEqNode->isConst = node->isConst;
				notEqNode->kind = nkSmmNotEq;
				notEqNode->left = node;
				notEqNode->right = zeroNode;
				notEqNode->token = notEqToken;
				notEqNode->type = parentType;
				*nodeField = notEqNode;
				break;
			}
		}
	} else if (parentType->kind != tiSmmBool && node->type->kind == tiSmmBool && !isParentCast) {
		// If parent is not bool but node is we issue an error
		smmPostMessage(errSmmUnexpectedBool, node->token->filePos);
	}

	if (node->type->kind == tiSmmSoftFloat64) {
		node->type -= 2; // Change to float32
	}

	if (cast) {
		*nodeField = cast;
		nodeField = &cast->left;
	}

	return nodeField;
}

static void processExpression(PSmmAstNode* exprField, PSmmTypeInfo parentType, bool isParentCast, PSmmAllocator a) {
	PSmmAstNode expr = *exprField;

	if (parentType != expr->type) {
		exprField = fixExpressionTypes(exprField, parentType, isParentCast, a);
	}

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp: case nkSmmXorOp:
		processExpression(&expr->left, expr->type, false, a);
		processExpression(&expr->right, expr->type, false, a);
		break;
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			PSmmTypeInfo newParentType;
			if (expr->left->type->kind > expr->right->type->kind) newParentType = expr->left->type;
			else newParentType = expr->right->type;
			processExpression(&expr->left, newParentType, false, a);
			processExpression(&expr->right, newParentType, false, a);
			break;
		}
	case nkSmmNeg: case nkSmmNot:
		processExpression(&expr->left, expr->type, false, a);
		break;
	case nkSmmCast:
		processExpression(&expr->left, expr->type, true, a);
		if (expr->type == expr->left->type) {
			 // Cast was succesfully lowered so it is not needed any more
			*exprField = expr->left;
			a->free(a, expr);
			expr = NULL;
		}
		break;
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			if (callNode->params) {
				size_t paramCount = callNode->params->count;
				PSmmAstParamNode astParam = callNode->params;
				PSmmAstNode* astArg = &callNode->args;
				for (size_t i = 0; i < paramCount; i++) {
					processExpression(astArg, astParam->type, false, a);
					astArg = &(*astArg)->next;
					astParam = astParam->next;
				}
			}
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		// No processing needed
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
}

static void processLocalSymbols(PSmmAstNode decl, PSmmAllocator a) {
	while (decl) {
		if (decl->left->kind == nkSmmConst) {
			processExpression(&decl->right, decl->type, false, a);
		}
		decl = decl->next;
	}
}

static void processBlock(PSmmAstBlockNode block, PSmmAllocator a) {
	PSmmAstNode* stmtField = &block->stmts;
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
		case nkSmmAssignment:
			assert(stmt->type == stmt->left->type);
			processExpression(&stmt->right, stmt->type, false, a);
			break;
		case nkSmmReturn:
			if (stmt->left) processExpression(&stmt->left, stmt->type, false, a);
			break;
		default:
			// We treat softFloat as float32
			if (stmt->type->kind == tiSmmSoftFloat64) stmt->type -= 2;
			processExpression(stmtField, stmt->type, stmt->kind == nkSmmCast, a); break;
		}
		stmtField = &stmt->next;
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
			assert(decl->left->type == decl->type);
			processExpression(&decl->right, decl->type, false, a);
		}
		decl = decl->next;
	}
}

void smmExecuteSemPass(PSmmAstNode module, PSmmAllocator a) {
	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);
	processGlobalSymbols(globalBlock->scope->decls, a);

	processBlock(globalBlock, a);
}
