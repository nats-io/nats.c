// Copyright 2015 Apcera Inc. All rights reserved.

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

// PRIVATE

void
natsSub_retain(natsSubscription *sub);

void
natsSub_release(natsSubscription *sub);

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, natsMsgHandler cb, void *cbClosure);

// PUBLIC

natsStatus
natsSubscription_QueuedMsgs(natsSubscription *sub, uint64_t *queuedMsgs);

natsStatus
natsSubscription_NextMsg(natsMsg **nextMsg, natsSubscription *sub, int64_t timeout);

natsStatus
natsSubscription_AutoUnsubscribe(natsSubscription *sub, int max);

natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub);

bool
natsSubscription_IsValid(natsSubscription *sub);

void
natsSubscription_Destroy(natsSubscription *sub);

#endif /* SUB_H_ */
