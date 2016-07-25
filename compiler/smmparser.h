#ifndef SMM_PARSER_H
#define SMM_PARSER_H

/**
 * Parser goes through tokens returned by lexer and tries to construct
 * Abstract Syntax Tree (AST) which is a tree structure of SmmAstNodes.
 * For example the expression a = b * (c - d / e); should result in a tree like this one:
 *    ___=_____
 *   a       __*______
 *          b       __-______
 *                 c       __/__
 *                        d     e
 * Every node is of a certain kind and needs to have certain type set as in
 * int16 or float32. In recursive descent parser the tree is sort of built from
 * the bottom so we set the "least" possible type on each node. After the parser
 * another, top-down pass through the tree is needed so that the maximum needed
 * type for the entire expression is lowered through the tree thus making sure
 * that operations are done in wanted precisions. For float types, in first pass
 * the node type is set to SoftFloat meaning that second pass should set this to
 * either float32 or float64 depending on parent nodes and the precision they
 * require. If inferred type of variable being assigned turns out to be SoftFloat
 * it is assumed to be float64.
 */

#include "smmcommon.h"
#include "smmlexer.h"

typedef struct SmmParser* PSmmParser;
typedef struct SmmAstNode* PSmmAstNode;

typedef struct SmmAstIdentNode* PSmmAstIdentNode;
typedef struct SmmAstScopeNode* PSmmAstScopeNode;
typedef struct SmmAstBlockNode* PSmmAstBlockNode;
typedef struct SmmAstParamNode* PSmmAstParamNode;
typedef struct SmmAstFuncDefNode* PSmmAstFuncDefNode;
typedef struct SmmAstCallNode* PSmmAstCallNode;

typedef PSmmAstNode (*PSmmSetupBinOpNode)(PSmmParser parser, PSmmAstNode binOp);

struct SmmBinaryOperator {
	PSmmSetupBinOpNode setupNode;
	int precedence;
};
typedef struct SmmBinaryOperator* PSmmBinaryOperator;

struct SmmParser {
	PSmmLexer lex;
	PSmmToken prevToken;
	PSmmToken curToken;
	PSmmDict idents;
	PSmmAstScopeNode curScope;
	PSmmAllocator allocator;
	PSmmBinaryOperator* operatorPrecedences;
};

// Each enum value should have coresponding string in smmparser.c
typedef enum {
	nkSmmError, nkSmmProgram, nkSmmFunc,
	nkSmmBlock, nkSmmScope,
	nkSmmDecl, nkSmmIdent, nkSmmConst,
	nkSmmAssignment,
	nkSmmAdd , nkSmmFAdd,
	nkSmmSub, nkSmmFSub,
	nkSmmMul, nkSmmFMul,
	nkSmmUDiv, nkSmmSDiv, nkSmmFDiv,
	nkSmmURem, nkSmmSRem, nkSmmFRem,
	nkSmmNeg,
	nkSmmType, nkSmmInt, nkSmmFloat, nkSmmBool,
	nkSmmCast, nkSmmParam, nkSmmCall, nkSmmReturn,
	nkSmmAndOp, nkSmmXorOp, nkSmmOrOp,
	nkSmmEq, nkSmmNotEq, nkSmmGt, nkSmmGtEq, nkSmmLt, nkSmmLtEq, nkSmmNot,

	nkSmmTerminator
} SmmAstNodeKind;

const char* nodeKindToString[nkSmmTerminator];

/**
 * Each built in type info kind should have coresponding type info defined in smmparser.c.
 * SoftFloat64 type is for literals that depending on the context could be interpreted as
 * Float32 or Float64 and will be converted to those types eventually.
 */
typedef enum {
	tiSmmUnknown, tiSmmBool,
	tiSmmUInt8, tiSmmUInt16, tiSmmUInt32, tiSmmUInt64,
	tiSmmInt8, tiSmmInt16, tiSmmInt32, tiSmmInt64,
	tiSmmFloat32, tiSmmFloat64, tiSmmSoftFloat64,
} SmmTypInfoKind;

enum SmmTypeInfoFlags {
	tifSmmUnknown = 0x0,
	tifSmmInt = 0x1,
	tifSmmUnsigned = 0x2,
	tifSmmUnsignedInt = 0x3,
	tifSmmFloat = 0x4,
	tifSmmBool = 0x8,
};

enum SmmNodeFlags {
	nfSmmIdent = 0x1,
	nfSmmConst = 0x2,
	nfSmmBinOp = 0x4,
};

struct SmmTypeInfo {
	SmmTypInfoKind kind;
	uint32_t sizeInBytes;
	const char* name;
	uint32_t flags;
};
typedef struct SmmTypeInfo* PSmmTypeInfo;

/**
 * SmmAstNode is base "class" of AstNodes so all following types of AstNodes
 * must begin with identical structure which is:
 * 1. SmmAstNodeKind kind;
 * 2. 32bit value
 * 3. pointer value that should point to token
 * 4. 4 more pointer values where first one should point to type
 * 5. Arbitraty additional fields may be added
 * That way we can freely cast between these types although in a few situations
 * we need to be aware what field in "derived" type matches what field in AstNode.
 */
struct SmmAstNode {
	SmmAstNodeKind kind;
	uint32_t flags; // Node flags, like does it represent a const expression
	PSmmToken token;
	PSmmTypeInfo type;
	PSmmAstNode next;
	PSmmAstNode left;
	PSmmAstNode right;
};

struct SmmAstIdentNode {
	SmmAstNodeKind kind;
	uint32_t flags;
	PSmmToken token;
	PSmmTypeInfo type;
	PSmmAstNode next;
	PSmmAstNode left;
	uintptr_t level; // Scope level ident is created in (must be int which is the same size as pointer)
};

struct SmmAstScopeNode {
	SmmAstNodeKind kind;
	uint32_t level; // Scope nesting level
	PSmmToken zzNotUsed1;
	PSmmTypeInfo returnType; // Return type of the function this scope is part of
	PSmmAstNode decls;
	PSmmAstScopeNode prevScope;
	PSmmAstNode lastDecl; // Last decl in scope node so we can add new ones at the end
};

struct SmmAstBlockNode {
	SmmAstNodeKind kind;
	uint32_t zzNotUsed1;
	PSmmToken token;
	PSmmTypeInfo zzNotUsed2;
	PSmmAstNode next;
	PSmmAstScopeNode scope;
	PSmmAstNode stmts;
};

struct SmmAstParamNode {
	SmmAstNodeKind kind;
	uint32_t flags;
	PSmmToken token;
	PSmmTypeInfo type;
	PSmmAstParamNode next;
	uintptr_t count; // Set on first param in FuncDef to tell us how many params there are in total
	uintptr_t level; // We need this so we can easily check if same param is defined twice
};

struct SmmAstFuncDefNode {
	SmmAstNodeKind kind;
	uint32_t flags;
	PSmmToken token;
	PSmmTypeInfo returnType;
	PSmmAstBlockNode body;
	PSmmAstParamNode params;
	PSmmAstFuncDefNode nextOverload;
};

struct SmmAstCallNode {
	SmmAstNodeKind kind;
	uint32_t flags;
	PSmmToken token;
	PSmmTypeInfo returnType;
	PSmmAstNode zzNotUsed1;
	PSmmAstParamNode params;
	PSmmAstNode args;
};

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator);
PSmmAstNode smmParse(PSmmParser parser);

#endif
