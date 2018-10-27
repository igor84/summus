#include "smmtypeinference.h"

#include <assert.h>
#include <string.h>

struct TIData {
	PIbsDict idents;
	PSmmMsgs msgs;
	PSmmAstDeclNode funcDecls;
	uint32_t isInMainCode : 1;
	uint32_t acceptOnlyConsts : 1;
};
typedef struct TIData* PTIData;

static PSmmTypeInfo processExpression(PSmmAstNode expr, PTIData tidata, PIbsAllocator a);
static bool processStatement(PSmmAstNode stmt, PTIData tidata, PIbsAllocator a);
static void processBlock(PSmmAstBlockNode block, PTIData tidata, PIbsAllocator a);

static PSmmToken newToken(int kind, const char* repr, struct SmmFilePos filePos, PIbsAllocator a) {
	PSmmToken res = ibsAlloc(a, sizeof(struct SmmToken));
	res->kind = kind;
	res->repr = repr;
	res->filePos = filePos;
	return res;
}

#define FUNC_SIGNATURE_LENGTH 4 * 1024

static char* getFuncsSignatureAsString(PSmmAstFuncDefNode funcs, char* buf) {
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
		buf[len++] = ' ';
		curFunc = curFunc->nextOverload;
	}
	buf[len - 1] = 0;
	return buf;
}

