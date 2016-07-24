#include "smmsemtypes.h"

#include <assert.h>

static PSmmAstNode getCastNode(PSmmAllocator a, PSmmAstNode node, PSmmAstNode parent) {
	assert(parent->type->kind != tiSmmSoftFloat64);
	if (parent->kind == nkSmmCast) return NULL;
	PSmmAstNode cast = a->alloc(a, sizeof(struct SmmAstNode));
	cast->kind = nkSmmCast;
	cast->left = node;
	cast->type = parent->type;
	return cast;
}

static PSmmAstNode* fixExpressionTypes(PSmmModuleData data, PSmmAstNode* nodeField, PSmmAstNode parent) {
	PSmmAstNode cast = NULL;
	PSmmAstNode node = *nodeField;
	if ((parent->type->flags & tifSmmInt) && (node->type->flags & tifSmmFloat)) {
		//if parent is int and node is float then warning and cast
		PSmmTypeInfo type = node->type;
		// If we need to cast arbitrary float expression to int we will treat expression as float32
		if (type->kind == tiSmmSoftFloat64) type -= 2;
		cast = getCastNode(data->allocator, node, parent);
		// if parent is cast node cast will be NULL and in that case we don't need warning
		if (cast) smmPostMessage(wrnSmmConversionDataLoss, parent->token->filePos, type->name, parent->type->name);
	} else if ((parent->type->flags & tifSmmFloat) && (node->type->flags & tifSmmInt)) {
		// if parent is float and node is int change it if it is literal or cast it otherwise
		if (node->kind == nkSmmInt) {
			node->kind = nkSmmFloat;
			node->type = parent->type;
			node->token->floatVal = (double)node->token->uintVal;
		} else {
			cast = getCastNode(data->allocator, node, parent);
		}
	} else if ((parent->type->flags & node->type->flags & tifSmmInt)) {
		// if both are ints just fix the sizes
		if (parent->type->flags == node->type->flags) {
			if (parent->type->kind > node->type->kind) {
				if (node->kind == nkSmmInt || (node->flags & nfSmmBinOp)) {
					node->type = parent->type; // if literal or operator
				} else {
					cast = getCastNode(data->allocator, node, parent);
				}
			} else { // if parent type < node type
				if (node->kind == nkSmmInt) {
					switch (parent->type->kind) {
					case tiSmmUInt8: node->token->uintVal = (uint8_t)node->token->uintVal; break;
					case tiSmmUInt16: node->token->uintVal = (uint16_t)node->token->uintVal; break;
					case tiSmmUInt32: node->token->uintVal = (uint32_t)node->token->uintVal; break;
					case tiSmmInt8: node->token->sintVal = (int8_t)node->token->sintVal; break;
					case tiSmmInt16: node->token->sintVal = (int16_t)node->token->sintVal; break;
					case tiSmmInt32: node->token->sintVal = (int32_t)node->token->sintVal; break;
					default: break;
					}
					smmPostMessage(wrnSmmConversionDataLoss, parent->token->filePos,
						node->type->name, parent->type->name);
					node->type = parent->type;
				} else {
					// No warning because operations on big numbers can give small numbers
					cast = getCastNode(data->allocator, node, parent);
				}
			}
		} else { // if one is uint and other is int
			if (node->kind != nkSmmInt) { // If it is not int literal
				cast = getCastNode(data->allocator, node, parent);
			} else {
				int64_t oldVal = node->token->sintVal;
				switch (parent->type->kind) {
				case tiSmmUInt8: node->token->uintVal = (uint8_t)node->token->sintVal; break;
				case tiSmmUInt16: node->token->uintVal = (uint16_t)node->token->sintVal; break;
				case tiSmmUInt32: node->token->uintVal = (uint32_t)node->token->sintVal; break;
				case tiSmmInt8: node->token->sintVal = (int8_t)node->token->uintVal; break;
				case tiSmmInt16: node->token->sintVal = (int16_t)node->token->uintVal; break;
				case tiSmmInt32: node->token->sintVal = (int32_t)node->token->uintVal; break;
				default: break;
				}
				if (oldVal < 0 || oldVal != node->token->sintVal) {
					smmPostMessage(wrnSmmConversionDataLoss, parent->token->filePos, node->type->name, parent->type->name);
				}
				node->token->kind = tkSmmUInt;
				node->type = parent->type;
			}
		}
	} else if ((parent->type->flags & node->type->flags & tifSmmFloat)) {
		// if both are floats just fix the sizes
		if (node->type->kind == tiSmmSoftFloat64) {
			node->type = parent->type;
		} else { // if they are different size
			cast = getCastNode(data->allocator, node, parent);
		}
	} else if (parent->type->kind == tiSmmBool && node->type->kind != tiSmmBool) {
		// If parent is bool but node is not bool we need to add compare with 0
		switch (node->kind) {
		case nkSmmInt: case nkSmmFloat:
			node->type = parent->type;
			node->token->boolVal = node->token->sintVal != 0;
			break;
		default:
			{
				PSmmToken zeroToken = data->allocator->alloc(data->allocator, sizeof(struct SmmToken));
				zeroToken->filePos = node->token->filePos;
				zeroToken->kind = tkSmmInt;
				zeroToken->repr = "0";

				PSmmToken notEqToken = data->allocator->alloc(data->allocator, sizeof(struct SmmToken));
				zeroToken->filePos = node->token->filePos;
				zeroToken->kind = tkSmmNotEq;
				zeroToken->repr = "!=";
				
				PSmmAstNode zeroNode = data->allocator->alloc(data->allocator, sizeof(struct SmmAstNode));
				zeroNode->flags = nfSmmConst;
				zeroNode->kind = nkSmmInt;
				zeroNode->token = zeroToken;
				zeroNode->type = node->type;
				if (zeroNode->type->flags & tifSmmFloat) {
					zeroToken->kind = tkSmmFloat;
				}
				
				PSmmAstNode notEqNode = data->allocator->alloc(data->allocator, sizeof(struct SmmAstNode));
				notEqNode->flags = nfSmmBinOp | (node->flags & nfSmmConst);
				notEqNode->kind = nkSmmNotEq;
				notEqNode->left = node;
				notEqNode->right = zeroNode;
				notEqNode->token = notEqToken;
				notEqNode->type = parent->type;
				*nodeField = notEqNode;
				break;
			}
		}
	} else if (parent->type->kind != tiSmmBool && node->type->kind == tiSmmBool && parent->kind != nkSmmCast) {
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

static void checkExpressionTypes(PSmmModuleData data, PSmmAstNode* nodeField, PSmmAstNode parent) {
	PSmmAstNode node = *nodeField;
	if (parent->type != node->type) {
		nodeField = fixExpressionTypes(data, nodeField, parent);
	}

	switch (node->kind) {
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)node;
			PSmmAstNode* curArgField = &callNode->args;
			PSmmAstParamNode curParam = callNode->params;
			while (curParam && *curArgField) {
				checkExpressionTypes(data, curArgField, (PSmmAstNode)curParam);
				curParam = curParam->next;
				curArgField = &(*curArgField)->next;
			}
			break;
		}
	case nkSmmEq: case nkSmmNotEq:case nkSmmGt:case nkSmmGtEq:case nkSmmLt:case nkSmmLtEq:
		if (node->left->type->kind > node->right->type->kind) {
			checkExpressionTypes(data, &node->right, node->left);
		} else {
			checkExpressionTypes(data, &node->left, node->right);
		}
		break;
	case nkSmmParam: break; // We don't need to do anything for params
	default:
		if (node->left) checkExpressionTypes(data, &node->left, node);
		if (node->right) checkExpressionTypes(data, &node->right, node);

		if (node->kind == nkSmmCast && node->type == node->left->type) {
			// Cast was succesfully lowered so it is not needed any more
			*nodeField = node->left;
		}
		break;
	}
}

