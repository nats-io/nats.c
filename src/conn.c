// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "statusp.h"
#include "conn.h"
#include "mem.h"
#include "buf.h"
#include "parser.h"
#include "srvpool.h"
#include "url.h"
#include "opts.h"
#include "util.h"
#include "timer.h"
#include "sub.h"
#include "msg.h"
#include "asynccb.h"
#include "comsock.h"

#define DEFAULT_SCRATCH_SIZE    (512)
#define DEFAULT_BUF_SIZE        (32768)
#define DEFAULT_PENDING_SIZE    (1024 * 1024)

#ifdef DEV_MODE
// For type safety

static void _retain(natsConnection *nc)  { nc->refs++; }
//static void _release(natsConnection *nc) { nc->refs--; }

void natsConn_Lock(natsConnection *nc)   { natsMutex_Lock(nc->mu);   }
void natsConn_Unlock(natsConnection *nc) { natsMutex_Unlock(nc->mu); }

#else
// We know what we are doing :-)

#define _retain(c)  ((c)->refs++)
#define _release(c) ((c)->refs--)

#endif // DEV_MODE

/*
 * Forward declarations:
 */
static natsStatus
_spinUpSocketWatchers(natsConnection *nc);

static void
_close(natsConnection *nc, natsConnStatus status, bool doCBs);

natsStatus
natsConnection_Flush(natsConnection *nc);

/*
 * ----------------------------------------
 */

struct threadsToJoin
{
    natsThread  *readLoop;
    natsThread  *flusher;
    natsThread  *reconnect;
    bool        joinReconnect;

} threadsToJoin;

static void
_initThreadsToJoin(struct threadsToJoin *ttj, natsConnection *nc, bool joinReconnect)
{
    memset(ttj, 0, sizeof(threadsToJoin));

    ttj->joinReconnect = joinReconnect;

    if (nc->readLoopThread != NULL)
    {
        ttj->readLoop = nc->readLoopThread;
        nc->readLoopThread = NULL;
    }

    if (joinReconnect && (nc->reconnectThread != NULL))
    {
        ttj->reconnect = nc->reconnectThread;
        nc->reconnectThread = NULL;
    }

    if (nc->flusherThread != NULL)
    {
        nc->flusherStop = true;
        natsCondition_Signal(nc->flusherCond);

        ttj->flusher = nc->flusherThread;
        nc->flusherThread = NULL;
    }
}

static void
_joinThreads(struct threadsToJoin *ttj)
{
    if (ttj->readLoop != NULL)
    {
        natsThread_Join(ttj->readLoop);
        natsThread_Destroy(ttj->readLoop);
    }

    if (ttj->joinReconnect && (ttj->reconnect != NULL))
    {
        natsThread_Join(ttj->reconnect);
        natsThread_Destroy(ttj->reconnect);
    }

    if (ttj->flusher != NULL)
    {
        natsThread_Join(ttj->flusher);
        natsThread_Destroy(ttj->flusher);
    }
}

static void
_clearServerInfo(natsServerInfo *si)
{
    NATS_FREE(si->id);
    NATS_FREE(si->host);
    NATS_FREE(si->version);

    memset(si, 0, sizeof(natsServerInfo));
}

static void
_freeConn(natsConnection *nc)
{
    if (nc == NULL)
        return;

    natsTimer_Destroy(nc->ptmr);
    natsBuf_Destroy(nc->pending);
    natsBuf_Destroy(nc->scratch);
    natsBuf_Destroy(nc->bw);
    natsSrvPool_Destroy(nc->srvPool);
    _clearServerInfo(&(nc->info));
    natsCondition_Destroy(nc->flusherCond);
    natsCondition_Destroy(nc->flushTimeoutCond);
    natsParser_Destroy(nc->ps);
    natsThread_Destroy(nc->readLoopThread);
    natsThread_Destroy(nc->flusherThread);
    natsHash_Destroy(nc->subs);
    natsOptions_Destroy(nc->opts);
    natsSock_DestroyFDSet(nc->fdSet);
    natsMutex_Destroy(nc->mu);

    NATS_FREE(nc);

    natsLib_Release();
}

void
natsConn_retain(natsConnection *nc)
{
    if (nc == NULL)
        return;

    natsConn_Lock(nc);

    nc->refs++;

    natsConn_Unlock(nc);
}

void
natsConn_release(natsConnection *nc)
{
    int refs = 0;

    if (nc == NULL)
        return;

    natsConn_Lock(nc);

    refs = --(nc->refs);

    natsConn_Unlock(nc);

    if (refs == 0)
        _freeConn(nc);
}

void
natsConn_lockAndRetain(natsConnection *nc)
{
    natsConn_Lock(nc);
    nc->refs++;
}

void
natsConn_unlockAndRelease(natsConnection *nc)
{
    int refs = 0;

    refs = --(nc->refs);

    natsConn_Unlock(nc);

    if (refs == 0)
        _freeConn(nc);
}

natsStatus
natsConn_bufferFlush(natsConnection *nc)
{
    natsStatus  s;
    int         n = 0;
    int         bufLen = natsBuf_Len(nc->bw);

    if (bufLen == 0)
        return NATS_OK;

    if (nc->usePending)
    {
        s = natsBuf_Append(nc->pending, natsBuf_Data(nc->bw), bufLen);
        n = bufLen;
    }
    else
    {
        s = natsSock_WriteFully(nc->fdSet, nc->fd, &(nc->deadline),
                                natsBuf_Data(nc->bw), bufLen, &n);
    }

    if (n == bufLen)
    {
        natsBuf_Reset(nc->bw);
    }
    else
    {
        memcpy(natsBuf_Data(nc->bw),
               natsBuf_Data(nc->bw) + n,
               bufLen - n);

        natsBuf_RewindTo(nc->bw, n);
    }

    return s;
}

natsStatus
natsConn_bufferWrite(natsConnection *nc, const char *buffer, int len)
{
    natsStatus  s = NATS_OK;
    int         offset = 0;

    if (len <= 0)
        return NATS_OK;

    if (nc->usePending)
        return natsBuf_Append(nc->pending, buffer, len);

    while ((s == NATS_OK) && (len > natsBuf_Available(nc->bw)))
    {
        if (natsBuf_Len(nc->bw) == 0)
        {
            int n = 0;
            s = natsSock_WriteFully(nc->fdSet, nc->fd, &(nc->deadline),
                                    buffer + offset, len, &n);
            offset += n;
            len -= n;
        }
        else
        {
            int remaining = len - natsBuf_Available(nc->bw);

            s = natsBuf_Append(nc->bw, buffer + offset, remaining);
            if (s == NATS_OK)
                s = natsConn_bufferFlush(nc);
            if (s == NATS_OK)
            {
                len -= remaining;
                offset += remaining;
            }
        }
    }
    if (s == NATS_OK)
        s = natsBuf_Append(nc->bw, buffer + offset, len);

    return s;
}

