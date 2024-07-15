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
    unsigned running : 1;
    unsigned shutdown : 1;
} natsDispatcher;

// Dispatcher main function with syntactic sugar for the callstack.
void nats_dispatchMessages(natsDispatcher *d);
static void nats_dispatchMessagesPoolThreadf(void *arg) { nats_dispatchMessages((natsDispatcher *)arg); }
static void nats_dispatchMessagesOwnThreadf(void *arg) { nats_dispatchMessages((natsDispatcher *)arg); }
static void nats_dispatchRepliesPoolThreadf(void *arg) { nats_dispatchMessages((natsDispatcher *)arg); }
static void nats_dispatchRepliesOwnThreadf(void *arg) { nats_dispatchMessages((natsDispatcher *)arg); }

static inline void nats_destroyQueuedMessages(natsDispatchQueue *queue)
{
    natsMsg *next = NULL;
    int n = 0;
    for (natsMsg *msg = queue->head; msg != NULL; msg = next, n++)
    {
        next = msg->next;
        natsMsg_Destroy(msg);
    }
}

#endif /* DISPATCH_H_ */