void analyzeTypesInBlock(PSmmModuleData data, PSmmAstBlockNode block) {
	PSmmAstNode curDecl = block->scope->decls;
	while (curDecl) {
		if (curDecl->left->kind == nkSmmConst) {
			assert(curDecl->type == curDecl->left->type);
			checkExpressionTypes(data, &curDecl->right, curDecl);
		} else if (curDecl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode func = (PSmmAstFuncDefNode)curDecl->left;
			if (func->body)	analyzeTypesInBlock(data, func->body);
		}
		curDecl = curDecl->next;
	}
	PSmmAstNode parent = block->stmts;
	while (parent) {
		if (parent->kind == nkSmmBlock) {
			analyzeTypesInBlock(data, (PSmmAstBlockNode)parent);
		} else if (parent->kind == nkSmmAssignment) {
			assert(parent->type == parent->left->type);
			checkExpressionTypes(data, &parent->right, parent);
		} else {
			// We treat softFloat as float 32 in order to be consistent
			if (parent->type->kind == tiSmmSoftFloat64) parent->type -= 2;
			if (parent->left) checkExpressionTypes(data, &parent->left, parent);
			if (parent->right) checkExpressionTypes(data, &parent->right, parent);
		}
		parent = parent->next;
	}
}

/********************************************************
API Functions
*********************************************************/

void smmAnalyzeTypes(PSmmModuleData data) {
	if (!data || !data->module) return;
	PSmmAstNode parent = data->module;
	if (parent->kind == nkSmmProgram) parent = parent->next;
	analyzeTypesInBlock(data, (PSmmAstBlockNode)parent);
}