natsStatus
natsConn_bufferWriteString(natsConnection *nc, const char *string)
{
    return natsConn_bufferWrite(nc, string, strlen(string));
}

// _createConn will connect to the server and do the right thing when an
// existing connection is in place.
static natsStatus
_createConn(natsConnection *nc)
{
    natsStatus  s = NATS_OK;
    natsSrv     *cur = NULL;

    cur = natsSrvPool_GetCurrentServer(nc->srvPool, nc->url, NULL);
    if (cur == NULL)
        return NATS_NO_SERVER;

    cur->lastAttempt = nats_Now();

    // Sets a deadline for the connect process (not just the low level
    // tcp connect. The deadline will be removed when we have received
    // the PONG to our initial PING. See _sendConnect().
    natsDeadline_Init(&(nc->deadline), nc->opts->timeout);

    nc->err = natsSock_ConnectTcp(&(nc->fd), nc->fdSet, &(nc->deadline),
                                  nc->url->host, nc->url->port);
    if (nc->err != NATS_OK)
        return nc->err;

    if ((nc->pending != NULL) && (nc->bw != NULL)
        && (natsBuf_Len(nc->bw) > 0))
    {
        // Move to pending buffer
        s = natsConn_bufferWrite(nc, natsBuf_Data(nc->bw),
                                 natsBuf_Len(nc->bw));
    }

    if (s == NATS_OK)
    {
        nc->usePending = false;

        if (nc->bw == NULL)
            s = natsBuf_Create(&(nc->bw), DEFAULT_BUF_SIZE);
        else
            natsBuf_Reset(nc->bw);
    }

    if (s != NATS_OK)
        nc->err = s;

    return s;
}

static void
_clearControlContent(natsControl *control)
{
    NATS_FREE(control->op);
    NATS_FREE(control->args);
}

static void
_initControlContent(natsControl *control)
{
    control->op     = NULL;
    control->args   = NULL;
}

static bool
_isConnecting(natsConnection *nc)
{
    return nc->status == CONNECTING;
}

bool
natsConn_isClosed(natsConnection *nc)
{
    return nc->status == CLOSED;
}

bool
_isReconnecting(natsConnection *nc)
{
    return ((nc->status == RECONNECTING) || (nc->status == CONNECTING));
}

static natsStatus
_readOp(natsConnection *nc, natsControl *control)
{
    natsStatus  s = NATS_OK;
    char        buffer[DEFAULT_BUF_SIZE];

    if (natsConn_isClosed(nc))
        return NATS_CONNECTION_CLOSED;

    s = natsSock_ReadLine(nc->fdSet, nc->fd, &(nc->deadline),
                          buffer, sizeof(buffer), NULL);
    if (s == NATS_OK)
        s = nats_ParseControl(control, buffer);

    return s;
}

#define TYPE_STR    (0)
#define TYPE_BOOL   (1)
#define TYPE_INT    (2)
#define TYPE_LONG   (3)

static natsStatus
_parseInfo(char **str, const char *field, int fieldType, void **addr)
{
    natsStatus  s    = NATS_OK;
    char        *ptr = NULL;
    char        *end = NULL;
    char        *val = NULL;

    ptr = strcasestr(*str, field);
    if (ptr == NULL)
        return NATS_OK;

    ptr += strlen(field);
    ptr += 2;
    if (fieldType == TYPE_STR)
        ptr += 1;

    if (fieldType == TYPE_STR)
    {
        end = strchr(ptr, '\"');
    }
    else
    {
        end = strchr(ptr, ',');
        if (end == NULL)
            end = strchr(ptr, '}');
    }

    if (end == NULL)
        return NATS_PROTOCOL_ERROR;

    *str = (end + 1);
    *end = '\0';

    if (fieldType == TYPE_STR)
    {
        val = NATS_STRDUP(ptr);
        if (val == NULL)
            return NATS_NO_MEMORY;

        (*(char**)addr) = val;
    }
    else if (fieldType == TYPE_BOOL)
    {
        if (strcasecmp(ptr, "true") == 0)
            (*(bool*)addr) = true;
        else
            (*(bool*)addr) = false;
    }
    else if ((fieldType == TYPE_INT)
             || (fieldType == TYPE_LONG))
    {
        char        *tail = NULL;
        long int    lval = 0;

        errno = 0;

        lval = strtod(ptr, &tail);
        if ((errno != 0) || (tail[0] != '\0'))
            return NATS_PROTOCOL_ERROR;

        if (fieldType == TYPE_INT)
            (*(int*)addr) = (int) lval;
        else
            (*(int64_t*)addr) = (int64_t) lval;
    }
    else
    {
        abort();
    }

    return s;
}

static natsStatus
_processInfo(natsConnection *nc, char *info)
{
    natsStatus  s     = NATS_OK;
    char        *copy = NULL;
    char        *ptr  = NULL;

    if (info == NULL)
        return NATS_OK;

    _clearServerInfo(&(nc->info));

    copy = NATS_STRDUP(info);
    if (copy == NULL)
        return NATS_NO_MEMORY;

    ptr = copy;

    s = _parseInfo(&ptr, "server_id", TYPE_STR, (void**) &(nc->info.id));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "version", TYPE_STR, (void**) &(nc->info.version));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "host", TYPE_STR, (void**) &(nc->info.host));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "port", TYPE_INT, (void**) &(nc->info.port));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "auth_required", TYPE_BOOL, (void**) &(nc->info.authRequired));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "ssl_required", TYPE_BOOL, (void**) &(nc->info.sslRequired));
    if (s == NATS_OK)
        s = _parseInfo(&ptr, "max_payload", TYPE_LONG, (void**) &(nc->info.maxPayload));

