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

#define SET_WRITE_DEADLINE(nc) if ((nc)->opts->writeDeadline > 0) natsDeadline_Init(&(nc)->sockCtx.writeDeadline, (nc)->opts->writeDeadline)

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

natsStatus
natsConn_flushOrKickFlusher(natsConnection *nc);

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

#define natsConn_subscribeNoPool(sub, nc, subj, cb, closure)                            natsConn_subscribeImpl((sub), (nc), true, (subj), NULL, 0, (cb), (closure), true, NULL)
#define natsConn_subscribeNoPoolNoLock(sub, nc, subj, cb, closure)                      natsConn_subscribeImpl((sub), (nc), false, (subj), NULL, 0, (cb), (closure), true, NULL)
#define natsConn_subscribeSyncNoPool(sub, nc, subj)                                     natsConn_subscribeNoPool((sub), (nc), (subj), NULL, NULL)
#define natsConn_subscribeWithTimeout(sub, nc, subj, timeout, cb, closure)              natsConn_subscribeImpl((sub), (nc), true, (subj), NULL, (timeout), (cb), (closure), false, NULL)
#define natsConn_subscribe(sub, nc, subj, cb, closure)                                  natsConn_subscribeWithTimeout((sub), (nc), (subj), 0, (cb), (closure))
#define natsConn_subscribeSync(sub, nc, subj)                                           natsConn_subscribe((sub), (nc), (subj), NULL, NULL)
#define natsConn_queueSubscribeWithTimeout(sub, nc, subj, queue, timeout, cb, closure)  natsConn_subscribeImpl((sub), (nc), true, (subj), (queue), (timeout), (cb), (closure), false, NULL)
#define natsConn_queueSubscribe(sub, nc, subj, queue, cb, closure)                      natsConn_queueSubscribeWithTimeout((sub), (nc), (subj), (queue), 0, (cb), (closure))
#define natsConn_queueSubscribeSync(sub, nc, subj, queue)                               natsConn_queueSubscribe((sub), (nc), (subj), (queue), NULL, NULL)

natsStatus
natsConn_subscribeImpl(natsSubscription **newSub,
                       natsConnection *nc, bool lock, const char *subj, const char *queue,
                       int64_t timeout, natsMsgHandler cb, void *cbClosure,
                       bool preventUseOfLibDlvPool, jsSub *jsi);

natsStatus
natsConn_unsubscribe(natsConnection *nc, natsSubscription *sub, int max, bool drainMode, int64_t timeout);

natsStatus
natsConn_enqueueUnsubProto(natsConnection *nc, int64_t sid);

natsStatus
natsConn_drainSub(natsConnection *nc, natsSubscription *sub, bool checkConnDrainStatus);

bool
natsConn_isDraining(natsConnection *nc);

bool
natsConn_isDrainingPubs(natsConnection *nc);

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *sub);

void
natsConn_processAsyncINFO(natsConnection *nc, char *buf, int len);

natsStatus
natsConn_addRespInfo(respInfo **newResp, natsConnection *nc, char *respInbox, int respInboxSize);

void
natsConn_disposeRespInfo(natsConnection *nc, respInfo *resp, bool needsLock);

natsStatus
natsConn_initResp(natsConnection *nc, natsMsgHandler cb);

void
natsConn_destroyRespPool(natsConnection *nc);

natsStatus
natsConn_publish(natsConnection *nc, natsMsg *msg, const char *reply, bool directFlush);

natsStatus
natsConn_userCreds(char **userJWT, char **customErrTxt, void *closure);

natsStatus
natsConn_signatureHandler(char **customErrTxt, unsigned char **sig, int *sigLen, const char *nonce, void *closure);

natsStatus
natsConn_sendSubProto(natsConnection *nc, const char *subject, const char *queue, int64_t sid);

natsStatus
natsConn_sendUnsubProto(natsConnection *nc, int64_t subId, int max);

#define natsConn_setFilter(c, f) natsConn_setFilterWithClosure((c), (f), NULL)

void
natsConn_setFilterWithClosure(natsConnection *nc, natsMsgFilter f, void* closure);

natsStatus
natsConn_initInbox(natsConnection *nc, char *buf, int bufSize, char **newInbox, bool *allocated);

natsStatus
natsConn_newInbox(natsConnection *nc, natsInbox **newInbox);

bool
natsConn_srvVersionAtLeast(natsConnection *nc, int major, int minor, int update);

void
natsConn_defaultErrHandler(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure);

void
natsConn_close(natsConnection *nc);

void
natsConn_destroy(natsConnection *nc, bool fromPublicDestroy);

#endif /* CONN_H_ */
