// Copyright 2015-2018 The NATS Authors
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

#ifndef NATSP_H_
#define NATSP_H_

#if defined(_WIN32)
# include "include/n-win.h"
#else
# include "include/n-unix.h"
#endif

#if defined(NATS_HAS_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#else
#define SSL             void*
#define SSL_free(c)     { (c) = NULL; }
#define SSL_CTX         void*
#define SSL_CTX_free(c) { (c) = NULL; }
#define NO_SSL_ERR  "The library was built without SSL support!"
#endif

#include "err.h"
#include "nats.h"
#include "buf.h"
#include "parser.h"
#include "timer.h"
#include "url.h"
#include "srvpool.h"
#include "msg.h"
#include "asynccb.h"
#include "hash.h"
#include "stats.h"
#include "natstime.h"
#include "nuid.h"

// Comment/uncomment to replace some function calls with direct structure
// access
//#define DEV_MODE    (1)

#define LIB_NATS_VERSION_STRING             NATS_VERSION_STRING
#define LIB_NATS_VERSION_NUMBER             NATS_VERSION_NUMBER
#define LIB_NATS_VERSION_REQUIRED_NUMBER    NATS_VERSION_REQUIRED_NUMBER

#define CString     "C"

#define _OK_OP_     "+OK"
#define _ERR_OP_    "-ERR"
#define _MSG_OP_    "MSG"
#define _PING_OP_   "PING"
#define _PONG_OP_   "PONG"
#define _INFO_OP_   "INFO"

#define _CRLF_      "\r\n"
#define _SPC_       " "
#define _PUB_P_     "PUB "

#define _PING_PROTO_         "PING\r\n"
#define _PONG_PROTO_         "PONG\r\n"
#define _PUB_PROTO_          "PUB %s %s %d\r\n"
#define _SUB_PROTO_          "SUB %s %s %d\r\n"
#define _UNSUB_PROTO_        "UNSUB %" PRId64 " %d\r\n"
#define _UNSUB_NO_MAX_PROTO_ "UNSUB %" PRId64 " \r\n"

#define STALE_CONNECTION     "Stale Connection"
#define PERMISSIONS_ERR      "Permissions Violation"
#define AUTHORIZATION_ERR    "Authorization Violation"

#define _CRLF_LEN_          (2)
#define _SPC_LEN_           (1)
#define _PUB_P_LEN_         (4)
#define _PING_OP_LEN_       (4)
#define _PONG_OP_LEN_       (4)
#define _PING_PROTO_LEN_    (6)
#define _PONG_PROTO_LEN_    (6)
#define _OK_OP_LEN_         (3)
#define _ERR_OP_LEN_        (4)

static const char *inboxPrefix = "_INBOX.";
#define NATS_INBOX_PRE_LEN (7)

#define NATS_REQ_ID_OFFSET  (NATS_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1)
#define NATS_MAX_REQ_ID_LEN (19) // to display 2^63 number

#define WAIT_FOR_READ       (0)
#define WAIT_FOR_WRITE      (1)
#define WAIT_FOR_CONNECT    (2)

#define DEFAULT_PORT_STRING "4222"

#define DEFAULT_DRAIN_TIMEOUT   30000 // 30 seconds

#define MAX_FRAMES (50)

#define DUP_STRING(s, s1, s2) \
        { \
            (s1) = NATS_STRDUP(s2); \
            if ((s1) == NULL) \
                (s) = nats_setDefaultError(NATS_NO_MEMORY); \
        }

#define IF_OK_DUP_STRING(s, s1, s2) \
        if ((s) == NATS_OK) \
            DUP_STRING((s), (s1), (s2))

extern int64_t gLockSpinCount;

typedef void (*natsInitOnceCb)(void);

typedef void (*natsOnCompleteCB)(void *closure);

typedef struct __natsControl
{
    char    *op;
    char    *args;

} natsControl;

