// Copyright 2015-2023 The NATS Authors
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

#include "natsp.h"

#include "servers.h"
#include "conn.h"
#include "opts.h"

#define STALE_CONNECTION "Stale Connection"
#define PERMISSIONS_ERR "Permissions Violation"
#define AUTHORIZATION_ERR "Authorization Violation"
#define AUTHENTICATION_EXPIRED_ERR "User Authentication Expired"

#define NATS_DEFAULT_INBOX_PRE "_INBOX."
#define NATS_DEFAULT_INBOX_PRE_LEN (7)

#define NATS_MAX_REQ_ID_LEN (19) // to display 2^63-1 number

#define ERR_CODE_AUTH_EXPIRED (1)
#define ERR_CODE_AUTH_VIOLATION (2)

#define SET_WRITE_DEADLINE(nc)         \
    if ((nc)->opts->writeDeadline > 0) \
    natsDeadline_Init(&(nc)->sockCtx.writeDeadline, (nc)->opts->writeDeadline)

#ifdef DEV_MODE

#define _retain(c) natsConn_retain(c)
#define _release(c) natsConn_release(c)

#else
// We know what we are doing :-)

#define _retain(c) ((c)->refs++)
#define _release(c) ((c)->refs--)

#endif // DEV_MODE

static natsStatus _connect(natsConnection *nc);
static natsStatus _connectTCP(natsConnection *nc);
static natsStatus _createConnectionObject(natsConnection **newnc, natsEventLoop *ev, natsOptions *opts);
static natsStatus _evStopPolling(natsConnection *nc);
static void _freeConn(natsConnection *nc);

natsStatus
nats_AsyncConnectWithOptions(natsConnection **newConn, natsEventLoop *ev, natsOptions *options)
{
    natsStatus s = NATS_OK;
    natsConnection *nc = NULL;

    if (options == NULL)
    {
        // s = natsConnection_ConnectTo(newConn, NATS_DEFAULT_URL);
        return NATS_UPDATE_ERR_STACK(NATS_INVALID_ARG);
    }

    s = _createConnectionObject(&nc, ev, options);
    IFOK(s,
         natsServers_Create(&(nc->servers), nc->lifetimePool, nc->opts));
    IFOK(s, ALWAYS_OK(
                nc->cur = natsServers_Get(nc->servers, 0)));
    IFOK(s, _connectTCP(nc));
    IFOK(s, CHECK_NO_MEMORY(
                nc->evState = nats_palloc(nc->lifetimePool, ev->ctxSize)));
    IFOK(s, nc->ev.attach(nc->evState, ev, nc, nc->sockCtx.fd));

    if (s != NATS_OK)
    {
        natsConn_release(nc);
        nats_setError(s,
                      "Error attaching to the event loop: %d - %s",
                      s, natsStatus_GetText(s));
        return NATS_UPDATE_ERR_STACK(s);
    }
    nc->evAttached = true;
    *newConn = nc;
    return NATS_OK;
}

// Creates and initializes a natsConnection memory object, does not take any
// action yet.
static natsStatus
_createConnectionObject(natsConnection **newConn, natsEventLoop *ev, natsOptions *opts)
{
    natsStatus s = NATS_OK;
    natsPool *pool = NULL;
    natsConnection *nc = NULL;

    s = nats_createPool(&pool, &opts->mem, "conn-lifetime");
    IFOK(s, CHECK_NO_MEMORY(nc = nats_palloc(pool, sizeof(natsConnection))));
    IFOK(s, natsConn_createParser(&(nc->ps), pool));
    IFOK(s, natsSock_Init(&nc->sockCtx));
    IFOK(s, natsWriteChain_init(&nc->writeChain, &opts->mem)); // TODO <>/<> defer to connect time

    if (s != NATS_OK)
    {
        nats_releasePool(pool);
        return NATS_UPDATE_ERR_STACK(s);
    }

    nats_retainGlobalLib();
    nats_cloneOptions(&nc->opts, pool, opts);
    nc->ev = *ev;
    nc->state = NATS_CONN_STATUS_DISCONNECTED;
    nc->refs = 1;
    nc->lifetimePool = pool;
    CONNTRACEf("_createConnectionObject: created natsConnection: %p", (void *)nc);
    *newConn = nc;

    return NATS_OK;
}

