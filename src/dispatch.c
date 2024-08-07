// Copyright 2015-2024 The NATS Authors
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

#include "natsp.h"

#include <string.h>
#include <stdio.h>

#include "mem.h"
#include "conn.h"
#include "sub.h"
#include "glib/glib.h"

// sub and dispatcher locks must be held.
void natsSub_enqueueMessage(natsSubscription *sub, natsMsg *msg)
{
    bool signal = false;
    natsDispatchQueue *q = &sub->dispatcher->queue;

    if (q->head == NULL)
    {
        signal = true;
        msg->next = NULL;
        q->head = msg;
    }
    else
    {
        q->tail->next = msg;
    }
    q->tail = msg;
    q->msgs++;
    q->bytes += natsMsg_dataAndHdrLen(msg);

    if (signal)
        natsCondition_Signal(sub->dispatcher->cond);
}

// sub and dispatcher locks must be held.
natsStatus natsSub_enqueueUserMessage(natsSubscription *sub, natsMsg *msg)
{
    natsDispatchQueue *toQ = &sub->dispatcher->queue;
    natsDispatchQueue *statsQ = &sub->ownDispatcher.queue;
    int newMsgs = statsQ->msgs + 1;
    int newBytes = statsQ->bytes + natsMsg_dataAndHdrLen(msg);

    msg->sub = sub;

    if (((sub->msgsLimit > 0) && (newMsgs > sub->msgsLimit)) ||
        ((sub->bytesLimit > 0) && (newBytes > sub->bytesLimit)))
    {
        return NATS_SLOW_CONSUMER;
    }
    sub->slowConsumer = false;

    if (newMsgs > sub->msgsMax)
        sub->msgsMax = newMsgs;
    if (newBytes > sub->bytesMax)
        sub->bytesMax = newBytes;

    if ((sub->jsi != NULL) && sub->jsi->ackNone)
        natsMsg_setAcked(msg);

    // Update the subscription stats if separate, the queue stats will be
    // updated below.
    if (toQ != statsQ)
    {
        statsQ->msgs++;
        statsQ->bytes += natsMsg_dataAndHdrLen(msg);
    }

    natsSub_enqueueMessage(sub, msg);
    return NATS_OK;
}

