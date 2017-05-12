// Copyright 2015-2017 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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

#define NATS_EVENT_ACTION_ADD       (true)
#define NATS_EVENT_ACTION_REMOVE    (false)

#ifdef DEV_MODE
// For type safety

static void _retain(natsConnection *nc)  { nc->refs++; }
static void _release(natsConnection *nc) { nc->refs--; }

void natsConn_Lock(natsConnection *nc)   { natsMutex_Lock(nc->mu);   }
void natsConn_Unlock(natsConnection *nc) { natsMutex_Unlock(nc->mu); }

#else
// We know what we are doing :-)

#define _retain(c)  ((c)->refs++)
#define _release(c) ((c)->refs--)

#endif // DEV_MODE


// CLIENT_PROTO_ZERO is the original client protocol from 2009.
// http://nats.io/documentation/internals/nats-protocol/
#define CLIENT_PROTO_ZERO   (0)

// CLIENT_PROTO_INFO signals a client can receive more then the original INFO block.
// This can be used to update clients on other cluster members, etc.
#define CLIENT_PROTO_INFO   (1)

/*
 * Forward declarations:
 */
static natsStatus
_spinUpSocketWatchers(natsConnection *nc);

static natsStatus
_processConnInit(natsConnection *nc);

static void
_close(natsConnection *nc, natsConnStatus status, bool doCBs);

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
    int i;

    NATS_FREE(si->id);
    NATS_FREE(si->host);
    NATS_FREE(si->version);

    for (i=0; i<si->connectURLsCount; i++)
        NATS_FREE(si->connectURLs[i]);
    NATS_FREE(si->connectURLs);

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
    natsCondition_Destroy(nc->pongs.cond);
    natsParser_Destroy(nc->ps);
    natsThread_Destroy(nc->readLoopThread);
    natsThread_Destroy(nc->flusherThread);
    natsHash_Destroy(nc->subs);
    natsOptions_Destroy(nc->opts);
    natsSock_Clear(&nc->sockCtx);
    if (nc->sockCtx.ssl != NULL)
        SSL_free(nc->sockCtx.ssl);
    NATS_FREE(nc->el.buffer);
    natsMutex_Destroy(nc->subsMu);
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
    natsStatus  s      = NATS_OK;
    int         bufLen = natsBuf_Len(nc->bw);

    if (bufLen == 0)
        return NATS_OK;

    if (nc->usePending)
    {
        s = natsBuf_Append(nc->pending, natsBuf_Data(nc->bw), bufLen);
    }
    else if (nc->sockCtx.useEventLoop)
    {
        if (!(nc->el.writeAdded))
        {
            nc->el.writeAdded = true;
            s = nc->opts->evCbs.write(nc->el.data, NATS_EVENT_ACTION_ADD);
            if (s != NATS_OK)
                nats_setError(s, "Error processing write request: %d - %s",
                              s, natsStatus_GetText(s));
        }

        return NATS_UPDATE_ERR_STACK(s);
    }
    else
    {
        s = natsSock_WriteFully(&(nc->sockCtx), natsBuf_Data(nc->bw), bufLen);
    }

    if (s == NATS_OK)
        natsBuf_Reset(nc->bw);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConn_bufferWrite(natsConnection *nc, const char *buffer, int len)
{
    natsStatus  s = NATS_OK;
    int         offset = 0;
    int         avail  = 0;

    if (len <= 0)
        return NATS_OK;

    if (nc->usePending)
        return natsBuf_Append(nc->pending, buffer, len);

    if (nc->sockCtx.useEventLoop)
    {
        s = natsBuf_Append(nc->bw, buffer, len);
        if ((s == NATS_OK)
            && (natsBuf_Len(nc->bw) >= DEFAULT_BUF_SIZE)
            && !(nc->el.writeAdded))
        {
            nc->el.writeAdded = true;
            s = nc->opts->evCbs.write(nc->el.data, NATS_EVENT_ACTION_ADD);
            if (s != NATS_OK)
                nats_setError(s, "Error processing write request: %d - %s",
                              s, natsStatus_GetText(s));
        }

        return NATS_UPDATE_ERR_STACK(s);
    }

    // If we have more data that can fit..
    while ((s == NATS_OK) && (len > natsBuf_Available(nc->bw)))
    {
        // If there is nothing in the buffer...
        if (natsBuf_Len(nc->bw) == 0)
        {
            // Do a single socket write to avoid a copy
            s = natsSock_WriteFully(&(nc->sockCtx), buffer + offset, len);

            // We are done
            return NATS_UPDATE_ERR_STACK(s);
        }

        // We already have data in the buffer, check how many more bytes
        // can we fit
        avail = natsBuf_Available(nc->bw);

        // Append that much bytes
        s = natsBuf_Append(nc->bw, buffer + offset, avail);

        // Flush the buffer
        if (s == NATS_OK)
            s = natsConn_bufferFlush(nc);

        // If success, then decrement what's left to send and update the
        // offset.
        if (s == NATS_OK)
        {
            len    -= avail;
            offset += avail;
        }
    }

    // If there is data left, the buffer can now hold this data.
    if ((s == NATS_OK) && (len > 0))
        s = natsBuf_Append(nc->bw, buffer + offset, len);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConn_bufferWriteString(natsConnection *nc, const char *string)
{
    natsStatus s = natsConn_bufferWrite(nc, string, (int) strlen(string));

    return NATS_UPDATE_ERR_STACK(s);
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
        return nats_setDefaultError(NATS_NO_SERVER);

    cur->lastAttempt = nats_Now();

    // Sets a deadline for the connect process (not just the low level
    // tcp connect. The deadline will be removed when we have received
    // the PONG to our initial PING. See _processConnInit().
    natsDeadline_Init(&(nc->sockCtx.deadline), nc->opts->timeout);

    // Set the IP resolution order
    nc->sockCtx.orderIP = nc->opts->orderIP;

    s = natsSock_ConnectTcp(&(nc->sockCtx), nc->url->host, nc->url->port);
    if (s == NATS_OK)
    {
        nc->sockCtx.fdActive = true;

        if ((nc->pending != NULL) && (nc->bw != NULL)
            && (natsBuf_Len(nc->bw) > 0))
        {
            // Move to pending buffer
            s = natsConn_bufferWrite(nc, natsBuf_Data(nc->bw),
                                     natsBuf_Len(nc->bw));
        }
    }

    if (s == NATS_OK)
    {
        if (nc->bw == NULL)
            s = natsBuf_Create(&(nc->bw), DEFAULT_BUF_SIZE);
        else
            natsBuf_Reset(nc->bw);
    }

    if (s != NATS_OK)
    {
        // reset the deadline
        natsDeadline_Clear(&(nc->sockCtx.deadline));
    }

    return NATS_UPDATE_ERR_STACK(s);
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
natsConn_isReconnecting(natsConnection *nc)
{
    return (nc->status == RECONNECTING);
}

static natsStatus
_readOp(natsConnection *nc, natsControl *control)
{
    natsStatus  s = NATS_OK;
    char        buffer[DEFAULT_BUF_SIZE];

    buffer[0] = '\0';

    s = natsSock_ReadLine(&(nc->sockCtx), buffer, sizeof(buffer));
    if (s == NATS_OK)
        s = nats_ParseControl(control, buffer);

    return NATS_UPDATE_ERR_STACK(s);
}

// _processInfo is used to parse the info messages sent
// from the server.
// This function may update the server pool.
static natsStatus
_processInfo(natsConnection *nc, char *info, int len)
{
    natsStatus  s     = NATS_OK;
    nats_JSON   *json = NULL;

    if (info == NULL)
        return NATS_OK;

    _clearServerInfo(&(nc->info));

    s = nats_JSONParse(&json, info, len);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "server_id", TYPE_STR,
                              (void**) &(nc->info.id));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "version", TYPE_STR,
                              (void**) &(nc->info.version));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "host", TYPE_STR,
                              (void**) &(nc->info.host));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "port", TYPE_INT,
                              (void**) &(nc->info.port));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "auth_required", TYPE_BOOL,
                              (void**) &(nc->info.authRequired));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "tls_required", TYPE_BOOL,
                              (void**) &(nc->info.tlsRequired));
    if (s == NATS_OK)
        s = nats_JSONGetValue(json, "max_payload", TYPE_LONG,
                             (void**) &(nc->info.maxPayload));
    if (s == NATS_OK)
        s = nats_JSONGetArrayValue(json, "connect_urls", TYPE_STR,
                                   (void***) &(nc->info.connectURLs),
                                   &(nc->info.connectURLsCount));

