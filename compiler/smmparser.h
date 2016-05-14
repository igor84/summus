#pragma once

#ifndef SMM_PARSER_H
#define SMM_PARSER_H

#include "smmlexer.h"

struct SmmParser {
	PSmmLexer lex;
	PSmmToken curToken;
	PSmmDict idents;
	PSmmAllocator allocator;
};
typedef struct SmmParser* PSmmParser;

typedef enum {
	nkSmmError, nkSmmProgram, nkSmmDeclAssignment, nkSmmSymbol,
	nkSmmAdd = 8, nkSmmFAdd,
	nkSmmSub, nkSmmFSub,
	nkSmmMul, nkSmmFMul,
	nkSmmUDiv, nkSmmSDiv, nkSmmFDiv,
	nkSmmURem, nkSmmSRem, nkSmmFRem,

	nkSmmNeg,

	nkSmmInt, nkSmmFloat
} SmmAstNodeKind;

typedef struct SmmAstNode* PSmmAstNode;
struct SmmAstNode {
	SmmAstNodeKind kind;
	PSmmToken token;
	PSmmAstNode next;
	PSmmAstNode left;
	PSmmAstNode right;
};

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator);
void smmParse(PSmmParser parser);

#endif