#ifndef SMM_LLVM_CODE_GEN_H
#define SMM_LLVM_CODE_GEN_H

#include "ibscommon.h"
#include "ibsallocator.h"
#include "smmparser.h"

#include <stdio.h>

bool smmExecuteLLVMCodeGenPass(PSmmAstNode module, FILE* out, PIbsAllocator a);

#endif
