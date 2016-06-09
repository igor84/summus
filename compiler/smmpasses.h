#ifndef SMM_PASSES_H
#define SMM_PASSES_H

#include "smmcommon.h"
#include "smmparser.h"

// Structure used to pass data to all AST passes
struct SmmModuleData {
	PSmmAstNode module;
	char* filename;
	PSmmAllocator allocator;
};
typedef struct SmmModuleData* PSmmModuleData;

#endif
