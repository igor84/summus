#pragma once

#include "../compiler/ibscommon.h"
#include "../compiler/ibsallocator.h"
#include "../compiler/smmparser.h"

#include <stdio.h>

void smmExecuteGVPass(PSmmAstNode module, FILE* f);
