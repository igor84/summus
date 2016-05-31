#ifndef SMM_PARSER_H
#define SMM_PARSER_H

#include "smmlexer.h"

struct SmmParser {
	PSmmLexer lex;
	PSmmToken prevToken;
	PSmmToken curToken;
	PSmmDict idents;
	int lastErrorLine;
	PSmmAllocator allocator;
};
typedef struct SmmParser* PSmmParser;

// Each enum value should have coresponding string in smmparser.c
typedef enum {
	nkSmmError, nkSmmProgram, nkSmmAssignment, nkSmmIdent,
	nkSmmAdd = 8, nkSmmFAdd,
	nkSmmSub, nkSmmFSub,
	nkSmmMul, nkSmmFMul,
	nkSmmUDiv, nkSmmSDiv, nkSmmFDiv,
	nkSmmURem, nkSmmSRem, nkSmmFRem,
	nkSmmNeg,
	nkSmmType, nkSmmUInt, nkSmmFloat
} SmmAstNodeKind;

typedef enum {
	tiSmmUnknown,
	tiSmmInt8, tiSmmInt16, tiSmmInt32, tiSmmInt64,
	tiSmmUInt8, tiSmmUInt16, tiSmmUInt32, tiSmmUInt64,
	tiSmmFloat32, tiSmmFloat64, tiSmmBool
} SmmTypInfoKind;

enum {
	tifSmmUnknown = 0,
	tifSmmInt = 1,
	tifSmmUnsigned = 2,
	tifSmmUnsignedInt = 3,
	tifSmmFloat = 4
};

struct SmmTypeInfo {
	int kind;
	char* name;
	int sizeInBytes;
	uint32_t flags;
};
typedef struct SmmTypeInfo* PSmmTypeInfo;

typedef struct SmmAstNode* PSmmAstNode;
struct SmmAstNode {
	SmmAstNodeKind kind;
	PSmmTypeInfo type;
	PSmmToken token;
	PSmmAstNode next;
	PSmmAstNode left;
	PSmmAstNode right;
};

PSmmParser smmCreateParser(PSmmLexer lex, PSmmAllocator allocator);
void smmParse(PSmmParser parser);

#endif