#if 0
    fprintf(stderr, "Id=%s Version=%s Host=%s Port=%d Auth=%s SSL=%s Payload=%d\n",
            nc->info.id, nc->info.version, nc->info.host, nc->info.port,
            nats_GetBoolStr(nc->info.authRequired),
            nats_GetBoolStr(nc->info.sslRequired),
            (int) nc->info.maxPayload);
#endif

    NATS_FREE(copy);
    return s;
}

static natsStatus
_processExpectedInfo(natsConnection *nc)
{
    natsControl     control;
    natsStatus      s;

    _initControlContent(&control);

    s = _readOp(nc, &control);
    if (s != NATS_OK)
        return s;

    if ((s == NATS_OK)
        && ((control.op == NULL)
            || (strncmp(control.op, _INFO_OP_, strlen(_INFO_OP_)) != 0)))
    {
        s = NATS_PROTOCOL_ERROR;
    }
    if (s == NATS_OK)
        s = _processInfo(nc, control.args);
    if ((s == NATS_OK) && nc->info.sslRequired)
        s = NATS_SECURE_CONN_REQUIRED;

    _clearControlContent(&control);

    return s;
}

static natsStatus
_connectProto(natsConnection *nc, char **proto)
{
    natsOptions *opts = nc->opts;
    const char  *user = NULL;
    const char  *pwd  = NULL;
    const char  *name = NULL;
    int         res;

    if (nc->url->username != NULL)
        user = nc->url->username;
    if (nc->url->password != NULL)
        pwd = nc->url->password;
    if (opts->name != NULL)
        name = opts->name;

    res = asprintf(proto,
                   "CONNECT {\"verbose\":%s,\"pedantic\":%s,%s%s%s%s%s%s\"ssl_required\":%s," \
                   "\"name\":\"%s\",\"lang\":\"%s\",\"version\":\"%s\"}%s",
                   nats_GetBoolStr(opts->verbose),
                   nats_GetBoolStr(opts->pedantic),
                   (user != NULL ? "\"user\":\"" : ""),
                   (user != NULL ? user : ""),
                   (user != NULL ? "\"," : ""),
                   (pwd != NULL ? "\"pass\":\"" : ""),
                   (pwd != NULL ? pwd : ""),
                   (pwd != NULL ? "\"," : ""),
                   nats_GetBoolStr(false),
                   name, CString, Version, _CRLF_);
    if (res < 0)
        return NATS_NO_MEMORY;

    return NATS_OK;
}

static natsStatus
_sendUnsubProto(natsConnection *nc, natsSubscription *sub)
{
    natsStatus  s       = NATS_OK;
    char        *proto  = NULL;
    int         res     = 0;

    if (sub->max > 0)
        res = asprintf(&proto, _UNSUB_PROTO_, (int) sub->sid, (int) sub->max);
    else
        res = asprintf(&proto, _UNSUB_NO_MAX_PROTO_, (int) sub->sid);

    if (res < 0)
        s = NATS_NO_MEMORY;
    else
    {
        s = natsConn_bufferWriteString(nc, proto);
        NATS_FREE(proto);
    }

    return s;
}

static natsStatus
_resendSubscriptions(natsConnection *nc)
{
    natsStatus          s    = NATS_OK;
    natsSubscription    *sub = NULL;
    natsHashIter        iter;
    char                *proto;
    int                 res;

    natsHashIter_Init(&iter, nc->subs);
    while (natsHashIter_Next(&iter, NULL, (void**) &sub))
    {
        proto = NULL;
        res = asprintf(&proto, _SUB_PROTO_,
                       sub->subject,
                       (sub->queue == NULL ? "" : sub->queue),
                       (int) sub->sid);
        if (res < 0)
            s = NATS_NO_MEMORY;

        if (s == NATS_OK)
        {
            s = natsConn_bufferWriteString(nc, proto);
            NATS_FREE(proto);
            proto = NULL;
        }

        if ((s == NATS_OK) && (sub->max > 0))
            s = _sendUnsubProto(nc, sub);
    }

    return s;
}

static natsStatus
_flushReconnectPendingItems(natsConnection *nc)
{
    natsStatus s = NATS_OK;

    if (nc->pending == NULL)
        return NATS_OK;

    if (natsBuf_Len(nc->pending) > 0)
    {
        s = natsBuf_Append(nc->bw, natsBuf_Data(nc->pending),
                           natsBuf_Len(nc->pending));
    }

    natsBuf_Destroy(nc->pending);
    nc->pending = NULL;

    return s;
}


