// Copyright 2015-2024 The NATS Authors
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

#include "conn.h"
#include "json.h"
#include "opts.h"
#include "servers.h"

static uint64_t _poolc = 0;

static inline natsStatus _startOp(natsReadBuffer **rbuf, natsConnection *nc)
{
    char name[64];
    if (rbuf != NULL)
        *rbuf = NULL;

    _poolc++;
    snprintf(name, sizeof(name), "conn-op-%" PRIu64, _poolc);
    if (nc->opPool == NULL)
        return nats_createPool(&nc->opPool, &nc->opts->mem, name);
    else
        return nats_recyclePool(&(nc->opPool), rbuf);
}

void nats_ProcessReadEvent(natsConnection *nc)
{
    natsStatus s = NATS_OK;
    natsReadBuffer *rbuf = NULL;
    natsString haveRead = {.len = 0};

    if (!(nc->evAttached) || (nc->sockCtx.fd == NATS_SOCK_INVALID))
    {
        return;
    }
    natsConn_retain(nc);

    // Recycle will create a new pool if needed. We need to do it before we read the data.
    if (natsConn_expectingNewOp(nc->ps))
        s = _startOp(NULL, nc);

    // We always exhaust any data we had read in the previous call, so just ask
    // for a (new or sufficently free) read buffer.
    IFOK(s, natsPool_getReadBuffer(&rbuf, nc->opPool));
    if (STILL_OK(s))
    {
        // Do not try to read again here on success. If more than one connection
        // is attached to the same loop, and there is a constant stream of data
        // coming for the first connection, this would starve the second
        // connection. So return and we will be called back later by the event
        // loop.
        haveRead.data = natsReadBuffer_end(rbuf);
        s = natsSock_Read(&(nc->sockCtx),
                          natsReadBuffer_end(rbuf),
                          natsReadBuffer_available(&nc->opts->mem, rbuf),
                          &haveRead.len);

        if (STILL_OK(s))
            CONNTRACE_in(&haveRead);
        rbuf->buf.len += haveRead.len; // works on error.
    }

    while ((STILL_OK(s)) && (rbuf != NULL) && (natsReadBuffer_unreadLen(rbuf) > 0))
    {
        size_t consumedByParser = 0;

        // The parser either detects an end of an op, or consumes the entire slice given to it. We want to consume all of the data we have read, so if it's a new op and there's unread data, start a new buffer.
        s = natsConn_parseOp(nc, rbuf->readFrom, natsReadBuffer_end(rbuf), &consumedByParser);
        rbuf->readFrom += consumedByParser; // a no-op on error.

        // If the parser is ready for a new op, recycle the pool. Preserve and use
        // any not yet parsed bytes.
        if ((STILL_OK(s)) && natsConn_expectingNewOp(nc->ps))
            s = _startOp(&rbuf, nc);
    }

    if (s != NATS_OK)
        natsConn_processOpError(nc, s);

    natsConn_release(nc);
}