#if 0
    fprintf(stderr, "Id=%s Version=%s Host=%s Port=%d Auth=%s SSL=%s Payload=%d\n",
            nc->info.id, nc->info.version, nc->info.host, nc->info.port,
            nats_GetBoolStr(nc->info.authRequired),
            nats_GetBoolStr(nc->info.tlsRequired),
            (int) nc->info.maxPayload);
#endif

    if (s == NATS_OK)
    {
        bool added = false;

        s = natsSrvPool_addNewURLs(nc->srvPool,
                                   nc->info.connectURLs,
                                   nc->info.connectURLsCount,
                                   !nc->opts->noRandomize,
                                   &added);
        if ((s == NATS_OK) && added && !nc->initc && (nc->opts->discoveredServersCb != NULL))
            natsAsyncCb_PostConnHandler(nc, ASYNC_DISCOVERED_SERVERS);
    }

    if (s != NATS_OK)
        s = nats_setError(NATS_PROTOCOL_ERROR,
                          "Invalid protocol: %s", nats_GetLastError(NULL));

    nats_JSONDestroy(json);

    return NATS_UPDATE_ERR_STACK(s);
}

// natsConn_processAsyncINFO does the same than processInfo, but is called
// from the parser. Calls processInfo under connection's lock
// protection.
void
natsConn_processAsyncINFO(natsConnection *nc, char *buf, int len)
{
    natsConn_Lock(nc);
    // Ignore errors, we will simply not update the server pool...
    (void) _processInfo(nc, buf, len);
    natsConn_Unlock(nc);
}