typedef struct __natsServerInfo
{
    char        *id;
    char        *host;
    int         port;
    char        *version;
    bool        authRequired;
    bool        tlsRequired;
    int64_t     maxPayload;
    char        **connectURLs;
    int         connectURLsCount;
    int         proto;

} natsServerInfo;

typedef struct __natsSSLCtx
{
    natsMutex   *lock;
    int         refs;
    SSL_CTX     *ctx;
    char        *expectedHostname;
    bool        skipVerify;

} natsSSLCtx;

#define natsSSLCtx_getExpectedHostname(ctx) ((ctx)->expectedHostname)

typedef struct
{
    natsEvLoop_Attach           attach;
    natsEvLoop_ReadAddRemove    read;
    natsEvLoop_WriteAddRemove   write;
    natsEvLoop_Detach           detach;

} natsEvLoopCallbacks;

struct __natsOptions
{
    // This field must be the first (see natsOptions_clone, same if you add
    // allocated fields such as strings).
    natsMutex               *mu;

    char                    *url;
    char                    **servers;
    int                     serversCount;
    bool                    noRandomize;
    int64_t                 timeout;
    char                    *name;
    bool                    verbose;
    bool                    pedantic;
    bool                    allowReconnect;
    bool                    secure;
    int                     maxReconnect;
    int64_t                 reconnectWait;
    int                     reconnectBufSize;

    char                    *user;
    char                    *password;
    char                    *token;

    natsConnectionHandler   closedCb;
    void                    *closedCbClosure;

    natsConnectionHandler   disconnectedCb;
    void                    *disconnectedCbClosure;

    natsConnectionHandler   reconnectedCb;
    void                    *reconnectedCbClosure;

    natsConnectionHandler   discoveredServersCb;
    void                    *discoveredServersClosure;

    natsConnectionHandler   connectedCb;
    void                    *connectedCbClosure;

    natsErrHandler          asyncErrCb;
    void                    *asyncErrCbClosure;

    int64_t                 pingInterval;
    int                     maxPingsOut;
    int                     maxPendingMsgs;

    natsSSLCtx              *sslCtx;

    void                    *evLoop;
    natsEvLoopCallbacks     evCbs;

    bool                    libMsgDelivery;

    int                     orderIP; // possible values: 0,4,6,46,64

    // forces the old method of Requests that utilize
    // a new Inbox and a new Subscription for each request
    bool                    useOldRequestStyle;

    // If set to true, the Publish call will flush in place and
    // not rely on the flusher.
    bool                    sendAsap;

    // NoEcho configures whether the server will echo back messages
    // that are sent on this connection if we also have matching subscriptions.
    // Note this is supported on servers >= version 1.2. Proto 1 or greater.
    bool                    noEcho;

    // If set to true, in case of failed connect, tries again using
    // reconnect options values.
    bool                    retryOnFailedConnect;
};

typedef struct __natsMsgList
{
    natsMsg     *head;
    natsMsg     *tail;
    int         msgs;
    int         bytes;

} natsMsgList;

typedef struct __natsMsgDlvWorker
{
    natsMutex       *lock;
    natsCondition   *cond;
    natsThread      *thread;
    bool            inWait;
    bool            shutdown;
    natsMsgList     msgList;

} natsMsgDlvWorker;

struct __natsSubscription
{
    natsMutex                   *mu;

    int                         refs;

    // This is non-zero when auto-unsubscribe is used.
    uint64_t                    max;

    // This is updated in the delivery thread (or NextMsg) and indicates
    // how many message have been presented to the callback (or returned
    // from NextMsg). Like 'msgs', this is also used to determine if we
    // have reached the max number of messages.
    uint64_t                    delivered;

    // The list of messages waiting to be delivered to the callback (or
    // returned from NextMsg).
    natsMsgList                 msgList;

    // True if msgList.count is over pendingMax
    bool                        slowConsumer;

    // Condition variable used to wait for message delivery.
    natsCondition               *cond;

