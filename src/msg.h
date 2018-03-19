// Copyright 2015-2018 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MSG_H_
#define MSG_H_

#include "status.h"
#include "gc.h"

struct __natsMsg;

struct __natsMsg
{
    natsGCItem          gc;

    // The message is allocated as a single memory block that contains
    // this structure and enough space for the payload. The msg payload
    // starts after the 'next' pointer.
    const char          *subject;
    const char          *reply;
    const char          *data;
    int                 dataLen;

    // subscription (needed when delivery done by connection)
    struct __natsSubscription *sub;

    // Must be last field!
    struct __natsMsg    *next;

    // Nothing after this: the message payload goes there.

};

natsStatus
natsMsg_create(natsMsg **newMsg,
               const char *subject, int subjLen,
               const char *reply, int replyLen,
               const char *buf, int bufLen);

// This needs to follow the nats_FreeObjectCb prototype (see gc.h)
void
natsMsg_free(void *object);

#endif /* MSG_H_ */
