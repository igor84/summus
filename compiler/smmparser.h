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

typedef struct SmmAstNode* PSmmAstNode;
typedef PSmmAstNode (*PSmmSetupBinOpNode)(PSmmAstNode binOp);

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
	int lastErrorLine;
	PSmmAllocator allocator;
	PSmmBinaryOperator* operatorPrecedences;
};
typedef struct SmmParser* PSmmParser;

// Each enum value should have coresponding string in smmparser.c
typedef enum {
	nkSmmError, nkSmmProgram, nkSmmDecl, nkSmmAssignment, nkSmmIdent,
	nkSmmAdd , nkSmmFAdd,
	nkSmmSub, nkSmmFSub,
	nkSmmMul, nkSmmFMul,
	nkSmmUDiv, nkSmmSDiv, nkSmmFDiv,
	nkSmmURem, nkSmmSRem, nkSmmFRem,
	nkSmmNeg,
	nkSmmType, nkSmmInt, nkSmmFloat,
	nkSmmCast,

	nkSmmTerminator
} SmmAstNodeKind;

const char* nodeKindToString[nkSmmTerminator];

/**
 * Each build in type info kind should have coresponding type info defined in smmparser.c.
 * SoftFloat64 type is for literals that depending on the context could be interpreted as
 * Float32 or Float64 and will be converted to those types eventually.
 */
typedef enum {
	tiSmmUnknown,
	tiSmmUInt8, tiSmmUInt16, tiSmmUInt32, tiSmmUInt64,
	tiSmmInt8, tiSmmInt16, tiSmmInt32, tiSmmInt64,
	tiSmmFloat32, tiSmmFloat64, tiSmmSoftFloat64, tiSmmBool
} SmmTypInfoKind;

enum SmmTypeInfoFlags {
	tifSmmUnknown = 0,
	tifSmmInt = 1,
	tifSmmUnsigned = 2,
	tifSmmUnsignedInt = 3,
	tifSmmFloat = 4,
	tifSmmBool = 8
};

struct SmmTypeInfo {
	int kind;
	const char* name;
	int sizeInBytes;
	uint32_t flags;
};
typedef struct SmmTypeInfo* PSmmTypeInfo;

struct SmmAstNode {
	SmmAstNodeKind kind;
	PSmmTypeInfo type;
	PSmmToken token;
	PSmmAstNode next;
	PSmmAstNode left;
	PSmmAstNode right;
};

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator);
PSmmAstNode smmParse(PSmmParser parser);

#endif