// Try to reconnect using the option parameters.
// This function assumes we are allowed to reconnect.
static void
_doReconnect(void *arg)
{
    natsStatus              s = NATS_OK;
    natsConnection          *nc = (natsConnection*) arg;
    natsThread              *tReconnect = NULL;
    natsSrv                 *cur;
    int64_t                 elapsed;
    natsSrvPool             *pool;
    struct threadsToJoin    ttj;

    natsConn_Lock(nc);

    _initThreadsToJoin(&ttj, nc, false);

    if (nc->inFlushTimeout)
    {
        nc->flushTimeoutComplete = true;
        natsCondition_Signal(nc->flusherCond);
    }

    natsConn_Unlock(nc);

    _joinThreads(&ttj);

    natsConn_Lock(nc);

    s = natsBuf_Create(&(nc->pending), DEFAULT_PENDING_SIZE);
    if (s == NATS_OK)
    {
        nc->usePending = true;

        // Clear any error.
        nc->err         = NATS_OK;
        nc->errStr[0]   = '\0';

        pool = nc->srvPool;
    }

    // Perform appropriate callback if needed for a disconnect.
    if ((s == NATS_OK) && (nc->opts->disconnectedCb != NULL))
        natsAsyncCb_PostConnHandler(nc, ASYNC_DISCONNECTED);

    // Note that the pool's size may decrement after the call to
    // natsSrvPool_GetNextServer.
    while ((s == NATS_OK) && (natsSrvPool_GetSize(pool) > 0))
    {
        cur = natsSrvPool_GetNextServer(pool, nc->opts, nc->url);
        nc->url = (cur == NULL ? NULL : cur->url);
        if (cur == NULL)
        {
            nc->err = NATS_NO_SERVER;
            break;
        }

        // Sleep appropriate amount of time before the
        // connection attempt if connecting to same server
        // we just got disconnected from..
        if (((elapsed = nats_Now() - cur->lastAttempt)) < nc->opts->reconnectWait)
        {
            int64_t sleepTime = (nc->opts->reconnectWait - elapsed);

            natsConn_Unlock(nc);

            nats_Sleep(sleepTime);

            natsConn_Lock(nc);
        }

        // Check if we have been closed first.
        if (natsConn_isClosed(nc))
            break;

        // Mark that we tried a reconnect
        cur->reconnects += 1;

        // Try to create a new connection
        s = _createConn(nc);
        if (s != NATS_OK)
        {
            // Reset status
            s = NATS_OK;

            // Not yet connected, retry...
            // Continue to hold the lock
            nc->err = s;
            continue;
        }

        // We are reconnected
        nc->stats.reconnects += 1;

        // Clear out server stats for the server we connected to..
        cur->didConnect = true;
        cur->reconnects = 0;

        // Set our status to connecting
        nc->status = CONNECTING;

        // Process Connect logic
        if ((nc->err = _processExpectedInfo(nc)) == NATS_OK)
        {
            char *cProto = NULL;

            // Create the connect protocol message
            s = _connectProto(nc, &cProto);
            if (s != NATS_OK)
            {
                // Reset status
                s = NATS_OK;

                continue;
            }

            // Send the CONNECT protocol
            s = natsConn_bufferWriteString(nc, cProto);

            // Send existing subscription state
            if (s == NATS_OK)
                s = _resendSubscriptions(nc);

            // Now send off and clear pending buffer
            if (s == NATS_OK)
                s = _flushReconnectPendingItems(nc);

            if (s == NATS_OK)
            {
                // This is where we are truly connected.
                nc->status = CONNECTED;

                // Clear our deadline now before starting the readLoop.
                natsDeadline_Clear(&(nc->deadline));
                s = natsSock_SetBlocking(nc->fd, true);
            }

            // Spin up socket watchers again
            if (s == NATS_OK)
                s = _spinUpSocketWatchers(nc);

            NATS_FREE(cProto);
        }

        if ((nc->err != NATS_OK) || (s != NATS_OK))
        {
            // Reset status
            s = NATS_OK;

            nc->status = RECONNECTING;
            continue;
        }

        tReconnect = nc->reconnectThread;
        nc->reconnectThread = NULL;

        // Call reconnectedCB if appropriate. Since we are in a separate
        // thread, we could invoke the callback directly, however, we
        // still post it so all callbacks from a connection are serialized.
        if (nc->opts->reconnectedCb != NULL)
            natsAsyncCb_PostConnHandler(nc, ASYNC_RECONNECTED);

        // Release lock here, we will return below.
        natsConn_Unlock(nc);

        // Make sure to flush everything
        (void) natsConnection_Flush(nc);

        natsThread_Join(tReconnect);
        natsThread_Destroy(tReconnect);

        return;
    }

    // Call into close.. We have no servers left..
    if (nc->err == NATS_OK)
        nc->err = NATS_NO_SERVER;

    natsConn_Unlock(nc);

    _close(nc, CLOSED, true);
}

// Notifies the flusher thread that there is pending data to send to the
// server.
void
natsConn_kickFlusher(natsConnection *nc)
{
    if (!(nc->flusherSignaled) && (nc->bw != NULL))
    {
        nc->flusherSignaled = true;
        natsCondition_Signal(nc->flusherCond);
    }
}

static natsStatus
_sendProto(natsConnection *nc, const char* proto, int protoLen)
{
    natsStatus  s;

    natsConn_Lock(nc);

    s = natsConn_bufferWrite(nc, proto, protoLen);
    if (s == NATS_OK)
        natsConn_kickFlusher(nc);

    natsConn_Unlock(nc);

    return s;
}

static natsStatus
_sendConnect(natsConnection *nc)
{
    natsStatus  s       = NATS_OK;
    char        *cProto = NULL;

    s = _connectProto(nc, &cProto);
    if (s == NATS_OK)
        s = natsConn_bufferWriteString(nc, cProto);
    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, _PING_OP_, _PING_OP_LEN_);
    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, _CRLF_, _CRLF_LEN_);
    if (s == NATS_OK)
        s = natsConn_bufferFlush(nc);
    if (s == NATS_OK)
    {
        char buffer[DEFAULT_BUF_SIZE];

        buffer[0] = '\0';

        s = natsSock_ReadLine(nc->fdSet, nc->fd, &(nc->deadline),
                              buffer, sizeof(buffer), NULL);
        if (s == NATS_OK)
        {
            if (strncmp(buffer, _PONG_OP_, _PONG_OP_LEN_) != 0)
            {
                // The server may have returned an error.
                if (strstr(buffer, "Authorization") != NULL)
                    s = NATS_NOT_PERMITTED;
                else
                    s = NATS_NO_SERVER;
            }
        }
    }

    if (s == NATS_OK)
        nc->status = CONNECTED;

    free(cProto);

    // Clear our deadline.
    natsDeadline_Clear(&(nc->deadline));

    return s;
}

static natsStatus
_processConnInit(natsConnection *nc)
{
    natsStatus s = NATS_OK;

    nc->status = CONNECTING;
    s = _processExpectedInfo(nc);
    if (s == NATS_OK)
        s = _sendConnect(nc);
    if (s == NATS_OK)
        s = natsSock_SetBlocking(nc->fd, true);
    if (s == NATS_OK)
        s = _spinUpSocketWatchers(nc);

    return s;
}

// Main connect function. Will connect to the server
static natsStatus
_connect(natsConnection *nc)
{
    natsStatus  s     = NATS_OK;
    natsSrvPool *pool = NULL;
    int         i;

    natsConn_Lock(nc);

    pool = nc->srvPool;

    // Create actual socket connection
    // For first connect we walk all servers in the pool and try
    // to connect immediately.
    for (i = 0; i < natsSrvPool_GetSize(pool); i++)
    {
        nc->url = natsSrvPool_GetSrvUrl(pool,i);

        s = _createConn(nc);
        if (s == NATS_OK)
        {
            s = _processConnInit(nc);

            if (s == NATS_OK)
            {
                natsSrvPool_SetSrvDidConnect(pool, i, true);
                natsSrvPool_SetSrvDidConnect(pool,i, 0);
                break;
            }
            else
            {
                nc->err = s;

                natsConn_Unlock(nc);

                _close(nc, DISCONNECTED, false);

                natsConn_Lock(nc);

                nc->url = NULL;
            }
        }
        else
        {
            if (s == NATS_IO_ERROR)
                nc->err = NATS_OK;
        }
    }

    if ((nc->err == NATS_OK) && (nc->status != CONNECTED))
    {
        nc->err = NATS_NO_SERVER;
        s = nc->err;
    }

    natsConn_Unlock(nc);

    return s;
}