// natsCon_processInfo is used to parse the info messages sent from the server. This
// function may update the server pool.
natsStatus
natsConn_processInfo(natsConnection *nc, nats_JSON *json)
{
    natsStatus s = NATS_OK;
    bool sendConnect = false;

    // Check that we are in a valid state to process INFO.
    switch (nc->state)
    {
    case NATS_CONN_STATUS_CONNECTED:
        // Nothing else to do here.
        break;

    case NATS_CONN_STATUS_CONNECTING:
        sendConnect = true;
        break;

    default:
        return NATS_UPDATE_ERR_STACK(nats_setError(NATS_PROTOCOL_ERROR,
                                                   "Received INFO in an unexpected connection state: %d", nc->state));
    }

    if (nc->info == NULL)
    {
        IFOK(s, CHECK_NO_MEMORY(
                    nc->info = nats_palloc(nc->lifetimePool, sizeof(natsServerInfo))));
    }

    IFOK(s, nats_unmarshalServerInfo(json, nc->lifetimePool, nc->info));
    if (STILL_OK(s))
    {
        nc->srvVersion.ma = 0;
        nc->srvVersion.mi = 0;
        nc->srvVersion.up = 0;

        if ((nc->info != NULL) && !nats_isCStringEmpty(nc->info->version))
            sscanf(nc->info->version, "%d.%d.%d", &(nc->srvVersion.ma), &(nc->srvVersion.mi), &(nc->srvVersion.up));
    }

    // The array could be empty/not present on initial connect,
    // if advertise is disabled on that server, or servers that
    // did not include themselves in the async INFO protocol.
    // If empty, do not remove the implicit servers from the pool.
    if ((STILL_OK(s)) && !nc->opts->net.ignoreDiscoveredServers && (nc->info->connectURLsCount > 0))
    {
        bool added = false;
        const char *tlsName = NULL;

        if ((nc->cur != NULL) && (nc->cur->url != NULL) && !nats_HostIsIP(nc->cur->url->host))
            tlsName = (const char *)nc->cur->url->host;

        s = natsServers_addNewURLs(nc->servers,
                                   nc->cur->url,
                                   nc->info->connectURLs,
                                   nc->info->connectURLsCount,
                                   tlsName,
                                   &added);
        // if ((STILL_OK(s)) && added && !nc->initc && postDiscoveredServersCb) <>//<>
        //     natsAsyncCb_PostConnHandler(nc, ASYNC_DISCOVERED_SERVERS);
    }

    if (sendConnect)
    {
        // Send the CONNECT protocol to the server.
        IFOK(s, natsConn_sendConnect(nc));
        IFOK(s, natsConn_sendPing(nc));
    }

    // Process the LDM callback after the above. It will cover cases where
    // we have connect URLs and invoke discovered server callback, and case
    // where we don't.
    // if ((STILL_OK(s)) && nc->info.lameDuckMode && postLameDuckCb) <>//<>
    //     natsAsyncCb_PostConnHandler(nc, ASYNC_LAME_DUCK_MODE);

    if (s != NATS_OK)
        s = nats_setError(s, "Invalid protocol: %s", nats_GetLastError(NULL));

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus natsConn_processPing(natsConnection *nc)
{
    return natsConn_asyncWrite(nc, &nats_PONG_CRLF, NULL, NULL);
}

natsStatus natsConn_processPong(natsConnection *nc)
{
    // Check that we are in a valid state to process INFO.
    switch (nc->state)
    {
    case NATS_CONN_STATUS_CONNECTED:
        // Nothing else to do here.
        break;

    case NATS_CONN_STATUS_CONNECTING:
        if (nc->opts->net.connected != NULL)
            nc->opts->net.connected(nc, nc->opts->net.connectedClosure);
        nc->state = NATS_CONN_STATUS_CONNECTED;
        break;

    default:
        return NATS_UPDATE_ERR_STACK(nats_setError(NATS_PROTOCOL_ERROR,
                                                   "Received INFO in an unexpected connection state: %d", nc->state));
    }

    if (nc->pingsOut == 0)
        return nats_setError(NATS_PROTOCOL_ERROR, "%s", "Received unexpected PONG");

    nc->pingsOut--;
    if (nc->pingsOut > nc->opts->proto.maxPingsOut)
        return nats_setDefaultError(NATS_STALE_CONNECTION);

    return NATS_OK;
}

// void natsConn_processErr(natsConnection *nc, char *buf, int bufLen)
// {
//     // char error[256];
//     // bool close       = false;
//     // int  authErrCode = 0;

//     // // Copy the error in this local buffer.
//     // snprintf(error, sizeof(error), "%.*s", bufLen, buf);

//     // // Trim spaces and remove quotes.
//     // nats_NormalizeErr(error);

//     // if (strcasecmp(error, STALE_CONNECTION) == 0)
//     // {
//     //     natsConn_processOpError(nc, NATS_STALE_CONNECTION, false);
//     // }
//     // else if (nats_strcasestr(error, PERMISSIONS_ERR) != NULL)
//     // {
//     //     _processPermissionViolation(nc, error);
//     // }
//     // else if ((authErrCode = _checkAuthError(error)) != 0)
//     // {
//     //     natsConn_Lock(nc);
//     //     close = _processAuthError(nc, authErrCode, error);
//     //     natsConn_Unlock(nc);
//     // }
//     // else
//     // {
//     //     close = true;
//     //     natsConn_Lock(nc);
//     //     nc->err = NATS_ERR;
//     //     snprintf(nc->errStr, sizeof(nc->errStr), "%s", error);
//     //     natsConn_Unlock(nc);
//     // }
//     // if (close)
//     _close(nc, NATS_CONN_STATUS_CLOSED, false, true);
// }
