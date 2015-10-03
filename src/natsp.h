// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef NATSP_H_
#define NATSP_H_

// Do this as early as possible
#undef FD_SETSIZE
#define FD_SETSIZE  (32768)

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
# include "include/n-win.h"
#else
# include "include/n-unix.h"
#endif

#include "status.h"
#include "buf.h"
#include "parser.h"
#include "timer.h"
#include "url.h"
#include "srvpool.h"
#include "msg.h"
#include "asynccb.h"
#include "hash.h"
#include "stats.h"
#include "time.h"

// Comment/uncomment to replace some function calls with direct structure
// access
//#define DEV_MODE    (1)

static const char* CString     = "C";
static const char* Version     = "0.1.1";

static const char* NATS_DEFAULT_URL = "nats://localhost:4222";

static const char* _OK_OP_     = "+OK";
static const char* _ERR_OP_    = "-ERR";
static const char* _MSG_OP_    = "MSG";
static const char* _PING_OP_   = "PING";
static const char* _PONG_OP_   = "PONG";
static const char* _INFO_OP_   = "INFO";

static const char* _CRLF_      = "\r\n";
static const char* _SPC_       = " ";
static const char* _PUB_P_     = "PUB ";

static const char* _PING_PROTO_         = "PING\r\n";
static const char* _PONG_PROTO_         = "PONG\r\n";
static const char* _PUB_PROTO_          = "PUB %s %s %d\r\n";
static const char* _SUB_PROTO_          = "SUB %s %s %d\r\n";
static const char* _UNSUB_PROTO_        = "UNSUB %d %d\r\n";
static const char* _UNSUB_NO_MAX_PROTO_ = "UNSUB %d \r\n";

static const char* STALE_CONNECTION     = "State Connection";
static int         STATE_CONNECTION_LEN = 16;

#define _CRLF_LEN_          (2)
#define _SPC_LEN_           (1)
#define _PUB_P_LEN_         (4)
#define _PING_OP_LEN_       (4)
#define _PONG_OP_LEN_       (4)
#define _PING_PROTO_LEN_    (6)
#define _PONG_PROTO_LEN_    (6)

extern int64_t gLockSpinCount;

// Forward declarations
struct __natsConnection;
struct __natsSubscription;

// natsMsgHandler is a callback function that processes messages delivered to
// asynchronous subscribers.
typedef void (*natsMsgHandler)(
        struct __natsConnection *nc,
        struct __natsSubscription *sub,
        struct __natsMsg *msg,
        void *closure);

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
    bool        sslRequired;
    int64_t     maxPayload;

} natsServerInfo;

typedef void (*natsConnectionHandler)(
        struct __natsConnection *nc, void *closure);

typedef void (*natsErrHandler)(
        struct __natsConnection *nc, struct __natsSubscription *sub, natsStatus err,
        void *closure);

typedef struct __natsOptions
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
    int                     maxReconnect;
    int64_t                 reconnectWait;

    natsConnectionHandler   closedCb;
    void                    *closedCbClosure;

    natsConnectionHandler   disconnectedCb;
    void                    *disconnectedCbClosure;

    natsConnectionHandler   reconnectedCb;
    void                    *reconnectedCbClosure;

    natsErrHandler          asyncErrCb;
    void                    *asyncErrCbClosure;

    int64_t                 pingInterval;
    int                     maxPingsOut;
    int                     maxPendingMsgs;

} natsOptions;

typedef struct __natsMsgList
{
    natsMsg     *head;
    natsMsg     *tail;
    int         count;

} natsMsgList;

typedef struct __natsSubscription
{
    natsMutex                   *mu;

    int                         refs;

    int64_t                     sid;

    // Subject that represents this subscription. This can be different
    // than the received subject inside a Msg if this is a wildcard.
    char                        *subject;

    // Optional queue group name. If present, all subscriptions with the
    // same name will form a distributed queue, and each message will
    // only be processed by one member of the group.
    char                        *queue;

    // These are protected by the connection's lock
    uint64_t                    msgs;
    uint64_t                    bytes;
    uint64_t                    max;

    // This is protected by the subscription's lock
    uint64_t                    delivered;

    struct __natsConnection     *conn;

    natsThread                  *deliverMsgsThread;
    natsCondition               *cond;
    bool                        signaled;
    bool                        closed;

    natsMsgHandler              msgCb;
    void                        *msgCbClosure;

    natsMsgList                 msgList;

    int                         pendingMax;

    bool                        slowConsumer;

} natsSubscription;

typedef struct __natsConnection
{
    natsMutex           *mu;
    natsOptions         *opts;
    const natsUrl       *url;

    int                 refs;
    natsSock            fd;

    // We switch to blocking socket after receiving the PONG to the first PING
    // during the connect process. Should we make all read/writes non blocking,
    // then we will use two different fd sets, and also probably pass deadlines
    // individually as opposed to use one at the connection level.
    fd_set              *fdSet;
    natsDeadline        deadline;

    natsSrvPool         *srvPool;

    natsBuffer          *pending;
    bool                usePending;

    natsBuffer          *bw;
    natsBuffer          *scratch;

    natsServerInfo      info;

    int64_t             ssid;
    natsHash            *subs;

    natsConnStatus      status;
    natsStatus          err;
    char                errStr[256];

    natsParser          *ps;
    natsTimer           *ptmr;
    int                 pout;

    natsCondition       *flushTimeoutCond;
    bool                inFlushTimeout;
    bool                flushTimeoutComplete;
    int64_t             pingId;
    int64_t             pongMark;
    int64_t             pongId;

    natsThread          *readLoopThread;

    natsThread          *flusherThread;
    natsCondition       *flusherCond;
    bool                flusherSignaled;
    bool                flusherStop;

    natsThread          *reconnectThread;

    natsStatistics      stats;

} natsConnection;

typedef char natsInbox;


//
// Library
//
void
natsSys_Init(void);

natsStatus
nats_Open(int64_t spinLockCount);

void
natsLib_Retain(void);

void
natsLib_Release(void);

void
nats_AddTimer(natsTimer *t);

void
nats_RemoveTimer(natsTimer *t);

natsStatus
nats_PostAsyncCbInfo(natsAsyncCbInfo *info);

natsStatus
natsInbox_Create(char **newInbox);

void
natsInbox_Destroy(char *inbox);

void
nats_Close(void);


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
natsThread_Destroy(natsThread *t);

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
natsCondition_TimedWait(natsCondition *cond, natsMutex *mutex, uint64_t timeout);

natsStatus
natsCondition_AbsoluteTimedWait(natsCondition *cond, natsMutex *mutex,
                                uint64_t absoluteTime);

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