// makeTLSConn will wrap an existing Conn using TLS
static natsStatus
_makeTLSConn(natsConnection *nc)
{
#if defined(NATS_HAS_TLS)
    natsStatus  s       = NATS_OK;
    SSL         *ssl    = NULL;

    // Reset nc->errStr before initiating the handshake...
    nc->errStr[0] = '\0';

    natsMutex_Lock(nc->opts->sslCtx->lock);

    s = natsSock_SetBlocking(nc->sockCtx.fd, true);
    if (s == NATS_OK)
    {
        ssl = SSL_new(nc->opts->sslCtx->ctx);
        if (ssl == NULL)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error creating SSL object: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
        else
        {
            nats_sslRegisterThreadForCleanup();

            SSL_set_ex_data(ssl, 0, (void*) nc);
        }
    }
    if (s == NATS_OK)
    {
        SSL_set_connect_state(ssl);

        if (SSL_set_fd(ssl, (int) nc->sockCtx.fd) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error connecting the SSL object to a file descriptor : %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
    }
    if (s == NATS_OK)
    {
        if (SSL_do_handshake(ssl) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "SSL handshake error: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
    }
    if ((s == NATS_OK) && !nc->opts->sslCtx->skipVerify)
    {
        X509 *cert = SSL_get_peer_certificate(ssl);

        if (cert != NULL)
        {
            if ((SSL_get_verify_result(ssl) != X509_V_OK)
                || (nc->errStr[0] != '\0'))
            {
                s = nats_setError(NATS_SSL_ERROR,
                                  "Server certificate verification failed: %s",
                                  nc->errStr);
            }
            X509_free(cert);
        }
        else
        {
            s = nats_setError(NATS_SSL_ERROR, "%s",
                              "Server did not provide a certificate");
        }
    }

    if (s == NATS_OK)
        s = natsSock_SetBlocking(nc->sockCtx.fd, false);

    natsMutex_Unlock(nc->opts->sslCtx->lock);

    if (s != NATS_OK)
    {
        if (ssl != NULL)
            SSL_free(ssl);
    }
    else
    {
        nc->sockCtx.ssl = ssl;
    }

    return NATS_UPDATE_ERR_STACK(s);
#else
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
#endif
}

// This will check to see if the connection should be
// secure. This can be dictated from either end and should
// only be called after the INIT protocol has been received.
static natsStatus
_checkForSecure(natsConnection *nc)
{
    natsStatus  s = NATS_OK;

    // Check for mismatch in setups
    if (nc->opts->secure && !nc->info.tlsRequired)
        s = nats_setDefaultError(NATS_SECURE_CONNECTION_WANTED);
    else if (nc->info.tlsRequired && !nc->opts->secure)
        s = nats_setDefaultError(NATS_SECURE_CONNECTION_REQUIRED);

    if ((s == NATS_OK) && nc->opts->secure)
        s = _makeTLSConn(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_processExpectedInfo(natsConnection *nc)
{
    natsControl     control;
    natsStatus      s;

    _initControlContent(&control);

    s = _readOp(nc, &control);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if ((s == NATS_OK)
        && ((control.op == NULL)
            || (strcmp(control.op, _INFO_OP_) != 0)))
    {
        s = nats_setError(NATS_PROTOCOL_ERROR,
                          "Unexpected protocol: got '%s' instead of '%s'",
                          (control.op == NULL ? "<null>" : control.op),
                          _INFO_OP_);
    }
    if (s == NATS_OK)
        s = _processInfo(nc, control.args, -1);
    if (s == NATS_OK)
        s = _checkForSecure(nc);

    _clearControlContent(&control);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_connectProto(natsConnection *nc, char **proto)
{
    natsOptions *opts = nc->opts;
    const char  *token= NULL;
    const char  *user = NULL;
    const char  *pwd  = NULL;
    const char  *name = NULL;
    int         res;

    if (nc->url->username != NULL)
        user = nc->url->username;
    if (nc->url->password != NULL)
        pwd = nc->url->password;
    if ((user != NULL) && (pwd == NULL))
    {
        token = user;
        user  = NULL;
    }
    if ((user == NULL) && (token == NULL))
    {
        // Take from options (possibly all NULL)
        user  = nc->opts->user;
        pwd   = nc->opts->password;
        token = nc->opts->token;
    }
    if (opts->name != NULL)
        name = opts->name;

    res = nats_asprintf(proto,
                        "CONNECT {\"verbose\":%s,\"pedantic\":%s,%s%s%s%s%s%s%s%s%s\"tls_required\":%s," \
                        "\"name\":\"%s\",\"lang\":\"%s\",\"version\":\"%s\",\"protocol\":%d}%s",
                        nats_GetBoolStr(opts->verbose),
                        nats_GetBoolStr(opts->pedantic),
                        (user != NULL ? "\"user\":\"" : ""),
                        (user != NULL ? user : ""),
                        (user != NULL ? "\"," : ""),
                        (pwd != NULL ? "\"pass\":\"" : ""),
                        (pwd != NULL ? pwd : ""),
                        (pwd != NULL ? "\"," : ""),
                        (token != NULL ? "\"auth_token\":\"" :""),
                        (token != NULL ? token : ""),
                        (token != NULL ? "\"," : ""),
                        nats_GetBoolStr(opts->secure),
                        (name != NULL ? name : ""),
                        CString, NATS_VERSION_STRING,
                        CLIENT_PROTO_INFO,
                        _CRLF_);
    if (res < 0)
        return NATS_NO_MEMORY;

    return NATS_OK;
}

static natsStatus
_sendUnsubProto(natsConnection *nc, int64_t subId, int max)
{
    natsStatus  s       = NATS_OK;
    char        *proto  = NULL;
    int         res     = 0;

    if (max > 0)
        res = nats_asprintf(&proto, _UNSUB_PROTO_, subId, max);
    else
        res = nats_asprintf(&proto, _UNSUB_NO_MAX_PROTO_, subId);

    if (res < 0)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
    {
        s = natsConn_bufferWriteString(nc, proto);
        NATS_FREE(proto);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_resendSubscriptions(natsConnection *nc)
{
    natsStatus          s    = NATS_OK;
    natsSubscription    *sub = NULL;
    natsHashIter        iter;
    char                *proto;
    int                 res;
    int                 adjustedMax;
    natsSubscription    **subs = NULL;
    int                 i = 0;
    int                 count = 0;

    // Since we are going to send protocols to the server, we don't want to
    // be holding the subsMu lock (which is used in processMsg). So copy
    // the subscriptions in a temporary array.
    natsMutex_Lock(nc->subsMu);
    if (natsHash_Count(nc->subs) > 0)
    {
        subs = NATS_CALLOC(natsHash_Count(nc->subs), sizeof(natsSubscription*));
        if (subs == NULL)
            s = NATS_NO_MEMORY;

        if (s == NATS_OK)
        {
            natsHashIter_Init(&iter, nc->subs);
            while (natsHashIter_Next(&iter, NULL, (void**) &sub))
            {
                subs[count++] = sub;
            }
        }
    }
    natsMutex_Unlock(nc->subsMu);

    for (i=0; (s == NATS_OK) && (i<count); i++)
    {
        sub = subs[i];

        proto = NULL;

        adjustedMax = 0;
        natsSub_Lock(sub);
        if (sub->max > 0)
        {
            if (sub->delivered < sub->max)
                adjustedMax = (int)(sub->max - sub->delivered);

            // The adjusted max could be 0 here if the number of delivered
            // messages have reached the max, if so, unsubscribe.
            if (adjustedMax == 0)
            {
                natsSub_Unlock(sub);
                s = _sendUnsubProto(nc, sub->sid, 0);
                continue;
            }
        }
        natsSub_Unlock(sub);

        // These sub's fields are immutable
        res = nats_asprintf(&proto, _SUB_PROTO_,
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

        if ((s == NATS_OK) && (adjustedMax > 0))
            s = _sendUnsubProto(nc, sub->sid, adjustedMax);
    }

    NATS_FREE(subs);

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

    return s;
}

static void
_removePongFromList(natsConnection *nc, natsPong *pong)
{
    if (pong->prev != NULL)
        pong->prev->next = pong->next;

    if (pong->next != NULL)
        pong->next->prev = pong->prev;

    if (nc->pongs.head == pong)
        nc->pongs.head = pong->next;

    if (nc->pongs.tail == pong)
        nc->pongs.tail = pong->prev;

    pong->prev = pong->next = NULL;
}

// When the connection is closed, or is disconnected and we are about
// to reconnect, we need to unblock all pending natsConnection_Flush[Timeout]()
// calls: there is no chance that a PING sent to a server is going to be
// echoed by the new server.
static void
_clearPendingFlushRequests(natsConnection *nc)
{
    natsPong *pong = NULL;

    while ((pong = nc->pongs.head) != NULL)
    {
        // Pop from the queue
        _removePongFromList(nc, pong);

        // natsConnection_Flush[Timeout]() is waiting on a condition
        // variable and exit when this value is != 0. "Flush" will
        // return an error to the caller if the connection status
        // is not CONNECTED at that time.
        pong->id = -1;

        // There may be more than one user-thread making
        // natsConnection_Flush() calls.
        natsCondition_Broadcast(nc->pongs.cond);
    }

    nc->pongs.incoming      = 0;
    nc->pongs.outgoingPings = 0;
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
    natsSrvPool             *pool = NULL;
    int64_t                 sleepTime;
    struct threadsToJoin    ttj;

    natsConn_Lock(nc);

    _initThreadsToJoin(&ttj, nc, false);

    natsConn_Unlock(nc);

    _joinThreads(&ttj);

    natsConn_Lock(nc);

    // Kick out all calls to natsConnection_Flush[Timeout]().
    _clearPendingFlushRequests(nc);

    // Clear any error.
    nc->err         = NATS_OK;
    nc->errStr[0]   = '\0';

    pool = nc->srvPool;

    // Perform appropriate callback if needed for a disconnect.
    if (nc->opts->disconnectedCb != NULL)
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

        sleepTime = 0;

        // Sleep appropriate amount of time before the
        // connection attempt if connecting to same server
        // we just got disconnected from..
        if (((elapsed = nats_Now() - cur->lastAttempt)) < nc->opts->reconnectWait)
            sleepTime = (nc->opts->reconnectWait - elapsed);

        natsConn_Unlock(nc);

        if (sleepTime > 0)
            nats_Sleep(sleepTime);
        else
            natsThread_Yield();

        natsConn_Lock(nc);

        // Check if we have been closed first.
        if (natsConn_isClosed(nc))
            break;

        // Mark that we tried a reconnect
        cur->reconnects += 1;

        // Try to create a new connection
        s = _createConn(nc);
        if (s != NATS_OK)
        {
            // Reset error here. We will return NATS_NO_SERVERS at the end of
            // this loop if appropriate.
            nc->err = NATS_OK;

            // Reset status
            s = NATS_OK;

            // Not yet connected, retry...
            // Continue to hold the lock
            continue;
        }

        // We have a valid FD and the writer buffer was moved to pending.
        // We are now going to send data directly to the newly connected
        // server, so we need to disable the use of 'pending' for the
        // moment
        nc->usePending = false;

        // We are reconnected
        nc->stats.reconnects += 1;

        // Process Connect logic
        s = _processConnInit(nc);

        // Send existing subscription state
        if (s == NATS_OK)
            s = _resendSubscriptions(nc);

        // Now send off and clear pending buffer
        if (s == NATS_OK)
            s = _flushReconnectPendingItems(nc);

        // This is where we are truly connected.
        if (s == NATS_OK)
            nc->status = CONNECTED;

        if (s != NATS_OK)
        {
            // In case we were at the last iteration, this is the error
            // we will report.
            nc->err = s;

            // Reset status
            s = NATS_OK;

            // Close the socket since we were connected, but a problem occurred.
            // (not doing this would cause an FD leak)
            natsSock_Close(nc->sockCtx.fd);
            nc->sockCtx.fd = NATS_SOCK_INVALID;

            // We need to re-activate the use of pending since we
            // may go back to sleep and release the lock
            nc->usePending = true;
            natsBuf_Reset(nc->bw);

            nc->status = RECONNECTING;
            continue;
        }

        // No more failure allowed past this point.

        // Clear out server stats for the server we connected to..
        cur->didConnect = true;
        cur->reconnects = 0;

        tReconnect = nc->reconnectThread;
        nc->reconnectThread = NULL;

        // At this point we know that we don't need the pending buffer
        // anymore. Destroy now.
        natsBuf_Destroy(nc->pending);
        nc->pending     = NULL;
        nc->usePending  = false;

        // Call reconnectedCB if appropriate. Since we are in a separate
        // thread, we could invoke the callback directly, however, we
        // still post it so all callbacks from a connection are serialized.
        if (nc->opts->reconnectedCb != NULL)
            natsAsyncCb_PostConnHandler(nc, ASYNC_RECONNECTED);

        // Release lock here, we will return below.
        natsConn_Unlock(nc);

        // Make sure we flush everything
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
    char        buffer[DEFAULT_BUF_SIZE];

    buffer[0] = '\0';

    // Create the CONNECT protocol
    s = _connectProto(nc, &cProto);

    // Add it to the buffer
    if (s == NATS_OK)
        s = natsConn_bufferWriteString(nc, cProto);

    // Add the PING protocol to the buffer
    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, _PING_OP_, _PING_OP_LEN_);
    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, _CRLF_, _CRLF_LEN_);

    // Flush the buffer
    if (s == NATS_OK)
        s = natsConn_bufferFlush(nc);

    // Now read the response from the server.
    if (s == NATS_OK)
        s = natsSock_ReadLine(&(nc->sockCtx), buffer, sizeof(buffer));

    // If Verbose is set, we expect +OK first.
    if ((s == NATS_OK) && nc->opts->verbose)
    {
        // Check protocol is as expected
        if (strncmp(buffer, _OK_OP_, _OK_OP_LEN_) != 0)
        {
            s = nats_setError(NATS_PROTOCOL_ERROR,
                              "Expected '%s', got '%s'",
                              _OK_OP_, buffer);
        }

        // Read the rest now...
        if (s == NATS_OK)
            s = natsSock_ReadLine(&(nc->sockCtx), buffer, sizeof(buffer));
    }

    // We except the PONG protocol
    if ((s == NATS_OK) && (strncmp(buffer, _PONG_OP_, _PONG_OP_LEN_) != 0))
    {
        // But it could be something else, like -ERR

        if (strncmp(buffer, _ERR_OP_, _ERR_OP_LEN_) == 0)
        {
            // Remove -ERR, trim spaces and quotes.
            nats_NormalizeErr(buffer);

            // Search if the error message says something about
            // authentication failure.

            if (nats_strcasestr(buffer, "authorization") != NULL)
                s = nats_setError(NATS_CONNECTION_AUTH_FAILED,
                                  "%s", buffer);
            else
                s = nats_setError(NATS_ERR, "%s", buffer);
        }
        else
        {
            s = nats_setError(NATS_PROTOCOL_ERROR,
                              "Expected '%s', got '%s'",
                              _PONG_OP_, buffer);
        }
    }

    if (s == NATS_OK)
        nc->status = CONNECTED;

    free(cProto);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_processConnInit(natsConnection *nc)
{
    natsStatus s = NATS_OK;

    nc->status = CONNECTING;

    // Process the INFO protocol that we should be receiving
    s = _processExpectedInfo(nc);

    // Send the CONNECT and PING protocol, and wait for the PONG.
    if (s == NATS_OK)
        s = _sendConnect(nc);

    // Clear our deadline, regardless of error
    natsDeadline_Clear(&(nc->sockCtx.deadline));

    // Switch to blocking socket here...
    if (s == NATS_OK)
        s = natsSock_SetBlocking(nc->sockCtx.fd, true);

    // Start the readLoop and flusher threads
    if (s == NATS_OK)
        s = _spinUpSocketWatchers(nc);

    if ((s == NATS_OK) && (nc->opts->evLoop != NULL))
    {
        s = natsSock_SetBlocking(nc->sockCtx.fd, false);

        // If we are reconnecting, buffer will have already been allocated
        if ((s == NATS_OK) && (nc->el.buffer == NULL))
        {
            nc->el.buffer = (char*) malloc(DEFAULT_BUF_SIZE);
            if (nc->el.buffer == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (s == NATS_OK)
        {
            // Set this first in case the event loop triggers the first READ
            // event just after this call returns.
            nc->sockCtx.useEventLoop = true;

            s = nc->opts->evCbs.attach(&(nc->el.data),
                                       nc->opts->evLoop,
                                       nc,
                                       (int) nc->sockCtx.fd);
            if (s == NATS_OK)
            {
                nc->el.attached = true;
            }
            else
            {
                nc->sockCtx.useEventLoop = false;

                nats_setError(s,
                              "Error attaching to the event loop: %d - %s",
                              s, natsStatus_GetText(s));
            }
        }
    }

    return NATS_UPDATE_ERR_STACK(s);
}

// Main connect function. Will connect to the server
static natsStatus
_connect(natsConnection *nc)
{
    natsStatus  s     = NATS_OK;
    natsStatus  retSts= NATS_OK;
    natsSrvPool *pool = NULL;
    int         i;
    int         poolSize;

    natsConn_Lock(nc);
    nc->initc = true;

    pool = nc->srvPool;

    // Create actual socket connection
    // For first connect we walk all servers in the pool and try
    // to connect immediately.

    // Get the size of the pool. The pool may change inside the loop
    // iteration due to INFO protocol.
    poolSize = natsSrvPool_GetSize(pool);
    for (i = 0; i < poolSize; i++)
    {
        nc->url = natsSrvPool_GetSrvUrl(pool,i);

        s = _createConn(nc);
        if (s == NATS_OK)
        {
            s = _processConnInit(nc);

            if (s == NATS_OK)
            {
                natsSrvPool_SetSrvDidConnect(pool, i, true);
                natsSrvPool_SetSrvReconnects(pool, i, 0);
                retSts = NATS_OK;
                break;
            }
            else
            {
                retSts = s;

                natsConn_Unlock(nc);

                _close(nc, DISCONNECTED, false);

                natsConn_Lock(nc);

                nc->url = NULL;
            }
            // Refresh our view of pool length since it may have been
            // modified when processing the INFO protocol.
            poolSize = natsSrvPool_GetSize(pool);
        }
        else
        {
            if (s == NATS_IO_ERROR)
                retSts = NATS_OK;
        }
    }

    if ((retSts == NATS_OK) && (nc->status != CONNECTED))
    {
        s = nats_setDefaultError(NATS_NO_SERVER);
    }

    nc->initc = false;
    natsConn_Unlock(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

// _processOpError handles errors from reading or parsing the protocol.
// The lock should not be held entering this function.
static void
_processOpError(natsConnection *nc, natsStatus s)
{
    natsConn_Lock(nc);

    if (_isConnecting(nc) || natsConn_isClosed(nc) || natsConn_isReconnecting(nc))
    {
        natsConn_Unlock(nc);

        return;
    }

    // Do reconnect only if allowed and we were actually connected
    if (nc->opts->allowReconnect && (nc->status == CONNECTED))
    {
        natsStatus ls = NATS_OK;

        // Set our new status
        nc->status = RECONNECTING;

        if (nc->ptmr != NULL)
            natsTimer_Stop(nc->ptmr);

        if (nc->sockCtx.fdActive)
        {
            natsConn_bufferFlush(nc);

            natsSock_Shutdown(nc->sockCtx.fd);
            nc->sockCtx.fdActive = false;
        }

        // If we use an external event loop, we need to stop polling
        // on the socket since we are going to reconnect.
        if (nc->el.attached)
        {
            // Stop polling for READ/WRITE events on that socket.
            nc->sockCtx.useEventLoop = false;
            nc->el.writeAdded = false;
            ls = nc->opts->evCbs.read(nc->el.data, NATS_EVENT_ACTION_REMOVE);
            if (ls == NATS_OK)
                ls = nc->opts->evCbs.write(nc->el.data, NATS_EVENT_ACTION_REMOVE);
        }

        // Create the pending buffer to hold all write requests while we try
        // to reconnect.
        ls = natsBuf_Create(&(nc->pending), nc->opts->reconnectBufSize);
        if (ls == NATS_OK)
        {
            nc->usePending = true;

            // Start the reconnect thread
            ls = natsThread_Create(&(nc->reconnectThread),
                                  _doReconnect, (void*) nc);
        }
        if (ls ==  NATS_OK)
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
natsConn_clearSSL(natsConnection *nc)
{
    if (nc->sockCtx.ssl == NULL)
        return;

    SSL_free(nc->sockCtx.ssl);
    nc->sockCtx.ssl = NULL;
}

static void
_readLoop(void  *arg)
{
    natsStatus  s = NATS_OK;
    char        buffer[DEFAULT_BUF_SIZE];
    natsSock    fd;
    int         n;

    natsConnection *nc = (natsConnection*) arg;

    natsConn_Lock(nc);

    if (nc->sockCtx.ssl != NULL)
        nats_sslRegisterThreadForCleanup();

    fd = nc->sockCtx.fd;

    if (nc->ps == NULL)
        s = natsParser_Create(&(nc->ps));

    while ((s == NATS_OK)
           && !natsConn_isClosed(nc)
           && !natsConn_isReconnecting(nc))
    {
        natsConn_Unlock(nc);

        n = 0;

        s = natsSock_Read(&(nc->sockCtx), buffer, sizeof(buffer), &n);
        if (s == NATS_OK)
            s = natsParser_Parse(nc, buffer, n);

        if (s != NATS_OK)
            _processOpError(nc, s);

        natsConn_Lock(nc);
    }

    natsSock_Close(fd);
    nc->sockCtx.fd       = NATS_SOCK_INVALID;
    nc->sockCtx.fdActive = false;

    // We need to cleanup some things if the connection was SSL.
    if (nc->sockCtx.ssl != NULL)
        natsConn_clearSSL(nc);

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
    natsStatus      s;

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

        if (natsConn_isClosed(nc) || natsConn_isReconnecting(nc))
        {
            natsConn_Unlock(nc);
            break;
        }

        if (nc->sockCtx.fdActive && (natsBuf_Len(nc->bw) > 0))
        {
            s = natsConn_bufferFlush(nc);
            if ((s != NATS_OK) && (nc->err == NATS_OK))
                nc->err = s;
        }

        natsConn_Unlock(nc);
    }

    // Release the connection to compensate for the retain when this thread
    // was created.
    natsConn_release(nc);
}

static void
_sendPing(natsConnection *nc, natsPong *pong)
{
    natsStatus  s     = NATS_OK;

    s = natsConn_bufferWrite(nc, _PING_PROTO_, _PING_PROTO_LEN_);
    if (s == NATS_OK)
    {
        // Flush the buffer in place.
        s = natsConn_bufferFlush(nc);
    }
    if (s == NATS_OK)
    {
        // Now that we know the PING was sent properly, update
        // the number of PING sent.
        nc->pongs.outgoingPings++;

        if (pong != NULL)
        {
            pong->id = nc->pongs.outgoingPings;

            // Add this pong to the list.
            pong->next = NULL;
            pong->prev = nc->pongs.tail;

            if (nc->pongs.tail != NULL)
                nc->pongs.tail->next = pong;

            nc->pongs.tail = pong;

            if (nc->pongs.head == NULL)
                nc->pongs.head = pong;
        }
    }
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

    // If we have more PINGs out than PONGs in, consider
    // the connection stale.
    if (++(nc->pout) > nc->opts->maxPingsOut)
    {
        natsConn_Unlock(nc);
        _processOpError(nc, NATS_STALE_CONNECTION);
        return;
    }

    _sendPing(nc, NULL);

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
    natsStatus  s = NATS_OK;

    nc->pout        = 0;
    nc->flusherStop = false;

    if (nc->opts->evLoop == NULL)
    {
        // Let's not rely on the created threads acquiring lock that would make it
        // safe to retain only on success.
        _retain(nc);

        s = natsThread_Create(&(nc->readLoopThread), _readLoop, (void*) nc);
        if (s != NATS_OK)
            _release(nc);
    }

    if (s == NATS_OK)
    {
        _retain(nc);

        s = natsThread_Create(&(nc->flusherThread), _flusher, (void*) nc);
        if (s != NATS_OK)
            _release(nc);
    }

    if ((s == NATS_OK) && (nc->opts->pingInterval > 0))
    {
        _retain(nc);

        if (nc->ptmr == NULL)
        {
            s = natsTimer_Create(&(nc->ptmr),
                                 _processPingTimer,
                                 _pingStopppedCb,
                                 nc->opts->pingInterval,
                                 (void*) nc);
            if (s != NATS_OK)
                _release(nc);
        }
        else
        {
            natsTimer_Reset(nc->ptmr, nc->opts->pingInterval);
        }
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

    natsMutex_Lock(nc->subsMu);
    natsHashIter_Init(&iter, nc->subs);
    while (natsHashIter_Next(&iter, NULL, (void**) &sub))
    {
        (void) natsHashIter_RemoveCurrent(&iter);

        natsSub_close(sub, true);

        natsSub_release(sub);
    }
    natsMutex_Unlock(nc->subsMu);
}


// Low level close call that will do correct cleanup and set
// desired status. Also controls whether user defined callbacks
// will be triggered. The lock should not be held entering this
// function. This function will handle the locking manually.
static void
_close(natsConnection *nc, natsConnStatus status, bool doCBs)
{
    struct threadsToJoin    ttj;
    bool                    sockWasActive = false;
    bool                    detach = false;

    natsConn_lockAndRetain(nc);

    if (natsConn_isClosed(nc))
    {
        nc->status = status;

        natsConn_unlockAndRelease(nc);
        return;
    }

    nc->status = CLOSED;

    _initThreadsToJoin(&ttj, nc, true);

    // Kick out all calls to natsConnection_Flush[Timeout]().
    _clearPendingFlushRequests(nc);

    if (nc->ptmr != NULL)
        natsTimer_Stop(nc->ptmr);

    // Remove all subscriptions. This will kick out the delivery threads,
    // and unblock NextMsg() calls.
    _removeAllSubscriptions(nc);

    // Go ahead and make sure we have flushed the outbound buffer.
    nc->status = CLOSED;
    if (nc->sockCtx.fdActive)
    {
        natsConn_bufferFlush(nc);

        // If there is no readLoop, then it is our responsibility to close
        // the socket. Otherwise, _readLoop is the one doing it.
        if ((ttj.readLoop == NULL) && (nc->opts->evLoop == NULL))
        {
            natsSock_Close(nc->sockCtx.fd);
            nc->sockCtx.fd = NATS_SOCK_INVALID;

            // We need to cleanup some things if the connection was SSL.
            if (nc->sockCtx.ssl != NULL)
                natsConn_clearSSL(nc);
        }
        else
        {
            // Shutdown the socket to stop any read/write operations.
            // The socket will be closed by the _readLoop thread.
            natsSock_Shutdown(nc->sockCtx.fd);
        }
        nc->sockCtx.fdActive = false;
        sockWasActive = true;
    }

    // Perform appropriate callback if needed for a disconnect.
    // Do not invoke if we were disconnected and failed to reconnect (since
    // it has already been invoked in doReconnect).
    if (doCBs && (nc->opts->disconnectedCb != NULL) && sockWasActive)
        natsAsyncCb_PostConnHandler(nc, ASYNC_DISCONNECTED);

    natsConn_Unlock(nc);

    _joinThreads(&ttj);

    natsConn_Lock(nc);

    // Perform appropriate callback if needed for a connection closed.
    if (doCBs && (nc->opts->closedCb != NULL))
        natsAsyncCb_PostConnHandler(nc, ASYNC_CLOSED);

    nc->status = status;

    if (nc->el.attached)
    {
        nc->el.attached = false;
        detach = true;
        _retain(nc);
    }

    natsConn_unlockAndRelease(nc);

    if (detach)
    {
        nc->opts->evCbs.detach(nc->el.data);
        natsConn_release(nc);
    }
}

static natsStatus
_createMsg(natsMsg **newMsg, natsConnection *nc, char *buf, int bufLen)
{
    natsStatus  s        = NATS_OK;
    int         subjLen  = 0;
    char        *reply   = NULL;
    int         replyLen = 0;

    subjLen = natsBuf_Len(nc->ps->ma.subject);

    if (nc->ps->ma.reply != NULL)
    {
        reply    = natsBuf_Data(nc->ps->ma.reply);
        replyLen = natsBuf_Len(nc->ps->ma.reply);
    }

    s = natsMsg_create(newMsg,
                       (const char*) natsBuf_Data(nc->ps->ma.subject), subjLen,
                       (const char*) reply, replyLen,
                       (const char*) buf, bufLen);
    return s;
}

natsStatus
natsConn_processMsg(natsConnection *nc, char *buf, int bufLen)
{
    natsStatus       s    = NATS_OK;
    natsSubscription *sub = NULL;
    natsMsg          *msg = NULL;
    natsMsgDlvWorker *ldw = NULL;
    bool             sc   = false;

    natsMutex_Lock(nc->subsMu);

    nc->stats.inMsgs  += 1;
    nc->stats.inBytes += (uint64_t) bufLen;

    sub = natsHash_Get(nc->subs, nc->ps->ma.sid);
    if (sub == NULL)
    {
        natsMutex_Unlock(nc->subsMu);
        return NATS_OK;
    }

    // Do this outside of sub's lock, even if we end-up having to destroy
    // it because we have reached the maxPendingMsgs count. This reduces
    // lock contention.
    s = _createMsg(&msg, nc, buf, bufLen);
    if (s != NATS_OK)
    {
        natsMutex_Unlock(nc->subsMu);
        return s;
    }

    if ((ldw = sub->libDlvWorker) != NULL)
        natsMutex_Lock(ldw->lock);
    else
        natsSub_Lock(sub);

    sub->msgList.msgs++;
    sub->msgList.bytes += bufLen;

    if (((sub->msgsLimit > 0) && (sub->msgList.msgs > sub->msgsLimit))
        || ((sub->bytesLimit > 0) && (sub->msgList.bytes > sub->bytesLimit)))
    {
        natsMsg_Destroy(msg);

        sub->dropped++;

        sc = sub->slowConsumer;
        sub->slowConsumer = true;

        // Undo stats from above.
        sub->msgList.msgs--;
        sub->msgList.bytes -= bufLen;
    }
    else
    {
        natsMsgList *list = NULL;

        if (sub->msgList.msgs > sub->msgsMax)
            sub->msgsMax = sub->msgList.msgs;

        if (sub->msgList.bytes > sub->bytesMax)
            sub->bytesMax = sub->msgList.bytes;

        sub->slowConsumer = false;

        if (ldw != NULL)
        {
            msg->sub = sub;
            list = &ldw->msgList;
        }
        else
        {
            list = &sub->msgList;
        }

        if (list->head == NULL)
            list->head = msg;

        if (list->tail != NULL)
            list->tail->next = msg;

        list->tail = msg;

        if (ldw != NULL)
        {
            if (ldw->inWait)
                natsCondition_Broadcast(ldw->cond);
        }
        else
        {
            if (sub->inWait > 0)
                natsCondition_Broadcast(sub->cond);
        }
    }

    if (ldw != NULL)
        natsMutex_Unlock(ldw->lock);
    else
        natsSub_Unlock(sub);

    natsMutex_Unlock(nc->subsMu);

    if (sc)
    {
        natsConn_Lock(nc);

        nc->err = NATS_SLOW_CONSUMER;

        if (nc->opts->asyncErrCb != NULL)
            natsAsyncCb_PostErrHandler(nc, sub, NATS_SLOW_CONSUMER);

        natsConn_Unlock(nc);
    }

    return s;
}

void
natsConn_processOK(natsConnection *nc)
{
    // Do nothing for now.
}

// _processPermissionViolation is called when the server signals a subject
// permissions violation on either publish or subscribe.
static void
_processPermissionViolation(natsConnection *nc, char *error)
{
    natsConn_Lock(nc);
    nc->err = NATS_NOT_PERMITTED;
    snprintf(nc->errStr, sizeof(nc->errStr), "%s", error);
    if (nc->opts->asyncErrCb != NULL)
        natsAsyncCb_PostErrHandler(nc, NULL, NATS_NOT_PERMITTED);
    natsConn_Unlock(nc);
}

void
natsConn_processErr(natsConnection *nc, char *buf, int bufLen)
{
    char error[256];

    // Copy the error in this local buffer.
    snprintf(error, sizeof(error), "%.*s", bufLen, buf);

    // Trim spaces and remove quotes.
    nats_NormalizeErr(error);

    if (strcasecmp(error, STALE_CONNECTION) == 0)
    {
        _processOpError(nc, NATS_STALE_CONNECTION);
    }
    else if (nats_strcasestr(error, PERMISSIONS_ERR) != NULL)
    {
        _processPermissionViolation(nc, error);
    }
    else
    {
        natsConn_Lock(nc);
        nc->err = NATS_ERR;
        snprintf(nc->errStr, sizeof(nc->errStr), "%s", error);
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
    natsPong *pong = NULL;

    natsConn_Lock(nc);

    nc->pongs.incoming++;

    // Check if the first pong's id in the list matches the incoming Id.
    if (((pong = nc->pongs.head) != NULL)
        && (pong->id == nc->pongs.incoming))
    {
        // Remove the pong from the list
        _removePongFromList(nc, pong);

        // Release the Flush[Timeout] call
        pong->id = 0;

        // There may be more than one thread waiting on this
        // condition variable, so we use broadcast instead of
        // signal.
        natsCondition_Broadcast(nc->pongs.cond);
    }

    nc->pout = 0;

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

    return NATS_UPDATE_ERR_STACK(s);
}

void
natsConn_removeSubscription(natsConnection *nc, natsSubscription *removedSub)
{
    natsSubscription *sub = NULL;

    natsMutex_Lock(nc->subsMu);

    sub = natsHash_Remove(nc->subs, removedSub->sid);

    // Note that the sub may have already been removed, so 'sub == NULL'
    // is not an error.
    if (sub != NULL)
        natsSub_close(sub, false);

    natsMutex_Unlock(nc->subsMu);

    // If we really removed the subscription, then release it.
    if (sub != NULL)
        natsSub_release(sub);
}

// subscribe is the internal subscribe function that indicates interest in a
// subject.
natsStatus
natsConn_subscribe(natsSubscription **newSub,
                   natsConnection *nc, const char *subj, const char *queue,
                   int64_t timeout, natsMsgHandler cb, void *cbClosure)
{
    natsStatus          s    = NATS_OK;
    natsSubscription    *sub = NULL;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((subj == NULL) || (strlen(subj) == 0))
        return nats_setDefaultError(NATS_INVALID_SUBJECT);

    natsConn_Lock(nc);

    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);

        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    s = natsSub_create(&sub, nc, subj, queue, timeout, cb, cbClosure);
    if (s == NATS_OK)
    {
        natsMutex_Lock(nc->subsMu);
        sub->sid = ++(nc->ssid);
        s = natsConn_addSubcription(nc, sub);
        natsMutex_Unlock(nc->subsMu);
    }

    if (s == NATS_OK)
    {
        // We will send these for all subs when we reconnect
        // so that we can suppress here.
        if (!natsConn_isReconnecting(nc))
        {
            char    *proto = NULL;
            int     res    = 0;

            res = nats_asprintf(&proto, _SUB_PROTO_,
                                subj,
                                (queue == NULL ? "" : queue),
                                (int) sub->sid);
            if (res < 0)
                s = nats_setDefaultError(NATS_NO_MEMORY);

            if (s == NATS_OK)
            {
                s = natsConn_bufferWriteString(nc, proto);
                if (s == NATS_OK)
                    natsConn_kickFlusher(nc);

                // We should not return a failure if we get an issue
                // with the buffer write (except if it is no memory).
                // For IO errors (if we just got disconnected), the
                // reconnect logic will resend the sub protocol.

                if (s != NATS_NO_MEMORY)
                    s = NATS_OK;
            }

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
        natsSub_close(sub, false);

        natsConn_removeSubscription(nc, sub);

        natsSub_release(sub);
    }

    natsConn_Unlock(nc);

    return NATS_UPDATE_ERR_STACK(s);
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
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    natsMutex_Lock(nc->subsMu);
    sub = natsHash_Get(nc->subs, sub->sid);
    natsMutex_Unlock(nc->subsMu);
    if (sub == NULL)
    {
        // Already unsubscribed
        natsConn_Unlock(nc);
        return NATS_OK;
    }

    natsSub_Lock(sub);
    sub->max = max;
    natsSub_Unlock(sub);

    if (max == 0)
        natsConn_removeSubscription(nc, sub);

    if (!natsConn_isReconnecting(nc))
    {
        // We will send these for all subs when we reconnect
        // so that we can suppress here.
        s = _sendUnsubProto(nc, sub->sid, max);
        if (s == NATS_OK)
            natsConn_kickFlusher(nc);

        // We should not return a failure if we get an issue
        // with the buffer write (except if it is no memory).
        // For IO errors (if we just got disconnected), the
        // reconnect logic will resend the unsub protocol.
        if ((s != NATS_OK) && (s != NATS_NO_MEMORY))
        {
            nats_clearLastError();
            s = NATS_OK;
        }
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

    return NATS_UPDATE_ERR_STACK(s);
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
        return nats_setDefaultError(NATS_NO_MEMORY);
    }

    natsLib_Retain();

    nc->refs        = 1;
    nc->sockCtx.fd  = NATS_SOCK_INVALID;
    nc->opts        = options;

    if (nc->opts->maxPingsOut == 0)
        nc->opts->maxPingsOut = NATS_OPTS_DEFAULT_MAX_PING_OUT;

    if (nc->opts->maxPendingMsgs == 0)
        nc->opts->maxPendingMsgs = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;

    if (nc->opts->reconnectBufSize == 0)
        nc->opts->reconnectBufSize = NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE;

    nc->errStr[0] = '\0';

    s = natsMutex_Create(&(nc->mu));
    if (s == NATS_OK)
        s = natsMutex_Create(&(nc->subsMu));
    if (s == NATS_OK)
        s = _setupServerPool(nc);
    if (s == NATS_OK)
        s = natsHash_Create(&(nc->subs), 8);
    if (s == NATS_OK)
        s = natsSock_Init(&nc->sockCtx);
    if (s == NATS_OK)
    {
        s = natsBuf_Create(&(nc->scratch), DEFAULT_SCRATCH_SIZE);
        if (s == NATS_OK)
            s = natsBuf_Append(nc->scratch, _PUB_P_, _PUB_P_LEN_);
    }
    if (s == NATS_OK)
        s = natsCondition_Create(&(nc->flusherCond));
    if (s == NATS_OK)
        s = natsCondition_Create(&(nc->pongs.cond));

    if (s == NATS_OK)
        *newConn = nc;
    else
        natsConn_release(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConnection_Connect(natsConnection **newConn, natsOptions *options)
{
    natsStatus      s       = NATS_OK;
    natsConnection  *nc     = NULL;
    natsOptions     *opts   = NULL;

    if (options == NULL)
    {
        s = natsConnection_ConnectTo(newConn, NATS_DEFAULT_URL);
        return NATS_UPDATE_ERR_STACK(s);
    }

    opts = natsOptions_clone(options);
    if (opts == NULL)
        s = NATS_NO_MEMORY;

    if (s == NATS_OK)
        s = natsConn_create(&nc, opts);
    if (s == NATS_OK)
        s = _connect(nc);

    if (s == NATS_OK)
        *newConn = nc;
    else
        natsConn_release(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_processUrlString(natsOptions *opts, const char *urls)
{
    int         count        = 0;
    natsStatus  s            = NATS_OK;
    char        **serverUrls = NULL;
    char        *urlsCopy    = NULL;
    char        *commaPos    = NULL;
    char        *ptr         = NULL;
    int         len;

    ptr = (char*) urls;
    while ((ptr = strchr(ptr, ',')) != NULL)
    {
        ptr++;
        count++;
    }
    if (count == 0)
        return natsOptions_SetURL(opts, urls);

    serverUrls = (char**) NATS_CALLOC(count + 1, sizeof(char*));
    if (serverUrls == NULL)
        s = NATS_NO_MEMORY;
    if (s == NATS_OK)
    {
        urlsCopy = NATS_STRDUP(urls);
        if (urlsCopy == NULL)
        {
            NATS_FREE(serverUrls);
            return NATS_NO_MEMORY;
        }
    }

    count = 0;
    ptr = urlsCopy;

    do
    {
        while (*ptr == ' ')
            ptr++;
        serverUrls[count++] = ptr;

        commaPos = strchr(ptr, ',');
        if (commaPos != NULL)
        {
            ptr = (char*)(commaPos + 1);
            *(commaPos) = '\0';
        }

        len = (int) strlen(ptr);
        while ((len > 0) && (ptr[len-1] == ' '))
            ptr[--len] = '\0';

    } while (commaPos != NULL);

    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, (const char**) serverUrls, count);

    NATS_FREE(urlsCopy);
    NATS_FREE(serverUrls);

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
        s = _processUrlString(opts, url);
    if (s == NATS_OK)
        s = natsConn_create(&nc, opts);
    if (s == NATS_OK)
        s = _connect(nc);

    if (s == NATS_OK)
        *newConn = nc;
    else
        natsConn_release(nc);

    return NATS_UPDATE_ERR_STACK(s);
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

    reconnecting = natsConn_isReconnecting(nc);

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

static void
_destroyPong(natsConnection *nc, natsPong *pong)
{
    // If this pong is the cached one, do not free
    if (pong == &(nc->pongs.cached))
        memset(pong, 0, sizeof(natsPong));
    else
        NATS_FREE(pong);
}

natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout)
{
    natsStatus  s       = NATS_OK;
    int64_t     target  = 0;
    natsPong    *pong   = NULL;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (timeout <= 0)
        return nats_setDefaultError(NATS_INVALID_TIMEOUT);

    natsConn_lockAndRetain(nc);

    if (natsConn_isClosed(nc))
        s = nats_setDefaultError(NATS_CONNECTION_CLOSED);

    if (s == NATS_OK)
    {
        // Use the cached PONG instead of creating one if the list
        // is empty
        if (nc->pongs.head == NULL)
            pong = &(nc->pongs.cached);
        else
            pong = (natsPong*) NATS_CALLOC(1, sizeof(natsPong));

        if (pong == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    if (s == NATS_OK)
    {
        // Send the ping (and add the pong to the list)
        _sendPing(nc, pong);

        target = nats_Now() + timeout;

        // When the corresponding PONG is received, the PONG processing code
        // will set pong->id to 0 and do a broadcast. This will allow this
        // code to break out of the 'while' loop.
        while ((s != NATS_TIMEOUT)
               && !natsConn_isClosed(nc)
               && (pong->id > 0))
        {
            s = natsCondition_AbsoluteTimedWait(nc->pongs.cond, nc->mu, target);
        }

        if ((s == NATS_OK) && (nc->status == CLOSED))
        {
            // The connection has been closed while we were waiting
            s = nats_setDefaultError(NATS_CONNECTION_CLOSED);
        }
        else if ((s == NATS_OK) && (pong->id == -1))
        {
            // The connection was disconnected and the library is in the
            // process of trying to reconnect
            s = nats_setDefaultError(NATS_CONNECTION_DISCONNECTED);
        }

        if (s != NATS_OK)
        {
            // If we are here, it is possible that we timed-out, or some other
            // error occurred. Make sure the request is no longer in the list.
            _removePongFromList(nc, pong);

            // Set the error. If we don't do that, and flush is called in a loop,
            // the stack would be growing with Flush/FlushTimeout.
            s = nats_setDefaultError(s);
        }

        // We are done with the pong
        _destroyPong(nc, pong);
    }

    natsConn_unlockAndRelease(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConnection_Flush(natsConnection *nc)
{
    natsStatus s = natsConnection_FlushTimeout(nc, 60000);
    return NATS_UPDATE_ERR_STACK(s);
}

int
natsConnection_Buffered(natsConnection *nc)
{
    int buffered = -1;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

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
        return nats_setDefaultError(NATS_INVALID_ARG);

    // Stats are updated either under connection's mu or subsMu mutexes.
    // Lock both to safely get them.
    natsConn_Lock(nc);
    natsMutex_Lock(nc->subsMu);

    memcpy(stats, &(nc->stats), sizeof(natsStatistics));

    natsMutex_Unlock(nc->subsMu);
    natsConn_Unlock(nc);

    return s;
}

natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize)
{
    natsStatus  s = NATS_OK;

    if ((nc == NULL) || (buffer == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsConn_Lock(nc);

    buffer[0] = '\0';

    if ((nc->status == CONNECTED) && (nc->url->fullUrl != NULL))
    {
        if (strlen(nc->url->fullUrl) >= bufferSize)
            s = nats_setDefaultError(NATS_INSUFFICIENT_BUFFER);

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
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsConn_Lock(nc);

    buffer[0] = '\0';

    if ((nc->status == CONNECTED) && (nc->info.id != NULL))
    {
        if (strlen(nc->info.id) >= bufferSize)
            s = nats_setDefaultError(NATS_INSUFFICIENT_BUFFER);

        if (s == NATS_OK)
            snprintf(buffer, bufferSize, "%s", nc->info.id);
    }

    natsConn_Unlock(nc);

    return s;
}

natsStatus
natsConnection_GetServers(natsConnection *nc, char ***servers, int *count)
{
    natsStatus  s       = NATS_OK;

    if ((nc == NULL) || (servers == NULL) || (count == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsConn_Lock(nc);

    s = natsSrvPool_GetServers(nc->srvPool, false, servers, count);

    natsConn_Unlock(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConnection_GetDiscoveredServers(natsConnection *nc, char ***servers, int *count)
{
    natsStatus  s       = NATS_OK;

    if ((nc == NULL) || (servers == NULL) || (count == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsConn_Lock(nc);

    s = natsSrvPool_GetServers(nc->srvPool, true, servers, count);

    natsConn_Unlock(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError)
{
    natsStatus  s;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

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

    nats_doNotUpdateErrStack(true);

    _close(nc, CLOSED, true);

    nats_doNotUpdateErrStack(false);
}

void
natsConnection_Destroy(natsConnection *nc)
{
    if (nc == NULL)
        return;

    nats_doNotUpdateErrStack(true);

    _close(nc, CLOSED, true);

    nats_doNotUpdateErrStack(false);

    natsConn_release(nc);
}

void
natsConnection_ProcessReadEvent(natsConnection *nc)
{
    natsStatus      s = NATS_OK;
    int             n = 0;
    char            *buffer;
    int             size;

    natsConn_Lock(nc);

    if (!(nc->el.attached))
    {
        natsConn_Unlock(nc);
        return;
    }

    if (nc->ps == NULL)
    {
        s = natsParser_Create(&(nc->ps));
        if (s != NATS_OK)
            nats_setDefaultError(NATS_NO_MEMORY);
    }

    if ((s != NATS_OK) || natsConn_isClosed(nc) || natsConn_isReconnecting(nc))
    {
        (void) NATS_UPDATE_ERR_STACK(s);
        natsConn_Unlock(nc);
        return;
    }

    _retain(nc);

    buffer = nc->el.buffer;
    size   = DEFAULT_BUF_SIZE;

    natsConn_Unlock(nc);

    // Do not try to read again here on success. If more than one connection
    // is attached to the same loop, and there is a constant stream of data
    // coming for the first connection, this would starve the second connection.
    // So return and we will be called back later by the event loop.
    s = natsSock_Read(&(nc->sockCtx), buffer, size, &n);
    if (s == NATS_OK)
        s = natsParser_Parse(nc, buffer, n);

    if (s != NATS_OK)
        _processOpError(nc, s);

    natsConn_release(nc);
}

void
natsConnection_ProcessWriteEvent(natsConnection *nc)
{
    natsStatus  s = NATS_OK;
    int         n = 0;
    char        *buf;
    int         len;

    natsConn_Lock(nc);

    if (!(nc->el.attached) || (nc->sockCtx.fd == NATS_SOCK_INVALID))
    {
        natsConn_Unlock(nc);
        return;
    }

    buf = natsBuf_Data(nc->bw);
    len = natsBuf_Len(nc->bw);

    s = natsSock_Write(&(nc->sockCtx), buf, len, &n);
    if (s == NATS_OK)
    {
        if (n == len)
        {
            // We sent all the data, reset buffer and remove WRITE event.
            natsBuf_Reset(nc->bw);

            s = nc->opts->evCbs.write(nc->el.data, NATS_EVENT_ACTION_REMOVE);
            if (s == NATS_OK)
                nc->el.writeAdded = false;
            else
                nats_setError(s, "Error processing write request: %d - %s",
                              s, natsStatus_GetText(s));
        }
        else
        {
            // We sent some part of the buffer. Move the remaining at the beginning.
            natsBuf_Consume(nc->bw, n);
        }
    }

    natsConn_Unlock(nc);

    if (s != NATS_OK)
        _processOpError(nc, s);

    (void) NATS_UPDATE_ERR_STACK(s);
}