// _processOpError handles errors from reading or parsing the protocol.
// The lock should not be held entering this function.
static void
_processOpError(natsConnection *nc, natsStatus s)
{
    natsConn_Lock(nc);

    if (_isConnecting(nc) || natsConn_isClosed(nc) || _isReconnecting(nc))
    {
        natsConn_Unlock(nc);

        return;
    }

    if (nc->opts->allowReconnect)
    {
        if (_isReconnecting(nc))
        {
            natsConn_Unlock(nc);

            return;
        }

        nc->status = RECONNECTING;

        if (nc->ptmr != NULL)
            natsTimer_Stop(nc->ptmr);

        if (nc->fd != NATS_SOCK_INVALID)
        {
            natsConn_bufferFlush(nc);

            natsSock_Shutdown(nc->fd);
            nc->fd = NATS_SOCK_INVALID;
        }

        if (natsThread_Create(&(nc->reconnectThread),
                              _doReconnect, (void*) nc) == NATS_OK)
        {
            natsConn_Unlock(nc);

            return;
        }
    }

    // reconnect not allowed or we failed to setup the reconnect code.

    nc->status = DISCONNECTED;
    nc->err = s;

    natsConn_Unlock(nc);

    _close(nc, CLOSED, true);
}

static void
_readLoop(void  *arg)
{
    natsStatus  s = NATS_OK;
    char        buffer[DEFAULT_BUF_SIZE];
    int         fd;
    int         n;

    natsConnection *nc = (natsConnection*) arg;

    natsConn_Lock(nc);

    fd = nc->fd;

    if (nc->ps == NULL)
        s = natsParser_Create(&(nc->ps));

    while ((s == NATS_OK)
           && !natsConn_isClosed(nc)
           && !_isReconnecting(nc))
    {
        natsConn_Unlock(nc);

        n = 0;

        s = natsSock_Read(fd, buffer, sizeof(buffer), &n);
        if (s == NATS_OK)
            s = natsParser_Parse(nc, buffer, n);

        if (s != NATS_OK)
            _processOpError(nc, s);

        natsConn_Lock(nc);
    }

    natsSock_Close(fd);

    natsParser_Destroy(nc->ps);
    nc->ps = NULL;

    // This unlocks and releases the connection to compensate for the retain
    // when this thread was created.
    natsConn_unlockAndRelease(nc);
}

static void
_flusher(void *arg)
{
    natsConnection  *nc  = (natsConnection*) arg;

    while (true)
    {
        natsConn_Lock(nc);

        while (!(nc->flusherSignaled) && !(nc->flusherStop))
            natsCondition_Wait(nc->flusherCond, nc->mu);

        if (nc->flusherStop)
        {
            natsConn_Unlock(nc);
            break;
        }

        //TODO: If we process the request right away, performance
        //      will suffer when sending quickly very small messages.
        //      The buffer is going to be always flushed, which
        //      defeats the purpose of a write buffer.
        //      We need to revisit this.

        // Give a chance to accumulate more requests...
        natsCondition_TimedWait(nc->flusherCond, nc->mu, 1);

        nc->flusherSignaled = false;

        if (natsConn_isClosed(nc) || _isReconnecting(nc))
        {
            natsConn_Unlock(nc);
            break;
        }

        if ((natsBuf_Len(nc->bw) > 0) && (nc->fd != NATS_SOCK_INVALID))
            nc->err = natsConn_bufferFlush(nc);

        natsConn_Unlock(nc);
    }

    // Release the connection to compensate for the retain when this thread
    // was created.
    natsConn_release(nc);
}

static void
_sendPing(natsConnection *nc)
{
    natsStatus  s;

    nc->pingId++;

    s = natsConn_bufferWrite(nc, _PING_PROTO_, _PING_PROTO_LEN_);
    if (s == NATS_OK)
        s = natsConn_bufferFlush(nc);
}

static void
_processPingTimer(natsTimer *timer, void *arg)
{
    natsConnection  *nc = (natsConnection*) arg;

    natsConn_Lock(nc);

    if (nc->status != CONNECTED)
    {
        natsConn_Unlock(nc);
        return;
    }

    if (++(nc->pout) > nc->opts->maxPingsOut)
    {
        natsConn_Unlock(nc);
        _processOpError(nc, NATS_STATE_CONNECTION);
        return;
    }

    _sendPing(nc);

    natsConn_Unlock(nc);
}

static void
_pingStopppedCb(natsTimer *timer, void *closure)
{
    natsConnection *nc = (natsConnection*) closure;

    natsConn_release(nc);
}

static natsStatus
_spinUpSocketWatchers(natsConnection *nc)
{
    natsStatus  s;

    nc->pout                 = 0;
    nc->flusherStop          = false;
    nc->flushTimeoutComplete = false;
    nc->pingId               = 0;
    nc->pongMark             = 0;
    nc->pongId               = 0;

    s = natsThread_Create(&(nc->readLoopThread), _readLoop, (void*) nc);
    if (s == NATS_OK)
    {
        // If the thread above was created ok, we need a retain, since the
        // thread will do a release on exit. It is safe to do the retain
        // after the create because the thread needs a lock held by the
        // caller.
        _retain(nc);

        s = natsThread_Create(&(nc->flusherThread), _flusher, (void*) nc);
        if (s == NATS_OK)
        {
            // See comment above.
            _retain(nc);
        }
    }
    if ((s == NATS_OK) && (nc->opts->pingInterval > 0))
    {
        if (nc->ptmr == NULL)
            s = natsTimer_Create(&(nc->ptmr),
                                 _processPingTimer,
                                 _pingStopppedCb,
                                 nc->opts->pingInterval,
                                 (void*) nc);
        else
            natsTimer_Reset(nc->ptmr, nc->opts->pingInterval);

        if (s == NATS_OK)
            _retain(nc);
    }

    return s;
}