static char* getFuncCallAsString(const char* name, PSmmAstNode args, char* buf) {
	size_t len = strlen(name);
	strncpy(buf, name, len);
	buf[len++] = '(';
	PSmmAstNode curArg = args;
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

static bool isUpcastPossible(PSmmTypeInfo srcType, PSmmTypeInfo dstType) {
	if (!srcType || !dstType || srcType->kind == tiSmmVoid || dstType->kind == tiSmmVoid) return false;
	bool bothInts = dstType->isInt && srcType->isInt && (dstType->isUnsigned == srcType->isUnsigned);
	bool bothFloats = dstType->isFloat && srcType->isFloat;
	bool floatAndSoftFloat = srcType->kind == tiSmmSoftFloat64 && dstType->isFloat;
	bool sameKindAndDstBigger = floatAndSoftFloat || ((bothInts || bothFloats) && (dstType->kind > srcType->kind));
	bool intToFloat = srcType->isInt && dstType->isFloat;
	return sameKindAndDstBigger || intToFloat;
}

static PSmmAstFuncDefNode findFuncWithMatchingParams(PSmmAstNode argNode, PSmmAstFuncDefNode curFunc, bool softMatch) {
	PSmmAstFuncDefNode softFunc = NULL;
	while (curFunc) {
		PSmmAstNode curArg = argNode;
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
	if (curFunc) return curFunc;
	if (!softMatch) return NULL;
	return softFunc;
}

/**
* When func node is read from identDict it is linked with other overloaded funcs (funcs
* with same name but different parameters) over the nextOverload pointer. Each func node
* has a list of params nodes. The given node also has a list of concrete args with which
* it is called. This function goes through all overloaded funcs and tries to match given
* arguments with each function's parameters. If exact match is not found but a match
* where some arguments can be upcast to a bigger type of the same kind (like from int8
* to int32 but not to uint32) that func will be used. If there are multiple such funcs
* we will say that it is undefined which one will be called (because compiler
* implementation can change) and that explicit casts should be used in such cases.
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
static void resolveCall(PSmmAstCallNode node, PSmmAstFuncDefNode curFunc, PSmmMsgs msgs) {
	PSmmAstFuncDefNode foundFunc = findFuncWithMatchingParams(node->args, curFunc, true);
	if (foundFunc) {
		node->returnType = foundFunc->returnType;
		node->params = foundFunc->params;
		node->token->stringVal = foundFunc->token->stringVal; // Copy mangled name
		return;
	}
	node->returnType = &builtInTypes[tiSmmUnknown];
	// Report error that we got a call with certain arguments but expected one of...
	char callWithArgsBuf[FUNC_SIGNATURE_LENGTH] = { 0 };
	char funcSignatures[8 * FUNC_SIGNATURE_LENGTH] = { 0 };
	char* callWithArgs = getFuncCallAsString(node->token->repr, node->args, callWithArgsBuf);
	char* signatures = getFuncsSignatureAsString(curFunc, funcSignatures);
	smmPostMessage(msgs, errSmmGotBadArgs, node->token->filePos, callWithArgs, signatures);
}

static PSmmTypeInfo getCommonTypeFromOperands(PSmmTypeInfo leftType, PSmmTypeInfo rightType) {
	PSmmTypeInfo type;
	if (leftType->isInt && rightType->isInt) {
		// if both are ints we need to select bigger type but if only one is signed result should be signed
		bool leftUnsigned = leftType->isUnsigned;
		bool rightUnsigned = rightType->isUnsigned;
		type = (leftType->sizeInBytes > rightType->sizeInBytes) ? leftType : rightType;
		if (leftUnsigned != rightUnsigned && (type->isUnsigned)) {
			type = &builtInTypes[type->kind - tiSmmUInt8 + tiSmmInt8];
		}
	} else {
		// Otherwise floats are always considered bigger then int
		type = (leftType->kind > rightType->kind) ? leftType : rightType;
		if (type->kind == tiSmmBool) type = &builtInTypes[tiSmmUInt8];
	}
	return type;
}

static void fixDivModOperandTypes(PSmmAstNode expr, PIbsAllocator a) {
	PSmmAstNode* goodField = NULL;
	PSmmAstNode* badField = NULL;
	if (expr->left->type->isInt) {
		goodField = &expr->left;
		badField = &expr->right;
	} else if (expr->right->type->isInt) {
		goodField = &expr->right;
		badField = &expr->left;
	}

	if (!goodField) {
		// Neither operand is int so we cast both to int32
		PSmmAstNode cast = smmNewAstNode(nkSmmCast, a);
		cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, expr->left->token->filePos, a);
		cast->left = expr->left;
		expr->left = cast;
		cast = smmNewAstNode(nkSmmCast, a);
		cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, expr->right->token->filePos, a);
		cast->left = expr->right;
		expr->right = cast;
	} else if ((*badField)->kind == nkSmmFloat) {
		// if bad node is literal we need to convert it
		PSmmAstNode bad = *badField;
		PSmmAstNode good = *goodField;
		bad->kind = nkSmmInt;
		bad->token->kind = tkSmmInt;
		if (bad->token->floatVal >= 0 && good->type->isUnsigned) {
			bad->token->uintVal = (uint64_t)bad->token->floatVal;
			bad->type = good->type;
		} else {
			bad->token->sintVal = (int64_t)bad->token->floatVal;
			if (good->type->sizeInBytes > 4) {
				bad->type = &builtInTypes[tiSmmInt64];
			} else {
				bad->type = &builtInTypes[tiSmmInt32];
			}
		}
	} else {
		// Otherwise we need to cast it
		PSmmAstNode cast = smmNewAstNode(nkSmmCast, a);
		cast->type = (*goodField)->type;
		if (cast->type->sizeInBytes < 4) cast->type = &builtInTypes[tiSmmInt32];
		cast->token = newToken(tkSmmIdent, cast->type->name, (*badField)->token->filePos, a);
		cast->left = *badField;
		*badField = cast;
	}
}

static PSmmTypeInfo deduceTypeFrom(PSmmAstNode val) {
	// If right value is just another variable or func call just copy its type
	// but if it is expression then try to be a bit smarter.
	if (val->kind == nkSmmIdent || val->kind == nkSmmParam || val->kind == nkSmmCall || !val->type) {
		return val->type;
	} else {
		switch (val->type->kind) {
		case tiSmmSoftFloat64: return &builtInTypes[tiSmmFloat32];
		case tiSmmInt8: case tiSmmInt16: return &builtInTypes[tiSmmInt32];
		case tiSmmUInt8: case tiSmmUInt16: return &builtInTypes[tiSmmUInt32];
		default: return val->type;
		}
	}
}

static char* getMangledName(PSmmAstFuncDefNode func, PIbsAllocator a) {
	char* buf = ibsStartAlloc(a);
	char* curbuf = buf;

	// Copy func name
	size_t len = strlen(func->token->repr);
	memcpy(curbuf, func->token->repr, len);
	curbuf += len;

	// Copy param type names delimited with '_'
	PSmmAstParamNode param = func->params;
	while (param) {
		*curbuf = '_';
		curbuf++;
		len = strlen(param->type->name);
		memcpy(curbuf, param->type->name, len);
		curbuf += len;
		param = param->next;
	}
	ibsEndAlloc(a, curbuf - buf + 1);
	return buf;
}

static void processDeclarationWithExpr(PSmmAstDeclNode decl, PTIData tidata, PIbsAllocator a) {
	PSmmAstNode assignment = decl->left;
	PSmmAstIdentNode ident = &assignment->left->asIdent;
	if (decl->isBeingProcessed) {
		smmPostMessage(tidata->msgs, errSmmCircularDefinition, ident->token->filePos, ident->token->repr);
		ident->type = &builtInTypes[tiSmmUnknown];
		assignment->type = ident->type;
		return;
	}
	if (decl->isProcessed) {
		return;
	}
	if (!assignment->right) {
		ident->type = &builtInTypes[tiSmmUnknown];
		assignment->type = ident->type;
		return;
	}
	decl->isBeingProcessed = true;
	PSmmAstNode typeNode = assignment->right;
	processExpression(typeNode, tidata, a);
	if (!ident->type) {
		ident->type = deduceTypeFrom(typeNode);
		assignment->type = ident->type;
	}
	decl->isBeingProcessed = false;
	decl->isProcessed = true;
}

static bool addDeclIfNew(PSmmAstDeclNode decl, PTIData tidata) {
	if (decl->left->kind != nkSmmFunc) {
		PSmmAstIdentNode newIdent = &decl->left->left->asIdent;
		PSmmAstNode existing = ibsDictGet(tidata->idents, newIdent->token->repr);
		if (existing) {
			uintptr_t exLevel = 0;
			if (existing->left->left->isIdent) {
				exLevel = existing->left->left->asIdent.level;
			} else {
				assert(false && "Got unexpected node kind");
			}
			if (newIdent->level == exLevel) {
				smmPostMessage(tidata->msgs, errSmmRedefinition, newIdent->token->filePos, newIdent->token->repr);
				return false;
			}
		}
		ibsDictPush(tidata->idents, newIdent->token->repr, decl);
		return true;
	}

	PSmmAstFuncDefNode newfunc = (PSmmAstFuncDefNode)decl->left;
	PSmmAstNode existingDecl = ibsDictGet(tidata->idents, newfunc->token->repr);
	if (!existingDecl) {
		ibsDictPush(tidata->idents, newfunc->token->repr, decl);
		return true;
	}

	PSmmAstFuncDefNode existingFunc = (PSmmAstFuncDefNode)existingDecl->left;

	if (existingFunc->kind != nkSmmFunc) {
		smmPostMessage(tidata->msgs, errSmmRedefinition, newfunc->token->filePos, newfunc->token->repr);
		return false;
	}

	if (findFuncWithMatchingParams((PSmmAstNode)newfunc->params, existingFunc, false)) {
		smmPostMessage(tidata->msgs, errSmmFuncRedefinition, newfunc->token->filePos);
		return false;
	}

	PSmmAstFuncDefNode* nextOverloadField = &existingFunc->nextOverload;
	while (*nextOverloadField) {
		nextOverloadField = &(*nextOverloadField)->nextOverload;
	}
	*nextOverloadField = newfunc;
	return true;
}

static PSmmTypeInfo processExpression(PSmmAstNode expr, PTIData tidata, PIbsAllocator a) {
	PSmmTypeInfo resType = NULL;

	PSmmTypeInfo leftType = NULL;
	PSmmTypeInfo rightType = NULL;

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp: case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		if (expr->type && expr->type->kind != tiSmmBool) {
			return expr->type;
		}
		leftType = processExpression(expr->left, tidata, a);
		rightType = processExpression(expr->right, tidata, a);
		expr->isConst = expr->left->isConst && expr->right->isConst;
		resType = getCommonTypeFromOperands(leftType, rightType);
		if (!expr->type) expr->type = resType;
		break;
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		leftType = processExpression(expr->left, tidata, a);
		expr->isConst = expr->left->isConst;
		break;
	default: break;
	}

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmSub:
		if (resType->kind >= tiSmmFloat32) expr->kind++; // Add to FAdd, Sub to FSub
		break;
	case nkSmmMul:
		if (resType->kind >= tiSmmFloat32) expr->kind = nkSmmFMul;
		break;
	case nkSmmSDiv: case nkSmmSRem:
		if (resType->isUnsigned) expr->kind--; // Signed op to unsigned op
		if (resType->kind >= tiSmmFloat32) {
			char buf[4];
			smmPostMessage(tidata->msgs, errSmmBadOperandsType, expr->token->filePos, smmTokenToString(expr->token, buf), resType->name);
			fixDivModOperandTypes(expr, a);
			expr->type = getCommonTypeFromOperands(expr->left->type, expr->right->type);
		}
		break;
	case nkSmmFDiv: case nkSmmFRem:
		if (resType->kind < tiSmmFloat32) expr->type = &builtInTypes[tiSmmSoftFloat64];
		break;
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			if (!leftType->isInt || !rightType->isInt) break;
			if (leftType->isUnsigned == rightType->isUnsigned) break;

			smmPostMessage(tidata->msgs, wrnSmmComparingSignedAndUnsigned, expr->token->filePos);

			PSmmAstNode castNode = smmNewAstNode(nkSmmCast, a);
			castNode->isConst = expr->isConst;
			castNode->type = resType;
			if (leftType->isUnsigned) {
				castNode->left = expr->left;
				castNode->token = expr->left->token;
				expr->left = castNode;
			} else {
				castNode->left = expr->right;
				castNode->token = expr->right->token;
				expr->right = castNode;
			}
			break;
		}
	case nkSmmNeg:
		expr->type = leftType;
		if (expr->type->isUnsigned) {
			expr->type = &builtInTypes[expr->type->kind - tiSmmUInt8 + tiSmmInt8];
		} else if (expr->type->kind == tiSmmBool) {
			expr->type = &builtInTypes[tiSmmInt32]; // Next pass should handle this
		}
		break;
	case nkSmmCall:
		{
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			PSmmAstNode funcDefDecl = ibsDictGet(tidata->idents, callNode->token->repr);
			if (!funcDefDecl) {
				smmPostMessage(tidata->msgs, errSmmUndefinedIdentifier, callNode->token->filePos, callNode->token->repr);
				expr->type = &builtInTypes[tiSmmUnknown];
			} else if (funcDefDecl->kind == nkSmmParam || funcDefDecl->left->kind != nkSmmFunc) {
				smmPostMessage(tidata->msgs, errSmmNotAFunction, callNode->token->filePos, callNode->token->repr);
				expr->type = &builtInTypes[tiSmmUnknown];
			} else {
				PSmmAstNode astArg = callNode->args;
				while (astArg) {
					processExpression(astArg, tidata, a);
					astArg = astArg->next;
				}
				resolveCall(callNode, (PSmmAstFuncDefNode)funcDefDecl->left, tidata->msgs);
				if (tidata->acceptOnlyConsts) {
					smmPostMessage(tidata->msgs, errSmmNonConstInConstExpression, expr->token->filePos);
				}
			}
			break;
		}
	case nkSmmIdent:
		{
			PSmmAstDeclNode decl = ibsDictGet(tidata->idents, expr->token->repr);
			if (!decl) {
				smmPostMessage(tidata->msgs, errSmmUndefinedIdentifier, expr->token->filePos, expr->token->repr);
				expr->type = &builtInTypes[tiSmmUnknown];
			} else if (decl->kind == nkSmmParam) {
				if (tidata->acceptOnlyConsts) {
					smmPostMessage(tidata->msgs, errSmmNonConstInConstExpression, expr->token->filePos);
				}
				expr->type = ((PSmmAstParamNode)decl)->type;
			} else {
				if (decl->left->left->kind == nkSmmConst) {
					expr->kind = nkSmmConst;
					expr->isConst = true;
					processDeclarationWithExpr(decl, tidata, a);
				} else if (tidata->acceptOnlyConsts) {
					smmPostMessage(tidata->msgs, errSmmNonConstInConstExpression, expr->token->filePos);
				} else if (!decl->left->type) {
					assert(false && "This should not happen any more, I think!");
					processDeclarationWithExpr(decl, tidata, a);
				}
				if (!expr->type) {
					expr->type = decl->left->type;
					if (!expr->type) {
						expr->type = &builtInTypes[tiSmmUnknown];
					}
				}
			}
			break;
		}
	case nkSmmConst:
		if (!expr->type) {
			PSmmAstDeclNode decl = ibsDictGet(tidata->idents, expr->token->repr);
			if (!decl) {
				smmPostMessage(tidata->msgs, errSmmUndefinedIdentifier, expr->token->filePos, expr->token->repr);
				expr->type = &builtInTypes[tiSmmUnknown];
			} else {
				if (!decl->left->type) {
					processDeclarationWithExpr(decl, tidata, a);
				}
				expr->type = decl->left->type;
			}
		}
		break;
	case nkSmmAndOp: case nkSmmOrOp: case nkSmmXorOp:
	case nkSmmNot: case nkSmmCast: case nkSmmParam:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		// Nothing else left to do
		break;
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
	return expr->type;
}

static void processLocalSymbols(PSmmAstDeclNode decl, PTIData tidata, PIbsAllocator a) {
	PSmmAstDeclNode origDecl = decl;
	while (decl) {
		if (decl->left->left->isConst) {
			addDeclIfNew(decl, tidata);
		}
		decl = decl->nextDecl;
	}
	// We can't join these two loops since earlier decl can use const from the later one
	tidata->acceptOnlyConsts = true;
	decl = origDecl;
	while (decl) {
		if (decl->left->left->isConst) {
			processDeclarationWithExpr(decl, tidata, a);
		}
		decl = decl->nextDecl;
	}
	tidata->acceptOnlyConsts = false;
}

// Returns false if this statement should be removed
static bool processAssignment(PSmmAstNode stmt, PTIData tidata, PIbsAllocator a) {
	PSmmAstDeclNode decl = ibsDictGet(tidata->idents, stmt->left->token->repr);
	if (!decl) {
		smmPostMessage(tidata->msgs, errSmmUndefinedIdentifier, stmt->left->token->filePos, stmt->left->token->repr);
		return false;
	}
	if (decl->left != stmt) {
		PSmmToken origToken = stmt->left->token;
		*stmt->left = *decl->left->left;
		stmt->left->token = origToken;
	} else if (decl->isProcessed) {
		return true;
	}
	if (stmt->left->kind == nkSmmConst) {
		smmPostMessage(tidata->msgs, errSmmCantAssignToConst, stmt->token->filePos);
	}
	if (!stmt->right) return false;
	stmt->type = stmt->left->type;
	processExpression(stmt->right, tidata, a);

	return true;
}

static void processReturn(PSmmAstNode stmt, PTIData tidata, PIbsAllocator a) {
	assert(stmt && stmt->type);
	PSmmTypeInfo retType = stmt->type;
	if (stmt->left) {
		PSmmTypeInfo exprType = processExpression(stmt->left, tidata, a);
		if (exprType == &builtInTypes[tiSmmVoid]) {
			// This can happen if we use return funcThatReturnsNothing();
			smmPostMessage(tidata->msgs, errSmmInvalidExprUsed, stmt->left->token->filePos);
		} else if (retType == &builtInTypes[tiSmmVoid]) {
			smmPostMessage(tidata->msgs, errSmmNoReturnValueNeeded, stmt->token->filePos);
		} else if (retType->kind == tiSmmUnknown) {
			stmt->type = deduceTypeFrom(stmt->left);
		} else if (exprType->kind != tiSmmUnknown && exprType != retType && !isUpcastPossible(exprType, retType)) {
			PSmmTypeInfo ltype = exprType;
			if (ltype == &builtInTypes[tiSmmSoftFloat64]) ltype = &builtInTypes[tiSmmFloat32];
			smmPostMessage(tidata->msgs, errSmmBadReturnStmtType, stmt->token->filePos, ltype->name, retType->name);
		}
		return;
	}

	if (retType->kind != tiSmmVoid && retType->kind != tiSmmUnknown) {
		smmPostMessage(tidata->msgs, errSmmFuncMustReturnValue, stmt->token->filePos);
	}
}

static bool processStatement(PSmmAstNode stmt, PTIData tidata, PIbsAllocator a) {
	switch (stmt->kind) {
	case nkSmmBlock:
		{
			PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
			processLocalSymbols(newBlock->scope->decls, tidata, a);
			processBlock(newBlock, tidata, a);
			break;
		}
	case nkSmmAssignment: return processAssignment(stmt, tidata, a);
	case nkSmmReturn: processReturn(stmt, tidata, a); break;
	case nkSmmIf: case nkSmmWhile:
		processExpression(stmt->asIfWhile.cond, tidata, a);
		processStatement(stmt->asIfWhile.body, tidata, a);
		if (stmt->asIfWhile.elseBody) {
			processStatement(stmt->asIfWhile.elseBody, tidata, a);
		}
		break;
	case nkSmmDecl:
		{
			PSmmAstNode assignment = stmt->left;
			PSmmAstIdentNode ident = &assignment->left->asIdent;
			if (!stmt->asDecl.isProcessed) {
				processExpression(assignment->right, tidata, a);
				stmt->asDecl.isProcessed = true;
			} else {
				assert(false && "This should not happen");
			}
			if (!ident->type) {
				ident->type = deduceTypeFrom(assignment->right);
				assignment->type = ident->type;
			} else {
				// Type was explicitly given in source code
			}
			ibsDictPush(tidata->idents, ident->token->repr, stmt);
			break;
		}
	default:
		processExpression(stmt, tidata, a); break;
	}

	return true;
}

static void processBlock(PSmmAstBlockNode block, PTIData tidata, PIbsAllocator a) {
	PSmmAstNode* stmtField = &block->stmts;
	while (*stmtField) {
		PSmmAstNode stmt = *stmtField;
		if (!processStatement(stmt, tidata, a)) {
			// This means the statement should be discarded
			*stmtField = stmt->next;
		}
		stmtField = &stmt->next;
	}

	if (block->scope->level > 0) {
		PSmmAstDeclNode decl = block->scope->decls;
		while (decl) {
			ibsDictPop(tidata->idents, decl->left->left->token->repr);
			decl = decl->nextDecl;
		}
	}
}

static PSmmAstDeclNode processGlobalSymbols(PSmmAstDeclNode decl, PTIData tidata, PIbsAllocator a) {
	PSmmAstDeclNode funcDecl = NULL;
	PSmmAstDeclNode* funcDeclField = &funcDecl;
	PSmmAstDeclNode varDecl = NULL;
	PSmmAstDeclNode* varDeclField = &varDecl;
	// Sort decls so vars and constants are before functions
	while (decl) {
		if (addDeclIfNew(decl, tidata)) {
			if (decl->left->kind == nkSmmFunc) {
				*funcDeclField = decl;
				funcDeclField = &decl->nextDecl;
				PSmmAstFuncDefNode funcNode = &decl->left->asFunc;
				if (funcNode->body) {
					funcNode->token->stringVal = getMangledName(funcNode, a);
				}
				else {
					// If function has no body we assume it is external C func and we don't mangle the name
					funcNode->token->stringVal = (char*)funcNode->token->repr;
				}
			} else {
				*varDeclField = decl;
				varDeclField = &decl->nextDecl;
			}
		}
		decl = decl->nextDecl;
	}
	*varDeclField = funcDecl; // Chain funcDecl at the end of var and const decls
	tidata->funcDecls = funcDecl;
	*funcDeclField = NULL;

	decl = varDecl;
	tidata->acceptOnlyConsts = true;
	while (decl && decl->left->kind != nkSmmFunc) {
		if (decl->left->left->kind == nkSmmConst) {
			processDeclarationWithExpr(decl, tidata, a);
		}
		decl = decl->nextDecl;
	}
	tidata->acceptOnlyConsts = false;

	decl = varDecl;
	while (decl && decl->left->kind != nkSmmFunc) {
		// We remove vars here and add them back when we actually come to decl statement
		// so we can detect if var is used before it is declared
		if (decl->left->left->kind != nkSmmConst) {
			ibsDictPop(tidata->idents, decl->left->left->token->repr);
		}
		decl = decl->nextDecl;
	}

	return varDecl;
}

void processFuncDecls(PTIData tidata, PIbsAllocator a) {
	PSmmAstDeclNode decl = tidata->funcDecls;
	tidata->isInMainCode = false;
	while (decl) {
		PSmmAstFuncDefNode funcNode = &decl->left->asFunc;
		if (funcNode->body) {
			PSmmAstParamNode param = funcNode->params;
			while (param) {
				ibsDictPush(tidata->idents, param->token->repr, param);
				param = param->next;
			}
			processLocalSymbols(funcNode->body->scope->decls, tidata, a);
			processBlock(funcNode->body, tidata, a);
			param = funcNode->params;
			while (param) {
				ibsDictPop(tidata->idents, param->token->repr);
				param = param->next;
			}
		}
		decl = decl->nextDecl;
	}
	tidata->isInMainCode = true;
}

void smmExecuteTypeInferencePass(PSmmAstNode module, PSmmMsgs msgs, PIbsAllocator a) {
	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	assert(globalBlock->kind == nkSmmBlock);

	PIbsAllocator tmpa = ibsSimpleAllocatorCreate("TypeInferenceTmp", a->size);
	PIbsDict idents = ibsDictCreate(tmpa);
	struct TIData tidata = { idents, msgs, NULL, true };

	globalBlock->scope->decls = processGlobalSymbols(globalBlock->scope->decls, &tidata, a);

	processBlock(globalBlock, &tidata, a);

	processFuncDecls(&tidata, a);

	ibsSimpleAllocatorFree(tmpa);
}
