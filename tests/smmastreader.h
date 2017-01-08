#ifndef SMM_AST_READER_H
#define SMM_AST_READER_H

#include "../compiler/ibscommon.h"
#include "../compiler/ibsallocator.h"
#include "../compiler/smmparser.h"
#include <stdio.h>

PSmmAstNode smmLoadAst(PSmmLexer lex, PIbsAllocator a);

#endif
