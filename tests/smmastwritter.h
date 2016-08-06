#ifndef SMM_AST_WRITTER_H
#define SMM_AST_WRITTER_H

#include "../compiler/smmcommon.h"
#include "../compiler/smmutil.h"
#include "../compiler/smmparser.h"
#include <stdio.h>

void smmOutputAst(PSmmAstNode module, FILE* f, PSmmAllocator a);

#endif
