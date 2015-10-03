// Copyright 2015 Apcera Inc. All rights reserved.

#include <stdlib.h>
#include <string.h>

#include "natsp.h"
#include "mem.h"

void
natsMsg_Destroy(natsMsg *msg)
{
    if (msg == NULL)
        return;

    NATS_FREE(msg->subject);
    NATS_FREE(msg->reply);
    NATS_FREE(msg->data);
    NATS_FREE(msg);
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
natsMsg_create(natsMsg **newMsg, char *buf, int bufLen)
{
    natsMsg     *msg     = NULL;

    msg = (natsMsg*) NATS_CALLOC(1, sizeof(natsMsg));
    if (msg == NULL)
        return NATS_NO_MEMORY;

    if (bufLen > 0)
    {
        msg->data = NATS_MALLOC(bufLen);
        if (msg->data == NULL)
        {
            NATS_FREE(msg);
            return NATS_NO_MEMORY;
        }

        memcpy(msg->data, buf, bufLen);
        msg->dataLen = bufLen;
    }

    *newMsg = msg;

    return NATS_OK;
}

natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen)
{
    natsStatus  s       = NATS_OK;
    natsMsg     *msg    = NULL;

    if ((newMsg == NULL) || (subj == NULL) || (strlen(subj) == 0))
        return NATS_INVALID_ARG;

    s = natsMsg_create(&msg, (char*) data, dataLen);
    if (s == NATS_OK)
    {
        msg->subject = NATS_STRDUP(subj);
        if (msg->subject == NULL)
            s = NATS_NO_MEMORY;
    }
    if ((s == NATS_OK) && (reply != NULL) && (strlen(reply) > 0))
    {
        msg->reply = NATS_STRDUP(reply);
        if (msg->reply == NULL)
            s = NATS_NO_MEMORY;
    }

    if (s == NATS_OK)
        *newMsg = msg;
    else
        natsMsg_Destroy(msg);

    return s;
}

