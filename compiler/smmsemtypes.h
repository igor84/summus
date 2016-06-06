#ifndef SMM_TYPE_FIXER_H
#define SMM_TYPE_FIXER_H

#include "smmcommon.h"
#include "smmparser.h"

struct SmmModuleData {
	PSmmAstNode module;
	char* filename;
	PSmmAllocator allocator;
};
typedef struct SmmModuleData* PSmmModuleData;

void smmAnalyzeTypes(PSmmModuleData data);

#endif