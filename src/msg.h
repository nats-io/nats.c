// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef MSG_H_
#define MSG_H_

#include "status.h"
#include "gc.h"

struct __natsMsg;

typedef struct __natsMsg
{
    natsGCItem          gc;

    // This holds subject, reply and payload.
    char                *buffer;

    // These points to specific locations in 'buffer'. They must not be freed.
    // (note that we could do without 'subject' since as of now, it is
    // equivalent to 'buffer').
    const char          *subject;
    const char          *reply;
    const char          *data;
    int                 dataLen;

    struct __natsMsg    *next;

} natsMsg;

// PRIVATE

natsStatus
natsMsg_create(natsMsg **newMsg,
               const char *subject, int subjLen,
               const char *reply, int replyLen,
               const char *buf, int bufLen);

// This needs to follow the nats_FreeObjectCb prototype (see gc.h)
void
natsMsg_free(void *object);


// PUBLIC

NATS_EXTERN natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen);

NATS_EXTERN const char*
natsMsg_GetReply(natsMsg *msg);

NATS_EXTERN const char*
natsMsg_GetData(natsMsg *msg);

NATS_EXTERN int
natsMsg_GetDataLength(natsMsg *msg);

NATS_EXTERN void
natsMsg_Destroy(natsMsg *msg);


#endif /* MSG_H_ */
