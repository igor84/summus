#ifndef SMM_MSGS_MOCKUP_H
#define SMM_MSGS_MOCKUP_H

#include "../compiler/smmmsgs.h"

void (*onPostMessageCalled)(SmmMsgType msgType);

#endif