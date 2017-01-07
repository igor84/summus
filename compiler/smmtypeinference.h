#pragma once

#include "ibscommon.h"
#include "ibsallocator.h"
#include "smmmsgs.h"
#include "smmparser.h"

void smmExecuteTypeInferencePass(PSmmAstNode module, PSmmMsgs msgs, PIbsAllocator a);
