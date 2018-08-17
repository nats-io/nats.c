// Copyright 2018 The NATS Authors
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

#include "msg.h"

uint64_t
stanMsg_GetSequence(stanMsg *msg)
{
    if (msg == NULL)
        return 0;

    return msg->seq;
}

const char*
stanMsg_GetData(stanMsg *msg)
{
    if (msg == NULL)
        return NULL;

    return (const char*) msg->data;
}

int
stanMsg_GetDataLength(stanMsg *msg)
{
    if (msg == NULL)
        return 0;

    return msg->dataLen;
}

int64_t
stanMsg_GetTimestamp(stanMsg *msg)
{
    if (msg == NULL)
        return 0;

    return msg->timestamp;
}

bool
stanMsg_IsRedelivered(stanMsg *msg)
{
    if (msg == NULL)
        return false;

    return msg->redelivered;
}

static void
stanMsg_free(void *object)
{
    stanMsg *msg;

    if (object == NULL)
        return;

    msg = (stanMsg*) object;

    NATS_FREE(msg);
}

void
stanMsg_Destroy(stanMsg *msg)
{
    if (msg == NULL)
        return;

    if (natsGC_collect((natsGCItem *) msg))
        return;

    stanMsg_free((void*) msg);
}

natsStatus
stanMsg_create(stanMsg **newMsg, Pb__MsgProto *pb)
{
    stanMsg *msg        = NULL;
    char    *ptr        = NULL;
    int     payloadSize = 0;

    payloadSize = (int) pb->data.len;
    msg = NATS_MALLOC(sizeof(stanMsg) + payloadSize + 1);
    if (msg == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // To be safe, we could 'memset' the message up to sizeof(stanMsg),
    // but since we are explicitly initializing most of the fields, we save
    // on that call, but we need to make sure what we initialize all fields!!!

    // That being said, we memset the 'gc' structure to protect us in case
    // some fields are later added to this 'external' structure and we forget
    // about updating this initialization code.
    memset(&(msg->gc), 0, sizeof(natsGCItem));

    msg->seq         = pb->sequence;
    msg->timestamp   = pb->timestamp;
    msg->redelivered = pb->redelivered;
    msg->next        = NULL;

    ptr = (char*) (((char*) &(msg->next)) + sizeof(msg->next));
    msg->data    = (const char*) ptr;
    msg->dataLen = payloadSize;
    memcpy(ptr, (char*) pb->data.data, payloadSize);
    ptr += payloadSize;
    *(ptr) = '\0';

    // Setting the callback will trigger garbage collection when
    // stanMsg_Destroy() is invoked.
    msg->gc.freeCb = stanMsg_free;

    *newMsg = msg;

    return NATS_OK;
}
