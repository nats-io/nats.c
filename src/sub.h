// Copyright 2015-2021 The NATS Authors
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

#ifdef DEV_MODE
// For type safety...

void natsSub_Lock(natsSubscription *sub);
void natsSub_Unlock(natsSubscription *sub);

#else

#define natsSub_Lock(s)      natsMutex_Lock((s)->mu)
#define natsSub_Unlock(s)    natsMutex_Unlock((s)->mu)

#endif // DEV_MODE

#define SUB_DRAIN_STARTED     ((uint8_t) 1)
#define SUB_DRAIN_COMPLETE    ((uint8_t) 2)

#define natsSub_drainStarted(s)     (((s)->drainState & SUB_DRAIN_STARTED) != 0)
#define natsSub_drainComplete(s)    (((s)->drainState & SUB_DRAIN_COMPLETE) != 0)

extern bool testDrainAutoUnsubRace;

void
natsSub_retain(natsSubscription *sub);

void
natsSub_release(natsSubscription *sub);

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
natsSubAndLdw_Lock(natsSubscription *sub);

void
natsSubAndLdw_Unlock(natsSubscription *sub);

void
natsSubAndLdw_LockAndRetain(natsSubscription *sub);

void
natsSubAndLdw_UnlockAndRelease(natsSubscription *sub);

void
natsSub_close(natsSubscription *sub, bool connectionClosed);

#endif /* SUB_H_ */
