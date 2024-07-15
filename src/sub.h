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

#ifndef SUB_H_
#define SUB_H_

#include "natsp.h"

#define SUB_DRAIN_STARTED ((uint8_t)1)
#define SUB_DRAIN_COMPLETE ((uint8_t)2)

#define natsSub_drainStarted(s) (((s)->drainState & SUB_DRAIN_STARTED) != 0)
#define natsSub_drainComplete(s) (((s)->drainState & SUB_DRAIN_COMPLETE) != 0)

extern bool testDrainAutoUnsubRace;

static inline void natsSub_Lock(natsSubscription *sub)
{
    natsMutex_Lock(sub->mu);
}

static inline void natsSub_Unlock(natsSubscription *sub)
{
    natsMutex_Unlock(sub->mu);
}

static inline void natsSub_lockRetain(natsSubscription *sub)
{
    natsSub_Lock(sub);

    sub->refs++;
}

static inline void natsSub_retain(natsSubscription *sub)
{
    natsSub_lockRetain(sub);
    natsSub_Unlock(sub);
}

void natsSub_release(natsSubscription *sub);
void natsSub_unlockRelease(natsSubscription *sub);

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, int64_t timeout, natsMsgHandler cb, void *cbClosure,
               bool noLibDlvPool, jsSub *jsi);

bool
natsSub_setMax(natsSubscription *sub, uint64_t max);

void
natsSub_initDrain(natsSubscription *sub);

natsStatus
natsSub_startDrain(natsSubscription *sub, int64_t timeout);

void
natsSub_setDrainCompleteState(natsSubscription *sub);

void
natsSub_setDrainSkip(natsSubscription *sub, natsStatus s);

void
natsSub_updateDrainStatus(natsSubscription *sub, natsStatus s);

void
natsSub_drain(natsSubscription *sub);

natsStatus
natsSub_nextMsg(natsMsg **nextMsg, natsSubscription *sub, int64_t timeout, bool pullSubInternal);

void
natsSub_close(natsSubscription *sub, bool connectionClosed);

natsStatus
natsSub_enqueueMsgImpl(natsSubscription *sub, natsMsg *msg, bool ctrl);

static natsStatus natsSub_enqueueMsg(natsSubscription *sub, natsMsg *msg)
{
    return natsSub_enqueueMsgImpl(sub, msg, false);
}

static natsStatus natsSub_enqueueCtrlMsg(natsSubscription *sub, natsMsg *msg)
{
    return natsSub_enqueueMsgImpl(sub, msg, true);
}

natsStatus nats_createControlMessages(natsSubscription *sub);

#endif /* SUB_H_ */
