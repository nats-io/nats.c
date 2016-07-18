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

void
natsSub_retain(natsSubscription *sub);

void
natsSub_release(natsSubscription *sub);

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, natsMsgHandler cb, void *cbClosure);

void
natsSub_close(natsSubscription *sub, bool connectionClosed);

#endif /* SUB_H_ */
