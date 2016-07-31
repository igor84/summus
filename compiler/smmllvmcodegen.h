#ifndef SMM_LLVM_CODE_GEN_H
#define SMM_LLVM_CODE_GEN_H

#include "smmcommon.h"
#include "smmutil.h"
#include "smmparser.h"

void smmExecuteLLVMCodeGenPass(PSmmAstNode module, PSmmAllocator a);

#endif