    // This is > 0 when the delivery thread (or NextMsg) goes into a
    // condition wait.
    int                         inWait;

    // The subscriber is closed (or closing).
    bool                        closed;

    // Indicates if this subscription is in drained mode.
    bool                        draining;

    // Same than draining but for the global delivery situation.
    // This boolean will be switched off when processed, as opposed
    // to draining that once set does not get reset.
    bool                        libDlvDraining;

    // If true, the subscription is closed, but because the connection
    // was closed, not because of subscription (auto-)unsubscribe.
    bool                        connClosed;

    // Subscriber id. Assigned during the creation, does not change after that.
    int64_t                     sid;

    // Subject that represents this subscription. This can be different
    // than the received subject inside a Msg if this is a wildcard.
    char                        *subject;

    // Optional queue group name. If present, all subscriptions with the
    // same name will form a distributed queue, and each message will
    // only be processed by one member of the group.
    char                        *queue;

    // Reference to the connection that created this subscription.
    struct __natsConnection     *conn;

    // Delivery thread (for async subscription).
    natsThread                  *deliverMsgsThread;

    // If message delivery is done by the library instead, this is the
    // reference to the worker handling this subscription.
    natsMsgDlvWorker            *libDlvWorker;

    // Message callback and closure (for async subscription).
    natsMsgHandler              msgCb;
    void                        *msgCbClosure;

    int64_t                     timeout;
    natsTimer                   *timeoutTimer;
    bool                        timedOut;
    bool                        timeoutSuspended;

    // Pending limits, etc..
    int                         msgsMax;
    int                         bytesMax;
    int                         msgsLimit;
    int                         bytesLimit;
    int64_t                     dropped;

    // Complete callback
    natsOnCompleteCB            onCompleteCB;
    void                        *onCompleteCBClosure;

};

typedef struct __natsPong
{
    int64_t             id;

    struct __natsPong   *prev;
    struct __natsPong   *next;

} natsPong;

typedef struct __natsPongList
{
    natsPong            *head;
    natsPong            *tail;

    int64_t             incoming;
    int64_t             outgoingPings;

    natsPong            cached;

    natsCondition       *cond;

} natsPongList;

typedef struct __natsSockCtx
{
    natsSock        fd;
    bool            fdActive;

    // We switch to blocking socket after receiving the PONG to the first PING
    // during the connect process. Should we make all read/writes non blocking,
    // then we will use two different fd sets, and also probably pass deadlines
    // individually as opposed to use one at the connection level.
    fd_set          *fdSet;
#ifdef _WIN32
    fd_set          *errSet;
#endif
    natsDeadline    deadline;

    SSL             *ssl;

    // This is true when we are using an external event loop (such as libuv).
    bool            useEventLoop;

    int             orderIP; // possible values: 0,4,6,46,64

} natsSockCtx;

typedef struct __respInfo
{
    natsMutex           *mu;
    natsCondition       *cond;
    natsMsg             *msg;
    bool                closed;
    bool                removed;
    bool                pooled;

} respInfo;

struct __natsConnection
{
    natsMutex           *mu;
    natsOptions         *opts;
    const natsUrl       *url;

    int                 refs;

    natsSockCtx         sockCtx;

    natsSrvPool         *srvPool;

    natsBuffer          *pending;
    bool                usePending;

    natsBuffer          *bw;
    natsBuffer          *scratch;

    natsServerInfo      info;

    int64_t             ssid;
    natsHash            *subs;
    natsMutex           *subsMu;

    natsConnStatus      status;
    bool                initc; // true if the connection is performing the initial connect
    natsStatus          err;
    char                errStr[256];

    natsParser          *ps;
    natsTimer           *ptmr;
    int                 pout;

    natsPongList        pongs;

    natsThread          *readLoopThread;

    natsThread          *flusherThread;
    natsCondition       *flusherCond;
    bool                flusherSignaled;
    bool                flusherStop;

    natsThread          *reconnectThread;
    int                 inReconnect;
    natsCondition       *reconnectCond;

