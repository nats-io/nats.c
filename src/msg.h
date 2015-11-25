// Copyright 2015 Apcera Inc. All rights reserved.

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
