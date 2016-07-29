#ifndef SMM_LLVM_MODULE_GEN_H
#define SMM_LLVM_MODULE_GEN_H

#include "smmcommon.h"
#include "smmutil.h"
#include "smmparser.h"

void smmGenLLVMModule(PSmmAstNode module, PSmmAllocator a);

#endif
