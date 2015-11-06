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
                   natsMsgHandler cb, void *cbClosure, bool noDelay);

natsStatus
natsConn_unsubscribe(natsConnection *nc, natsSubscription *sub, int max);

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *sub, bool needsLock);

// PUBLIC
NATS_EXTERN natsStatus
natsConnection_Connect(natsConnection **nc, natsOptions *options);

NATS_EXTERN natsStatus
natsConnection_ConnectTo(natsConnection **nc, const char *url);

NATS_EXTERN natsStatus
natsConnection_Publish(natsConnection *nc, const char *subj,
                       const void *data, int dataLen);

NATS_EXTERN natsStatus
natsConnection_PublishString(natsConnection *nc, const char *subj,
                             const char *str);

NATS_EXTERN natsStatus
natsConnection_PublishRequestString(natsConnection *nc, const char *subj,
                                    const char *reply, const char *str);

NATS_EXTERN natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg);

NATS_EXTERN natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout);

NATS_EXTERN natsStatus
natsConnection_RequestString(natsMsg **replyMsg, natsConnection *nc,
                             const char *subj, const char *str,
                             int64_t timeout);

NATS_EXTERN natsStatus
natsConnection_Subscribe(natsSubscription **sub, natsConnection *nc,
                         const char *subject, natsMsgHandler cb,
                         void *cbClosure);

NATS_EXTERN natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc,
                             const char *subject);

NATS_EXTERN natsStatus
natsConnection_QueueSubscribe(natsSubscription **sub, natsConnection *nc,
                              const char *subject, const char *queueGroup,
                              natsMsgHandler cb, void *cbClosure);

NATS_EXTERN natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                                  const char *subject, const char *queueGroup);

NATS_EXTERN int
natsConnection_Buffered(natsConnection *nc);

NATS_EXTERN bool
natsConnection_IsClosed(natsConnection *nc);

NATS_EXTERN bool
natsConnection_IsReconnecting(natsConnection *nc);

NATS_EXTERN natsConnStatus
natsConnection_Status(natsConnection *nc);

NATS_EXTERN natsStatus
natsConnection_Flush(natsConnection *nc);

NATS_EXTERN natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout);

NATS_EXTERN int64_t
natsConnection_GetMaxPayload(natsConnection *nc);

NATS_EXTERN natsStatus
natsConnection_GetStats(natsConnection *nc, natsStatistics *stats);

NATS_EXTERN natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize);

NATS_EXTERN natsStatus
natsConnection_GetConnectedServerId(natsConnection *nc, char *buffer, size_t bufferSize);

NATS_EXTERN natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError);

NATS_EXTERN void
natsConnection_Close(natsConnection *nc);

NATS_EXTERN void
natsConnection_Destroy(natsConnection *nc);


#endif /* CONN_H_ */
