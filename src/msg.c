// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

// Do this after including natsp.h in order to have some of the
// GNU specific flag set first.
#include <string.h>

#include "mem.h"

void
natsMsg_free(void *object)
{
    natsMsg *msg;

    if (object == NULL)
        return;

    msg = (natsMsg*) object;

    NATS_FREE(msg->buffer);
    NATS_FREE(msg);
}

void
natsMsg_Destroy(natsMsg *msg)
{
    if (msg == NULL)
        return;

    if (natsGC_collect((natsGCItem *) msg))
        return;

    natsMsg_free((void*) msg);
}

const char*
natsMsg_GetSubject(natsMsg *msg)
{
    if (msg == NULL)
        return NULL;

    return (const char*) msg->subject;
}

const char*
natsMsg_GetReply(natsMsg *msg)
{
    if (msg == NULL)
        return NULL;

    return (const char*) msg->reply;
}

const char*
natsMsg_GetData(natsMsg *msg)
{
    if (msg == NULL)
        return NULL;

    return (const char*) msg->data;
}

int
natsMsg_GetDataLength(natsMsg *msg)
{
    if (msg == NULL)
        return 0;

    return msg->dataLen;
}

natsStatus
natsMsg_create(natsMsg **newMsg,
               const char *subject, int subjLen,
               const char *reply, int replyLen,
               const char *buf, int bufLen)
{
    natsMsg     *msg      = NULL;
    char        *ptr      = NULL;
    int         totalSize = 0;

    msg = NATS_CALLOC(1, sizeof(natsMsg));
    if (msg == NULL)
        return NATS_NO_MEMORY;

    totalSize  = subjLen;
    totalSize += 1;
    totalSize += replyLen;
    totalSize += 1;
    totalSize += bufLen;
    totalSize += 1;

    msg->buffer = NATS_MALLOC(totalSize);
    if (msg->buffer == NULL)
    {
        NATS_FREE(msg);
        return NATS_NO_MEMORY;
    }
    ptr = msg->buffer;

    msg->subject = (const char*) ptr;
    memcpy(ptr, subject, subjLen);
    ptr += subjLen;
    *(ptr++) = '\0';

    msg->reply = (const char*) ptr;
    if (replyLen > 0)
    {
        memcpy(ptr, reply, replyLen);
        ptr += replyLen;
    }
    *(ptr++) = '\0';

    msg->data    = (const char*) ptr;
    msg->dataLen = bufLen;
    memcpy(ptr, buf, bufLen);
    ptr += bufLen;
    *(ptr) = '\0';

    // Setting the callback will trigger garbage collection
    msg->gc.freeCb = natsMsg_free;

    *newMsg = msg;

    return NATS_OK;
}

natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen)
{
    natsStatus  s = NATS_OK;

    if ((subj == NULL)
        || (subj[0] == '\0')
        || ((reply != NULL) && (reply[0] == '\0')))
    {
        return NATS_INVALID_ARG;
    }

    s = natsMsg_create(newMsg,
                       subj, strlen(subj),
                       reply, (reply == NULL ? 0 : strlen(reply)),
                       data, dataLen);
    return s;
}