    natsStatistics      stats;

    natsTimer           *drainTimer;
    int64_t             drainDeadline;

    // New Request style
    char                respId[NATS_MAX_REQ_ID_LEN+1];
    int                 respIdPos;
    int                 respIdVal;
    char                *respSub;   // The wildcard subject
    natsSubscription    *respMux;   // A single response subscription
    natsCondition       *respReady; // For race when initializing the wildcard subscription
    natsStrHash         *respMap;   // Request map for the response msg
    respInfo            **respPool;
    int                 respPoolSize;
    int                 respPoolIdx;

    struct
    {
        bool            attached;
        bool            writeAdded;
        void            *buffer;
        void            *data;
    } el;
};

//
// Library
//
void
natsSys_Init(void);

void
natsLib_Retain(void);

void
natsLib_Release(void);

void
nats_resetTimer(natsTimer *t, int64_t newInterval);

void
nats_stopTimer(natsTimer *t);

// Returns the number of timers that have been created and not stopped.
int
nats_getTimersCount(void);

// Returns the number of timers actually in the list. This should be
// equal to nats_getTimersCount() or nats_getTimersCount() - 1 when a
// timer thread is invoking a timer's callback.
int
nats_getTimersCountInList(void);

natsStatus
nats_postAsyncCbInfo(natsAsyncCbInfo *info);

void
nats_sslRegisterThreadForCleanup(void);

natsStatus
nats_sslInit(void);

natsStatus
natsInbox_init(char *inbox, int inboxLen);

natsStatus
natsLib_msgDeliveryPostControlMsg(natsSubscription *sub);

natsStatus
natsLib_msgDeliveryAssignWorker(natsSubscription *sub);

bool
natsLib_isLibHandlingMsgDeliveryByDefault(void);

void
natsLib_getMsgDeliveryPoolInfo(int *maxSize, int *size, int *idx, natsMsgDlvWorker ***workersArray);

natsLocale
natsLib_getLocale(void);

//
// Threads
//
typedef void (*natsThreadCb)(void *arg);

natsStatus
natsThread_Create(natsThread **t, natsThreadCb cb, void *arg);

bool
natsThread_IsCurrent(natsThread *t);

void
natsThread_Join(natsThread *t);

void
natsThread_Detach(natsThread *t);

void
natsThread_Yield(void);

void
natsThread_Destroy(natsThread *t);

natsStatus
natsThreadLocal_CreateKey(natsThreadLocal *tl, void (*destructor)(void*));

void*
natsThreadLocal_Get(natsThreadLocal tl);

#define natsThreadLocal_Set(k, v) natsThreadLocal_SetEx((k), (v), true)

natsStatus
natsThreadLocal_SetEx(natsThreadLocal tl, const void *value, bool setErr);

void
natsThreadLocal_DestroyKey(natsThreadLocal tl);

bool
nats_InitOnce(natsInitOnceType *control, natsInitOnceCb cb);


//
// Conditions
//
natsStatus
natsCondition_Create(natsCondition **cond);

void
natsCondition_Wait(natsCondition *cond, natsMutex *mutex);

natsStatus
natsCondition_TimedWait(natsCondition *cond, natsMutex *mutex, int64_t timeout);

natsStatus
natsCondition_AbsoluteTimedWait(natsCondition *cond, natsMutex *mutex,
                                int64_t absoluteTime);

void
natsCondition_Signal(natsCondition *cond);

void
natsCondition_Broadcast(natsCondition *cond);

void
natsCondition_Destroy(natsCondition *cond);

//
// Mutexes
//
natsStatus
natsMutex_Create(natsMutex **newMutex);

void
natsMutex_Lock(natsMutex *m);

bool
natsMutex_TryLock(natsMutex *m);

void
natsMutex_Unlock(natsMutex *m);

void
natsMutex_Destroy(natsMutex *m);


#endif /* NATSP_H_ */
