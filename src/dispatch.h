// Copyright 2024 The NATS Authors
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

#ifndef DISPATCH_H_
#define DISPATCH_H_

typedef struct __natsDispatchQueue_s
{
    natsMsg *head;
    natsMsg *tail;
    int msgs;
    int bytes;
} natsDispatchQueue;

typedef struct __natsDispatcher_s
{
    // When created as a dedicated dispatcher for a subscription, we use the
    // sub's mutex (for performance? TODO: benchmack), so there is special
    // handling for mu in the code.
    natsSubscription *dedicatedTo;
    natsMutex *mu;

    natsThread *thread;
    natsCondition *cond;
    natsDispatchQueue queue;

    // Flags.
    bool running;
    bool shutdown;
} natsDispatcher;

static inline void nats_destroyQueuedMessages(natsDispatchQueue *queue)
{
    natsMsg *next = NULL;
    for (natsMsg *msg = queue->head; msg != NULL; msg = next)
    {
        next = msg->next;
        natsMsg_Destroy(msg);
    }
}

void natsSub_enqueueMessage(natsSubscription *sub, natsMsg *msg);
natsStatus natsSub_enqueueUserMessage(natsSubscription *sub, natsMsg *msg);

#endif /* DISPATCH_H_ */
