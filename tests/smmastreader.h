#ifndef SMM_AST_READER_H
#define SMM_AST_READER_H

#include "../compiler/smmcommon.h"
#include "../compiler/smmutil.h"
#include "../compiler/smmparser.h"
#include <stdio.h>

PSmmAstNode smmLoadAst(PSmmLexer lex, PSmmAllocator a);

#endif
