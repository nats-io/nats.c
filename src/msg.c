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
    int         bufSize   = 0;

    bufSize  = subjLen;
    bufSize += 1;
    bufSize += replyLen;
    bufSize += 1;
    bufSize += bufLen;
    bufSize += 1;

    msg = NATS_MALLOC(sizeof(natsMsg) + bufSize);
    if (msg == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // To be safe, we could 'memset' the message up to sizeof(natsMsg),
    // but since we are explicitly initializing most of the fields, we save
    // on that call, but we need to make sure what we initialize all fields!!!

    // That being said, we memset the 'gc' structure to protect us in case
    // some fields are later added to this 'external' structure and we forget
    // about updating this initialization code.
    memset(&(msg->gc), 0, sizeof(natsGCItem));

    msg->next = NULL;

    ptr = (char*) (((char*) &(msg->next)) + sizeof(msg->next));

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

    // Setting the callback will trigger garbage collection when
    // natsMsg_Destroy() is invoked.
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
        return nats_setDefaultError(NATS_INVALID_ARG);
    }

    s = natsMsg_create(newMsg,
                       subj, (int) strlen(subj),
                       reply, (reply == NULL ? 0 : (int) strlen(reply)),
                       data, dataLen);

    return NATS_UPDATE_ERR_STACK(s);
}

