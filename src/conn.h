// Copyright 2015-2017 Apcera Inc. All rights reserved.

#ifndef CONN_H_
#define CONN_H_

#include "natsp.h"

#define RESP_INFO_POOL_MAX_SIZE (10)

#ifdef DEV_MODE
// For type safety

void natsConn_Lock(natsConnection *nc);
void natsConn_Unlock(natsConnection *nc);

#else
// We know what we are doing :-)

#define natsConn_Lock(c)    (natsMutex_Lock((c)->mu))
#define natsConn_Unlock(c)  (natsMutex_Unlock((c)->mu))

#endif // DEV_MODE

natsStatus
natsConn_create(natsConnection **newConn, natsOptions *options);

void
natsConn_retain(natsConnection *nc);

void
natsConn_release(natsConnection *nc);

natsStatus
natsConn_bufferWrite(natsConnection *nc, const char *buffer, int len);

natsStatus
natsConn_bufferFlush(natsConnection *nc);

bool
natsConn_isClosed(natsConnection *nc);

bool
natsConn_isReconnecting(natsConnection *nc);

void
natsConn_kickFlusher(natsConnection *nc);

natsStatus
natsConn_processMsg(natsConnection *nc, char *buf, int bufLen);

void
natsConn_processOK(natsConnection *nc);

void
natsConn_processErr(natsConnection *nc, char *buf, int bufLen);

void
natsConn_processPing(natsConnection *nc);

void
natsConn_processPong(natsConnection *nc);

natsStatus
natsConn_subscribe(natsSubscription **newSub,
                   natsConnection *nc, const char *subj, const char *queue,
                   int64_t timeout, natsMsgHandler cb, void *cbClosure);

natsStatus
natsConn_unsubscribe(natsConnection *nc, natsSubscription *sub, int max);

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *sub);

void
natsConn_processAsyncINFO(natsConnection *nc, char *buf, int len);

natsStatus
natsConn_addRespInfo(respInfo **newResp, natsConnection *nc, char *respInbox, int respInboxSize);

void
natsConn_disposeRespInfo(natsConnection *nc, respInfo *resp, bool needsLock);

natsStatus
natsConn_createRespMux(natsConnection *nc, char *ginbox, natsMsgHandler cb);

natsStatus
natsConn_waitForRespMux(natsConnection *nc);

natsStatus
natsConn_initResp(natsConnection *nc, char *ginbox, int ginboxSize);

void
natsConn_destroyRespPool(natsConnection *nc);

#endif /* CONN_H_ */
