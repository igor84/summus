#pragma once

#include "ibscommon.h"
#include "ibsallocator.h"
#include "smmparser.h"

void smmExecuteSemPass(PSmmAstNode module, PSmmMsgs msgs, PIbsAllocator a);
