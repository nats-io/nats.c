// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef CONN_H_
#define CONN_H_

#include "natsp.h"

#ifdef DEV_MODE
// For type safety

void natsConn_Lock(natsConnection *nc);
void natsConn_Unlock(natsConnection *nc);

#else
// We know what we are doing :-)

#define natsConn_Lock(c)    (natsMutex_Lock((c)->mu))
#define natsConn_Unlock(c)  (natsMutex_Unlock((c)->mu))

#endif // DEV_MODE

// PRIVATE
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
                   natsMsgHandler cb, void *cbClosure);

natsStatus
natsConn_unsubscribe(natsConnection *nc, natsSubscription *sub, int max);

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *sub, bool needsLock);

// PUBLIC
natsStatus
natsConnection_Connect(natsConnection **nc, natsOptions *options);

natsStatus
natsConnection_ConnectTo(natsConnection **nc, const char *url);

natsStatus
natsConnection_Publish(natsConnection *nc, const char *subj,
                       const void *data, int dataLen);

natsStatus
natsConnection_PublishString(natsConnection *nc, const char *subj,
                             const char *str);

natsStatus
natsConnection_PublishRequestString(natsConnection *nc, const char *subj,
                                    const char *reply, const char *str);

natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg);

natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout);

natsStatus
natsConnection_RequestString(natsMsg **replyMsg, natsConnection *nc,
                             const char *subj, const char *str,
                             int64_t timeout);

natsStatus
natsConnection_Subscribe(natsSubscription **sub, natsConnection *nc,
                         const char *subject, natsMsgHandler cb,
                         void *cbClosure);

natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc,
                             const char *subject);

natsStatus
natsConnection_QueueSubscribe(natsSubscription **sub, natsConnection *nc,
                              const char *subject, const char *queueGroup,
                              natsMsgHandler cb, void *cbClosure);

natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                                  const char *subject, const char *queueGroup);

int
natsConnection_Buffered(natsConnection *nc);

bool
natsConnection_IsClosed(natsConnection *nc);

bool
natsConnection_IsReconnecting(natsConnection *nc);

natsConnStatus
natsConnection_Status(natsConnection *nc);

natsStatus
natsConnection_Flush(natsConnection *nc);

natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout);

int64_t
natsConnection_GetMaxPayload(natsConnection *nc);

natsStatus
natsConnection_GetStats(natsConnection *nc, natsStatistics *stats);

natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize);

natsStatus
natsConnection_GetConnectedServerId(natsConnection *nc, char *buffer, size_t bufferSize);

natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError);

void
natsConnection_Close(natsConnection *nc);

void
natsConnection_Destroy(natsConnection *nc);


#endif /* CONN_H_ */