// _connectTCP will connect to the server and do the right thing when an
// existing connection is in place.
static natsStatus
_connectTCP(natsConnection *nc)
{
    natsStatus s = NATS_OK;

    // TODO <>/<> check that we are not already connected?
    nc->state = NATS_CONN_STATUS_CONNECTING;

    // Initialize the connect-time memory pool.
    s = nats_createPool(&nc->connectPool, &nc->opts->mem, "conn-connect");
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    nc->sockCtx.orderIP = nc->opts->net.orderIP;
    nc->sockCtx.noRandomize = nc->opts->net.noRandomize;
    s = natsSock_ConnectTcp(&(nc->sockCtx), nc->connectPool, nc->cur->url->host, nc->cur->url->port);
    if (STILL_OK(s))
        nc->sockCtx.fdActive = true;
    if (STILL_OK(s))
        CONNDEBUGf("TCP connected to %s", nc->cur->url->fullUrl);
    

    return NATS_UPDATE_ERR_STACK(s);
}

// Low level close call that will do correct cleanup and set
// desired status. Also controls whether user defined callbacks
// will be triggered. The lock should not be held entering this
// function. This function will handle the locking manually.
static void
_close(natsConnection *nc, natsConnStatus state, bool fromPublicClose, bool doCBs)
{
    natsConn_retain(nc);

    if (natsConn_isClosed(nc))
    {
        nc->state = state;

        natsConn_release(nc);
        return;
    }

    nc->state = NATS_CONN_STATUS_CLOSED;

    nc->ev.stop(nc->evState);

    natsSock_Close(nc->sockCtx.fd);
    nc->sockCtx.fd = NATS_SOCK_INVALID;

    // We need to cleanup some things if the connection was SSL.
    // _clearSSL(nc);
    nc->sockCtx.fdActive = false;

    if (nc->opts->net.closed != NULL)
        nc->opts->net.closed(nc, nc->opts->net.closedClosure);
    if (nc->opts->net.disconnected != NULL)
        nc->opts->net.disconnected(nc, nc->opts->net.disconnectedClosure);

    nc->state = state;
    nc->ev.detach(nc->evState);
    natsConn_release(nc);
}

// natsConn_processOpError handles errors from reading or parsing the protocol.
// The lock should not be held entering this function.
bool natsConn_processOpError(natsConnection *nc, natsStatus s)
{
    CONNERROR("ERROR!");
    _close(nc, NATS_CONN_STATUS_CLOSED, false, true);

    return false;
}

// static void
// _processPingTimer(natsTimer *timer, void *arg)
// {
//     natsConnection *nc = (natsConnection *)arg;

//     if (nc->status != NATS_CONN_STATUS_CONNECTED)
//     {
//         return;
//     }

//     // If we have more PINGs out than PONGs in, consider
//     // the connection stale.
//     if (++(nc->pout) > nc->opts->maxPingsOut)
//     {
//         natsConn_processOpError(nc, NATS_STALE_CONNECTION, false);
//         return;
//     }

//     _sendPing(nc, NULL);
// }

// static void
// _pingStopppedCb(natsTimer *timer, void *closure)
// {
//     natsConnection *nc = (natsConnection *)closure;

//     natsConn_release(nc);
// }

void nats_CloseConnection(natsConnection *nc)
{
    if (nc == NULL)
        return;

    nats_doNotUpdateErrStack(true);

    _close(nc, NATS_CONN_STATUS_CLOSED, true, true);

    nats_doNotUpdateErrStack(false);
}

void nats_DestroyConnection(natsConnection *nc)
{
    nats_CloseConnection(nc);
    natsConn_release(nc);
}

void natsConn_freeConn(natsConnection *nc)
{
    if (nc == NULL)
        return;

    nats_releasePool(nc->opPool);
    nats_releasePool(nc->connectPool);
    nats_releasePool(nc->lifetimePool); // will free nc itself
    nats_releaseGlobalLib();
}

bool natsConn_srvVersionAtLeast(natsConnection *nc, int major, int minor, int update)
{
    bool ok;
    ok = (((nc->srvVersion.ma > major) || ((nc->srvVersion.ma == major) && (nc->srvVersion.mi > minor)) || ((nc->srvVersion.ma == major) && (nc->srvVersion.mi == minor) && (nc->srvVersion.up >= update))) ? true : false);
    return ok;
}

const char *
nats_GetConnectionError(natsConnection *nc)
{
    natsStatus s;

    if (nc == NULL)
        return "invalid";

    s = nc->err;
    if (s == NATS_OK)
        nc->errStr[0] = '\0';
    else if (nc->errStr[0] == '\0')
        snprintf(nc->errStr, sizeof(nc->errStr), "%s", natsStatus_GetText(s));

    return nc->errStr;
}
