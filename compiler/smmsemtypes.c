#include "smmsemtypes.h"

#include <assert.h>

static PSmmAstNode getCastNode(PSmmAllocator a, PSmmAstNode node, PSmmAstNode parent) {
	if (parent->kind == nkSmmCast) return NULL;
	PSmmAstNode cast = (PSmmAstNode)a->alloc(a, sizeof(struct SmmAstNode));
	cast->kind = nkSmmCast;
	cast->left = node;
	cast->type = parent->type;
	return cast;
}

static void fixExpressionTypes(PSmmModuleData data, PSmmAstNode node, PSmmAstNode parent) {
	PSmmAstNode cast = NULL;
	// TODO(igors): See if we can loose cast node from code if we manage to lower it or just ignore it and let LLVM do it
	if (parent->type != node->type) {
		if ((parent->type->flags & tifSmmInt) && (node->type->flags & tifSmmFloat)) {
			//if parent is int and node is float then warning and cast
			PSmmTypeInfo type = node->type;
			// If we need to cast arbitrary float expression to int we will treat expression as float32
			if (type->kind == tiSmmSoftFloat64) type -= 2;
			smmPostMessage(wrnSmmConversionDataLoss, parent->token->filePos, type->name, parent->type->name);
			cast = getCastNode(data->allocator, node, parent);
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
					if (node->kind == nkSmmInt || node->right) {
						node->type = parent->type; // if literal or operator
					} else {
						cast = getCastNode(data->allocator, node, parent);
					}
				} else { // if parent type < node type
					if (node->kind == nkSmmInt) {
						switch (parent->type->kind) {
						case tiSmmUInt8: node->token->uintVal = (uint8_t)node->token->uintVal; break;
						case tiSmmUInt16: node->token->uintVal = (uint8_t)node->token->uintVal; break;
						case tiSmmUInt32: node->token->uintVal = (uint8_t)node->token->uintVal; break;
						case tiSmmInt8: node->token->sintVal = (int8_t)node->token->sintVal; break;
						case tiSmmInt16: node->token->sintVal = (int8_t)node->token->sintVal; break;
						case tiSmmInt32: node->token->sintVal = (int8_t)node->token->sintVal; break;
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
				if (node->kind != nkSmmInt) {
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
		} else if ((parent->type->flags & node->type->flags & tifSmmBool) == 0) {
			// TODO(igors): If only one is bool then Err
		}

		if (node->type->kind == tiSmmSoftFloat64)
			node->type -= 2; // Change to float32
	}

	if (cast) {
		if (node == parent->left) {
			parent->left = cast;
		} else if (node == parent->right) {
			parent->right = cast;
		} else {
			assert(false && "Casting can't be done because parent and child are not related");
		}
	}

	if (node->kind == nkSmmCall) {	
		PSmmAstNode curArg = node->nextArg;
		PSmmAstNode curParam = node->nextParam;
		while (curParam && curArg) {
			fixExpressionTypes(data, curArg, curParam);
			curParam = curParam->nextParam;
			curArg = curArg->nextArg;
		}
	} else {
		if (node->left) fixExpressionTypes(data, node->left, node);
		if (node->right) fixExpressionTypes(data, node->right, node);
	}
}

/********************************************************
API Functions
*********************************************************/

void smmAnalyzeTypes(PSmmModuleData data) {
	if (!data || !data->module) return;
	PSmmAstNode parent = data->module;
	if (parent->kind == nkSmmProgram) parent = parent->next;
	while (parent) {
		if (parent->kind == nkSmmBlock) {
			PSmmAstNode curDecl = parent->scope->next;
			while (curDecl) {
				if (curDecl->left->kind == nkSmmConst) {
					assert(curDecl->type == curDecl->left->type);
					fixExpressionTypes(data, curDecl->right, curDecl);
				}
				curDecl = curDecl->next;
			}
		} else if (parent->kind == nkSmmAssignment) {
			assert(parent->type == parent->left->type);
			fixExpressionTypes(data, parent->right, parent);
		} else {
			// We treat softFloat as float 32 in order to be consistent
			if (parent->type->kind == tiSmmSoftFloat64) parent->type -= 2;
			if (parent->left) fixExpressionTypes(data, parent->left, parent);
			if (parent->right) fixExpressionTypes(data, parent->right, parent);
		}
		parent = parent->next;
	}
}
