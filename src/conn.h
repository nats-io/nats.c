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

#include "comsock.h"
#include "conn_write.h"

// CLIENT_PROTO_ZERO is the original client protocol from 2009.
// http://nats.io/documentation/internals/nats-protocol/
#define CLIENT_PROTO_ZERO (0)

// CLIENT_PROTO_INFO signals a client can receive more then the original INFO block.
// This can be used to update clients on other cluster members, etc.
#define CLIENT_PROTO_INFO (1)

#define NATS_EV_ADD (true)
#define NATS_EV_REMOVE (false)

typedef enum
{
    NATS_CONN_STATUS_DISCONNECTED = 0, ///< The connection has been disconnected
    NATS_CONN_STATUS_CONNECTING,       ///< The connection is in the process or connecting
    NATS_CONN_STATUS_CONNECTED,        ///< The connection is connected
    NATS_CONN_STATUS_CLOSED,           ///< The connection is closed
    NATS_CONN_STATUS_RECONNECTING,     ///< The connection is in the process or reconnecting
    NATS_CONN_STATUS_DRAINING_SUBS,    ///< The connection is draining subscriptions
    NATS_CONN_STATUS_DRAINING_PUBS,    ///< The connection is draining publishers

} natsConnStatus;

struct __natsServerInfo
{
    const char *id;
    const char *host;
    int port;
    const char *version;
    bool authRequired;
    bool tlsRequired;
    bool tlsAvailable;
    int64_t maxPayload;
    const char **connectURLs;
    int connectURLsCount;
    int proto;
    uint64_t CID;
    const char *nonce;
    const char *clientIP;
    bool lameDuckMode;
    bool headers;
};

struct __natsConnection
{
    natsOptions *opts;
    natsEventLoop ev;
    void *evState;
    bool evAttached;
    natsServer *cur;
    natsSockCtx sockCtx;

    int refs;
    natsPool *lifetimePool;
    natsPool *connectPool;
    natsPool *opPool;

    natsConnStatus state;

    natsStatus err;
    char errStr[256];
    natsParser *ps;
    natsWriteQueue writeChain;
    int pingsOut;

    natsServers *servers;
    natsServerInfo *info;
    struct
    {
        int ma;
        int mi;
        int up;
    } srvVersion;

    natsConnectionStatistics stats;
};

static inline void natsConn_retain(natsConnection *nc)
{
    if (nc == NULL)
        return;
    nc->refs++;
}

void natsConn_freeConn(natsConnection *nc);
static inline void natsConn_release(natsConnection *nc)
{
    if (nc == NULL)
        return;
    if (--(nc->refs) == 0)
        natsConn_freeConn(nc);
}

#define natsConn_isConnecting(nc) ((nc)->state == NATS_CONN_STATUS_CONNECTING)
#define natsConn_isDrainingPubs(nc) ((nc)->state == NATS_CONN_STATUS_DRAINING_PUBS)
#define natsConn_isDrainingSubs(nc) ((nc)->state == NATS_CONN_STATUS_DRAINING_SUBS)
#define natsConn_isDraining(nc) (natsConn_isDrainingPubs(nc) || natsConn_isDrainingSubs(nc))
#define natsConn_isConnected(nc) (((nc)->state == NATS_CONN_STATUS_CONNECTED || natsConn_isDraining(nc)))
#define natsConn_isClosed(nc) ((nc)->state == NATS_CONN_STATUS_CLOSED)
#define natsConn_initialConnectDone(nc) ((nc)->info != NULL)

natsStatus natsConn_createParser(natsParser **ps, natsPool *pool);
natsStatus natsConn_parseOp(natsConnection *nc, uint8_t *buf, uint8_t *end, size_t *consumed);
bool natsConn_expectingNewOp(natsParser *ps);

static bool natsConn_isReconnecting(natsConnection *nc)
{
    return false; // <>/<> FIXME
}

natsStatus natsConn_processMsg(natsConnection *nc, char *buf, int bufLen);
void natsConn_processOK(natsConnection *nc);
void natsConn_processErr(natsConnection *nc, char *buf, int bufLen);
natsStatus natsConn_processPing(natsConnection *nc);
natsStatus natsConn_processPong(natsConnection *nc);
natsStatus natsConn_processInfo(natsConnection *nc, nats_JSON *json);
bool natsConn_srvVersionAtLeast(natsConnection *nc, int major, int minor, int update);
bool natsConn_processOpError(natsConnection *nc, natsStatus s);

natsStatus
natsConn_sendPing(natsConnection *nc);
natsStatus
natsConn_sendConnect(natsConnection *nc);

#define natsConn_subscribeNoPool(sub, nc, subj, cb, closure) natsConn_subscribeImpl((sub), (nc), true, (subj), NULL, 0, (cb), (closure), true, NULL)
#define natsConn_subscribeNoPoolNoLock(sub, nc, subj, cb, closure) natsConn_subscribeImpl((sub), (nc), false, (subj), NULL, 0, (cb), (closure), true, NULL)
#define natsConn_subscribeSyncNoPool(sub, nc, subj) natsConn_subscribeNoPool((sub), (nc), (subj), NULL, NULL)
#define natsConn_subscribeWithTimeout(sub, nc, subj, timeout, cb, closure) natsConn_subscribeImpl((sub), (nc), true, (subj), NULL, (timeout), (cb), (closure), false, NULL)
#define natsConn_subscribe(sub, nc, subj, cb, closure) natsConn_subscribeWithTimeout((sub), (nc), (subj), 0, (cb), (closure))
#define natsConn_subscribeSync(sub, nc, subj) natsConn_subscribe((sub), (nc), (subj), NULL, NULL)
#define natsConn_queueSubscribeWithTimeout(sub, nc, subj, queue, timeout, cb, closure) natsConn_subscribeImpl((sub), (nc), true, (subj), (queue), (timeout), (cb), (closure), false, NULL)
#define natsConn_queueSubscribe(sub, nc, subj, queue, cb, closure) natsConn_queueSubscribeWithTimeout((sub), (nc), (subj), (queue), 0, (cb), (closure))
#define natsConn_queueSubscribeSync(sub, nc, subj, queue) natsConn_queueSubscribe((sub), (nc), (subj), (queue), NULL, NULL)

#ifdef DEV_MODE_CONN
#define CONNTRACEf(fmt, ...) DEVTRACEf("CONN", fmt, __VA_ARGS__)
#define CONNDEBUGf(fmt, ...) DEVDEBUGf("CONN", fmt, __VA_ARGS__)
#define CONNERROR(str) DEVERROR("CONN", str)
#else
#define CONNTRACEf DEVNOLOGf
#define CONNDEBUGf DEVNOLOGf
#define CONNERROR DEVNOLOG
#endif

#ifdef DEV_MODE_CONN_TRACE
#define CONNTRACE_out(_buf) DEVTRACEf("->  ", "%zu: '%s'", (_buf)->len, (natsString_debugPrintable(_buf, 0)));
#define CONNTRACE_in(_buf) DEVTRACEf("<-  ", "%zu: '%s'", (_buf)->len, (natsString_debugPrintable(_buf, 0)));
#else
#define CONNTRACE_out(_buf)
#define CONNTRACE_in(_buf)
#endif

#endif /* CONN_H_ */
