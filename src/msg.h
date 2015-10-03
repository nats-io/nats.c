// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef MSG_H_
#define MSG_H_

#include "status.h"

struct __natsMsg;

typedef struct __natsMsg
{
    char                *subject;
    char                *reply;
    char                *data;
    int                 dataLen;

    struct __natsMsg    *next;

} natsMsg;

// PRIVATE

natsStatus
natsMsg_create(natsMsg **newMsg, char *buf, int bufLen);


// PUBLIC

natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen);

const char*
natsMsg_GetReply(natsMsg *msg);

const char*
natsMsg_GetData(natsMsg *msg);

int
natsMsg_GetDataLength(natsMsg *msg);

void
natsMsg_Destroy(natsMsg *msg);


#endif /* MSG_H_ */
