#ifndef SMM_AST_WRITTER_H
#define SMM_AST_WRITTER_H

#include "../compiler/ibscommon.h"
#include "../compiler/ibsallocator.h"
#include "../compiler/smmparser.h"
#include <stdio.h>

void smmOutputAst(PSmmAstNode module, FILE* f, PIbsAllocator a);

#endif