// Remove all subscriptions. This will kick out the delivery threads,
// and unblock NextMsg() calls.
static void
_removeAllSubscriptions(natsConnection *nc)
{
    natsHashIter     iter;
    natsSubscription *sub;

    natsHashIter_Init(&iter, nc->subs);
    while (natsHashIter_Next(&iter, NULL, (void**) &sub))
    {
        (void) natsHashIter_RemoveCurrent(&iter);

        natsSub_Lock(sub);

        sub->closed = true;
        natsCondition_Signal(sub->cond);

        natsSub_Unlock(sub);

        natsSub_release(sub);
    }
}


// Low level close call that will do correct cleanup and set
// desired status. Also controls whether user defined callbacks
// will be triggered. The lock should not be held entering this
// function. This function will handle the locking manually.
static void
_close(natsConnection *nc, natsConnStatus status, bool doCBs)
{
    struct threadsToJoin    ttj;

    natsConn_lockAndRetain(nc);

    if (natsConn_isClosed(nc))
    {
        nc->status = status;

        natsConn_unlockAndRelease(nc);
        return;
    }

    nc->status = CLOSED;

    if (nc->inFlushTimeout)
    {
        nc->flushTimeoutComplete = true;
        natsCondition_Signal(nc->flushTimeoutCond);
    }

    _initThreadsToJoin(&ttj, nc, true);

    if (nc->ptmr != NULL)
        natsTimer_Stop(nc->ptmr);

    // Remove all subscriptions. This will kick out the delivery threads,
    // and unblock NextMsg() calls.
    _removeAllSubscriptions(nc);

    // Go ahead and make sure we have flushed the outbound buffer.
    nc->status = CLOSED;
    if (nc->fd != NATS_SOCK_INVALID)
    {
        natsConn_bufferFlush(nc);
        natsSock_Shutdown(nc->fd);
        nc->fd = NATS_SOCK_INVALID;
    }

    // Perform appropriate callback if needed for a disconnect.
    if (doCBs && (nc->opts->disconnectedCb != NULL))
        natsAsyncCb_PostConnHandler(nc, ASYNC_DISCONNECTED);

    natsConn_Unlock(nc);

    _joinThreads(&ttj);

    natsConn_Lock(nc);

    // Perform appropriate callback if needed for a connection closed.
    if (doCBs && (nc->opts->closedCb != NULL))
        natsAsyncCb_PostConnHandler(nc, ASYNC_CLOSED);

    nc->status = status;

    natsConn_unlockAndRelease(nc);
}

static void
_processSlowConsumer(natsConnection *nc, natsSubscription *sub)
{
    nc->err = NATS_SLOW_CONSUMER;

    if (!(sub->slowConsumer) && (nc->opts->asyncErrCb != NULL))
        natsAsyncCb_PostErrHandler(nc, sub, NATS_SLOW_CONSUMER);

    sub->slowConsumer = true;
}

static natsStatus
_createMsg(natsMsg **newMsg, natsConnection *nc, char *buf, int bufLen)
{
    natsStatus  s        = NATS_OK;
    natsMsg     *msg     = NULL;
    char        *subject = NULL;
    char        *reply   = NULL;

    s = nats_CreateStringFromBuffer(&subject, nc->ps->ma.subject);
    if (s == NATS_OK)
        s = nats_CreateStringFromBuffer(&reply, nc->ps->ma.reply);
    if (s == NATS_OK)
        s = natsMsg_create(&msg, buf, bufLen);

    if (s == NATS_OK)
    {
        msg->subject = subject;
        msg->reply   = reply;

        *newMsg = msg;
    }
    else
    {
        NATS_FREE(subject);
        NATS_FREE(reply);
        natsMsg_Destroy(msg);
    }

    return s;
}

natsStatus
natsConn_processMsg(natsConnection *nc, char *buf, int bufLen)
{
    natsStatus       s    = NATS_OK;
    natsSubscription *sub = NULL;
    natsMsg          *msg = NULL;

    natsConn_Lock(nc);

    nc->stats.inMsgs  += 1;
    nc->stats.inBytes += (uint64_t) bufLen;

    sub = natsHash_Get(nc->subs, nc->ps->ma.sid);
    if (sub == NULL)
    {
        natsConn_Unlock(nc);
        return NATS_OK;
    }

    if ((sub->max > 0) && (sub->msgs > sub->max))
    {
        natsConn_removeSubscription(nc, sub, false);

        natsConn_Unlock(nc);
        return NATS_OK;
    }

    // Updated under the connection lock
    sub->msgs  += 1;
    sub->bytes += (uint64_t) bufLen;

    // Do this outside of sub's lock, even if we end-up having to destroy
    // it because we have reached the maxPendingMsgs count. This reduces
    // lock contention.
    s = _createMsg(&msg, nc, buf, bufLen);
    if (s == NATS_OK)
    {
        natsSub_Lock(sub);

        if (sub->msgList.count >= nc->opts->maxPendingMsgs)
        {
            natsMsg_Destroy(msg);

            _processSlowConsumer(nc, sub);
        }
        else
        {
            sub->slowConsumer = false;

            if (sub->msgList.head == NULL)
                sub->msgList.head = msg;

            if (sub->msgList.tail != NULL)
                sub->msgList.tail->next = msg;

            sub->msgList.tail = msg;

            sub->msgList.count++;

            if (!(sub->signaled))
            {
                sub->signaled = true;
                natsCondition_Signal(sub->cond);
            }
        }

        natsSub_Unlock(sub);
    }

    natsConn_Unlock(nc);

    return s;
}

void
natsConn_processOK(natsConnection *nc)
{
    // Do nothing for now.
}

void
natsConn_processErr(natsConnection *nc, char *buf, int bufLen)
{
    if (strncmp(buf, STALE_CONNECTION, STATE_CONNECTION_LEN) == 0)
    {
        _processOpError(nc, NATS_STATE_CONNECTION);
    }
    else
    {
        natsConn_Lock(nc);
        snprintf(nc->errStr, sizeof(nc->errStr), "%.*s", bufLen, buf);
        natsConn_Unlock(nc);
        _close(nc, CLOSED, true);
    }
}

void
natsConn_processPing(natsConnection *nc)
{
    _sendProto(nc, _PONG_PROTO_, _PONG_PROTO_LEN_);
}

void
natsConn_processPong(natsConnection *nc)
{
    natsConn_Lock(nc);

    nc->pout = 0;

    if (++(nc->pongId) == nc->pongMark)
    {
        nc->flushTimeoutComplete = true;
        natsCondition_Signal(nc->flushTimeoutCond);
    }

    natsConn_Unlock(nc);
}

natsStatus
natsConn_addSubcription(natsConnection *nc, natsSubscription *sub)
{
    natsStatus          s       = NATS_OK;
    natsSubscription    *oldSub = NULL;

    s = natsHash_Set(nc->subs, sub->sid, (void*) sub, (void**) &oldSub);
    if (s == NATS_OK)
    {
        assert(oldSub == NULL);
        natsSub_retain(sub);
    }

    return s;
}

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *removedSub, bool needsLock)
{
    natsSubscription *sub = NULL;

    if (needsLock)
        natsConn_Lock(nc);

    sub = natsHash_Remove(nc->subs, removedSub->sid);

    // Note that the sub may have already been removed, so 'sub == NULL'
    // is not an error.
    if (sub != NULL)
    {
        natsSub_Lock(sub);

        // Kick out the deliverMsgs thread.
        sub->closed = true;
        natsCondition_Signal(sub->cond);

        natsSub_Unlock(sub);
    }

    if (needsLock)
        natsConn_Unlock(nc);

    // If we really removed the subscription, then release it.
    if (sub != NULL)
        natsSub_release(sub);
}

// subscribe is the internal subscribe function that indicates interest in a
// subject.
natsStatus
natsConn_subscribe(natsSubscription **newSub,
                   natsConnection *nc, const char *subj, const char *queue,
                   natsMsgHandler cb, void *cbClosure)
{
    natsStatus          s    = NATS_OK;
    natsSubscription    *sub = NULL;

    if (nc == NULL)
        return NATS_INVALID_ARG;

    if ((subj == NULL) || (strlen(subj) == 0))
        return NATS_INVALID_SUBJECT;

    natsConn_Lock(nc);

    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);

        return NATS_CONNECTION_CLOSED;
    }

    s = natsSub_create(&sub, nc, subj, queue, cb, cbClosure);
    if (s == NATS_OK)
    {
        sub->sid = ++(nc->ssid);
        s = natsConn_addSubcription(nc, sub);
    }

    if (s == NATS_OK)
    {
        // We will send these for all subs when we reconnect
        // so that we can suppress here.
        if (!_isReconnecting(nc))
        {
            char    *proto = NULL;
            int     res    = 0;

            res = asprintf(&proto, _SUB_PROTO_,
                           subj,
                           (queue == NULL ? "" : queue),
                           (int) sub->sid);
            if (res < 0)
                s = NATS_NO_MEMORY;

            if (s == NATS_OK)
                s = natsConn_bufferWriteString(nc, proto);
            if (s == NATS_OK)
                natsConn_kickFlusher(nc);

            NATS_FREE(proto);
        }
    }

    if (s == NATS_OK)
    {
        *newSub = sub;
    }
    else if (sub != NULL)
    {
        // A delivery thread may have been started, but the subscription not
        // added to the connection's subscription map. So this is necessary
        // for the delivery thread to unroll.
        sub->closed = true;
        natsCondition_Signal(sub->cond);

        natsConn_removeSubscription(nc, sub, false);

        natsSub_release(sub);
    }

    natsConn_Unlock(nc);

    return s;
}

// Performs the low level unsubscribe to the server.
natsStatus
natsConn_unsubscribe(natsConnection *nc, natsSubscription *sub, int max)
{
    natsStatus      s = NATS_OK;

    natsConn_Lock(nc);

    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);
        return NATS_CONNECTION_CLOSED;
    }

    sub = natsHash_Get(nc->subs, sub->sid);
    if (sub == NULL)
    {
        // Already unsubscribed
        natsConn_Unlock(nc);
        return NATS_OK;
    }

    if (max > 0)
    {
        sub->max = max;
    }
    else
    {
        sub->max = 0;
        natsConn_removeSubscription(nc, sub, false);
    }

    if (!_isReconnecting(nc))
    {
        // We will send these for all subs when we reconnect
        // so that we can suppress here.
        s = _sendUnsubProto(nc, sub);
        if (s == NATS_OK)
            natsConn_kickFlusher(nc);
    }

    natsConn_Unlock(nc);

    return s;
}

static natsStatus
_setupServerPool(natsConnection *nc)
{
    natsStatus  s;

    s = natsSrvPool_Create(&(nc->srvPool), nc->opts);
    if (s == NATS_OK)
        nc->url = natsSrvPool_GetSrvUrl(nc->srvPool, 0);

    return s;
}

natsStatus
natsConn_create(natsConnection **newConn, natsOptions *options)
{
    natsStatus      s   = NATS_OK;
    natsConnection  *nc = NULL;

    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    nc = NATS_CALLOC(1, sizeof(natsConnection));
    if (nc == NULL)
    {
        // options have been cloned or created for the connection,
        // which was supposed to take ownership, so destroy it now.
        natsOptions_Destroy(options);
        return NATS_NO_MEMORY;
    }

    natsLib_Retain();

    nc->refs = 1;
    nc->fd   = NATS_SOCK_INVALID;
    nc->opts = options;

    if (nc->opts->maxPingsOut == 0)
        nc->opts->maxPingsOut = NATS_OPTS_DEFAULT_MAX_PING_OUT;

    if (nc->opts->maxPendingMsgs == 0)
        nc->opts->maxPendingMsgs = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;

    nc->errStr[0] = '\0';

    s = natsMutex_Create(&(nc->mu));
    if (s == NATS_OK)
        s = _setupServerPool(nc);
    if (s == NATS_OK)
        s = natsHash_Create(&(nc->subs), 8);
    if (s == NATS_OK)
        s = natsSock_CreateFDSet(&(nc->fdSet));
        if (s == NATS_OK)
    {
        s = natsBuf_Create(&(nc->scratch), DEFAULT_SCRATCH_SIZE);
        if (s == NATS_OK)
            s = natsBuf_Append(nc->scratch, _PUB_P_, _PUB_P_LEN_);
    }
    if (s == NATS_OK)
        s = natsCondition_Create(&(nc->flusherCond));
    if (s == NATS_OK)
        s = natsCondition_Create(&(nc->flushTimeoutCond));

    if (s == NATS_OK)
        *newConn = nc;
    else
        _freeConn(nc);

    return s;
}

natsStatus
natsConnection_Connect(natsConnection **newConn, natsOptions *options)
{
    natsStatus      s;
    natsConnection  *nc     = NULL;
    natsOptions     *opts   = NULL;

    opts = natsOptions_clone(options);
    if (opts == NULL)
        return NATS_NO_MEMORY;

    s = natsConn_create(&nc, opts);
    if (s == NATS_OK)
        s = _connect(nc);

    if (s == NATS_OK)
        *newConn = nc;
    else
        _freeConn(nc);

    return s;
}

natsStatus
natsConnection_ConnectTo(natsConnection **newConn, const char *url)
{
    natsStatus      s       = NATS_OK;
    natsConnection  *nc     = NULL;
    natsOptions     *opts   = NULL;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, url);
    if (s == NATS_OK)
        s = natsConn_create(&nc, opts);
    if (s == NATS_OK)
        s = _connect(nc);

    if (s == NATS_OK)
        *newConn = nc;
    else
        _freeConn(nc);

    return s;
}

// Test if connection  has been closed.
bool
natsConnection_IsClosed(natsConnection *nc)
{
    bool closed;

    if (nc == NULL)
        return true;

    natsConn_Lock(nc);

    closed = natsConn_isClosed(nc);

    natsConn_Unlock(nc);

    return closed;
}

// Test if connection is reconnecting.
bool
natsConnection_IsReconnecting(natsConnection *nc)
{
    bool reconnecting;

    if (nc == NULL)
        return false;

    natsConn_Lock(nc);

    reconnecting = _isReconnecting(nc);

    natsConn_Unlock(nc);

    return reconnecting;
}

// Returns the current state of the connection.
natsConnStatus
natsConnection_Status(natsConnection *nc)
{
    natsConnStatus cs;

    if (nc == NULL)
        return CLOSED;

    natsConn_Lock(nc);

    cs = nc->status;

    natsConn_Unlock(nc);

    return cs;
}

natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout)
{
    natsStatus  s = NATS_OK;
    int64_t     target = 0;

    if (nc == NULL)
        return NATS_INVALID_ARG;

    if (timeout <= 0)
        return NATS_INVALID_TIMEOUT;

    natsConn_lockAndRetain(nc);

    if (natsConn_isClosed(nc))
        s = NATS_CONNECTION_CLOSED;

    if ((s == NATS_OK) && (nc->inFlushTimeout))
        s = NATS_NOT_PERMITTED;

    if (s == NATS_OK)
    {
        nc->inFlushTimeout = true;

        nc->pongMark = (nc->pingId + 1);
        _sendPing(nc);

        target = nats_Now() + timeout;

        while ((s != NATS_TIMEOUT)
               && !natsConn_isClosed(nc)
               && !(nc->flushTimeoutComplete))
        {
            s = natsCondition_AbsoluteTimedWait(nc->flushTimeoutCond, nc->mu, target);
        }

        // Reset those
        nc->flushTimeoutComplete = false;
        nc->pongMark             = 0;
        nc->inFlushTimeout       = false;

        if ((s == NATS_OK) && (nc->status != CONNECTED))
            s = NATS_CONNECTION_CLOSED;
        else if (nc->err != NATS_OK)
            s = nc->err;
    }

    natsConn_unlockAndRelease(nc);

    return s;
}

natsStatus
natsConnection_Flush(natsConnection *nc)
{
    return natsConnection_FlushTimeout(nc, 60000);
}

int
natsConnection_Buffered(natsConnection *nc)
{
    int buffered = -1;

    if (nc == NULL)
        return NATS_INVALID_ARG;

    natsConn_Lock(nc);

    if ((nc->status != CLOSED) && (nc->bw != NULL))
        buffered = natsBuf_Len(nc->bw);

    natsConn_Unlock(nc);

    return buffered;
}

int64_t
natsConnection_GetMaxPayload(natsConnection *nc)
{
    int64_t mp = 0;

    if (nc == NULL)
        return 0;

    natsConn_Lock(nc);

    mp = nc->info.maxPayload;

    natsConn_Unlock(nc);

    return mp;
}

natsStatus
natsConnection_GetStats(natsConnection *nc, natsStatistics *stats)
{
    natsStatus  s = NATS_OK;

    if ((nc == NULL) || (stats == NULL))
        return NATS_INVALID_ARG;

    natsConn_Lock(nc);

    memcpy(stats, &(nc->stats), sizeof(natsStatistics));

    natsConn_Unlock(nc);

    return s;
}

natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize)
{
    natsStatus  s = NATS_OK;

    if ((nc == NULL) || (buffer == NULL))
        return NATS_INVALID_ARG;

    natsConn_Lock(nc);

    buffer[0] = '\0';

    if ((nc->status == CONNECTED) && (nc->url->fullUrl != NULL))
    {
        if (strlen(nc->url->fullUrl) >= bufferSize)
            s = NATS_INSUFFICIENT_BUFFER;

        if (s == NATS_OK)
            snprintf(buffer, bufferSize, "%s", nc->url->fullUrl);
    }

    natsConn_Unlock(nc);

    return s;
}

natsStatus
natsConnection_GetConnectedServerId(natsConnection *nc, char *buffer, size_t bufferSize)
{
    natsStatus  s = NATS_OK;

    if ((nc == NULL) || (buffer == NULL))
        return NATS_INVALID_ARG;

    natsConn_Lock(nc);

    buffer[0] = '\0';

    if ((nc->status == CONNECTED) && (nc->info.id != NULL))
    {
        if (strlen(nc->info.id) >= bufferSize)
            s = NATS_INSUFFICIENT_BUFFER;

        if (s == NATS_OK)
            snprintf(buffer, bufferSize, "%s", nc->info.id);
    }

    natsConn_Unlock(nc);

    return s;
}

natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError)
{
    natsStatus  s;

    if (nc == NULL)
        return NATS_INVALID_ARG;

    natsConn_Lock(nc);

    s = nc->err;
    if (s == NATS_OK)
        nc->errStr[0] = '\0';
    else if (nc->errStr[0] == '\0')
        snprintf(nc->errStr, sizeof(nc->errStr), "%s", natsStatus_GetText(s));

    *lastError = nc->errStr;

    natsConn_Unlock(nc);

    return s;
}

void
natsConnection_Close(natsConnection *nc)
{
    if (nc == NULL)
        return;

    _close(nc, CLOSED, true);
}

void
natsConnection_Destroy(natsConnection *nc)
{
    if (nc == NULL)
        return;

    _close(nc, CLOSED, true);
    natsConn_release(nc);
}

