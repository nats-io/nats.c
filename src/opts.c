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

#include <string.h>

#include "natsp.h"

#include "conn.h"
#include "opts.h"

natsStatus
nats_SetURL(natsOptions *opts, const char *url)
{
    natsStatus s = NATS_OK;

    CHECK_OPTIONS(opts, 0);

    if (opts->url != NULL)
        opts->url = NULL;

    if (url != NULL)
        s = nats_Trim(&(opts->url), opts->pool, url);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_SetServers(natsOptions *opts, const char **servers, int serversCount)
{
    natsStatus s = NATS_OK;
    int i;

    CHECK_OPTIONS(opts,
                  (((servers != NULL) && (serversCount <= 0)) || ((servers == NULL) && (serversCount != 0))));

    if (servers != NULL)
    {
        opts->servers = (char **)nats_palloc(opts->pool, serversCount * sizeof(char *));
        if (opts->servers == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i = 0; (STILL_OK(s)) && (i < serversCount); i++)
        {
            s = nats_Trim(&(opts->servers[i]), opts->pool, servers[i]);
            if (STILL_OK(s))
                opts->serversCount++;
        }
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_SetNoRandomize(natsOptions *opts, bool noRandomize)
{
    natsStatus s = NATS_OK;

    CHECK_OPTIONS(opts, 0);

    opts->net.noRandomize = noRandomize;

    return s;
}

natsStatus
nats_SetTimeout(natsOptions *opts, int64_t timeout)
{
    CHECK_OPTIONS(opts, (timeout < 0));

    opts->net.timeout = timeout;

    return NATS_OK;
}

natsStatus
nats_SetName(natsOptions *opts, const char *name)
{
    natsStatus s = NATS_OK;

    CHECK_OPTIONS(opts, 0);

    opts->name = NULL;
    if (name != NULL)
    {
        opts->name = nats_pstrdupC(opts->pool, name);
        if (opts->name == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    return s;
}

natsStatus
nats_SetUserInfo(natsOptions *opts, const char *user, const char *password)
{
    natsStatus s = NATS_OK;

    CHECK_OPTIONS(opts, 0);

    opts->user = NULL;
    opts->password = NULL;
    if (user != NULL)
    {
        opts->user = nats_pstrdupC(opts->pool, user);
        if (opts->user == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    if ((STILL_OK(s)) && (password != NULL))
    {
        opts->password = nats_pstrdupC(opts->pool, password);
        if (opts->password == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    return s;
}

natsStatus
nats_SetVerbose(natsOptions *opts, bool verbose)
{
    CHECK_OPTIONS(opts, 0);

    opts->proto.verbose = verbose;

    return NATS_OK;
}

natsStatus
nats_SetPedantic(natsOptions *opts, bool pedantic)
{
    CHECK_OPTIONS(opts, 0);

    opts->proto.pedantic = pedantic;

    return NATS_OK;
}

natsStatus
nats_SetPingInterval(natsOptions *opts, int64_t interval)
{
    CHECK_OPTIONS(opts, 0);

    opts->proto.pingInterval = interval;

    return NATS_OK;
}

natsStatus
nats_SetMaxPingsOut(natsOptions *opts, int maxPignsOut)
{
    CHECK_OPTIONS(opts, 0);

    opts->proto.maxPingsOut = maxPignsOut;

    return NATS_OK;
}

natsStatus
nats_SetAllowReconnect(natsOptions *opts, bool allow)
{
    CHECK_OPTIONS(opts, 0);

    opts->net.allowReconnect = allow;

    return NATS_OK;
}

natsStatus
nats_SetMaxReconnect(natsOptions *opts, int maxReconnect)
{
    CHECK_OPTIONS(opts, 0);

    opts->net.maxReconnect = maxReconnect;

    return NATS_OK;
}

natsStatus
nats_SetReconnectWait(natsOptions *opts, int64_t reconnectWait)
{
    CHECK_OPTIONS(opts, (reconnectWait < 0));

    opts->net.reconnectWait = reconnectWait;

    return NATS_OK;
}

natsStatus
nats_SetReconnectJitter(natsOptions *opts, int64_t jitter, int64_t jitterTLS)
{
    CHECK_OPTIONS(opts, ((jitter < 0) || (jitterTLS < 0)));

    opts->net.reconnectJitter = jitter;
    opts->net.reconnectJitterTLS = jitterTLS;

    return NATS_OK;
}


natsStatus
nats_SetIgnoreDiscoveredServers(natsOptions *opts, bool ignore)
{
    CHECK_OPTIONS(opts, 0);

    opts->net.ignoreDiscoveredServers = ignore;

    return NATS_OK;
}

natsStatus
nats_SetIPResolutionOrder(natsOptions *opts, int order)
{
    CHECK_OPTIONS(opts, ((order != 0) && (order != 4) && (order != 6) && (order != 46) && (order != 64)));

    opts->net.orderIP = order;

    return NATS_OK;
}

natsStatus
nats_SetNoEcho(natsOptions *opts, bool noEcho)
{
    CHECK_OPTIONS(opts, 0);
    opts->proto.noEcho = noEcho;

    return NATS_OK;
}

natsStatus
nats_SetFailRequestsOnDisconnect(natsOptions *opts, bool failRequests)
{
    CHECK_OPTIONS(opts, 0);
    opts->net.failRequestsOnDisconnect = failRequests;

    return NATS_OK;
}

natsStatus
nats_SetWriteDeadline(natsOptions *opts, int64_t deadline)
{
    CHECK_OPTIONS(opts, (deadline < 0));

    opts->net.writeDeadline = deadline;

    return NATS_OK;
}

natsStatus
nats_SetDisableNoResponders(natsOptions *opts, bool disabled)
{
    CHECK_OPTIONS(opts, 0);

    opts->proto.disableNoResponders = disabled;

    return NATS_OK;
}

natsStatus
nats_SetOnConnected(natsOptions *opts, natsOnConnectionEventF cb, void *closure)
{
    CHECK_OPTIONS(opts, 0);

    opts->net.connected = cb;
    opts->net.connectedClosure = closure;

    return NATS_OK;
}

natsStatus
nats_SetOnConnectionClosed(natsOptions *opts, natsOnConnectionEventF cb, void *closure)
{
    CHECK_OPTIONS(opts, 0);

    opts->net.closed = cb;
    opts->net.closedClosure = closure;

    return NATS_OK;
}

natsStatus
nats_createOptions(natsOptions **newOpts, natsPool *pool)
{
    natsStatus s;
    natsOptions *opts = NULL;

    // Ensure the library is loaded
    s = nats_open();
    IFOK(s, CHECK_NO_MEMORY(opts = nats_palloc(pool, sizeof(natsOptions))));
    if (s != NATS_OK)
        return s;

    opts->pool = pool;

    opts->net.allowReconnect = true;
    opts->net.maxReconnect = NATS_OPTS_DEFAULT_MAX_RECONNECT;
    opts->net.reconnectJitter = NATS_OPTS_DEFAULT_RECONNECT_JITTER;
    opts->net.reconnectJitterTLS = NATS_OPTS_DEFAULT_RECONNECT_JITTER_TLS;
    opts->net.reconnectWait = NATS_OPTS_DEFAULT_RECONNECT_WAIT;
    opts->net.timeout = NATS_OPTS_DEFAULT_TIMEOUT;

    opts->proto.maxPingsOut = NATS_OPTS_DEFAULT_MAX_PING_OUT;
    opts->proto.pingInterval = NATS_OPTS_DEFAULT_PING_INTERVAL;

    opts->secure.secure = false;

    opts->mem = nats_defaultMemOptions;

    *newOpts = opts;

    return NATS_OK;
}

natsOptions *
nats_GetDefaultOptions(void)
{
    if (NOT_OK(nats_open()))
        return NULL;
    natsOptions *opts = NULL;
    nats_createOptions(&opts, nats_globalPool());
    return opts;
}

natsStatus
nats_cloneOptions(natsOptions **newOptions, natsPool *pool, natsOptions *opts)
{
    natsStatus s = NATS_OK;
    natsOptions *cloned = NULL;

    if ((s = nats_createOptions(&cloned, pool)) != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // Make a blind copy first...

    size_t off  = offsetof(natsOptions, __memcpy_from_here);
    memset((char *)cloned, 0, off);
    memcpy((char *)cloned + off, (char *)opts+off, sizeof(natsOptions)-off);
    cloned->pool = pool;

    if (opts->name != NULL)
        s = nats_SetName(cloned, opts->name);

    if ((STILL_OK(s)) && (opts->url != NULL))
        s = nats_SetURL(cloned, opts->url);

    if ((STILL_OK(s)) && (opts->servers != NULL))
        s = nats_SetServers(cloned,
                                   (const char **)opts->servers,
                                   opts->serversCount);

    if ((STILL_OK(s)) && (opts->user != NULL))
        s = nats_SetUserInfo(cloned, opts->user, opts->password);

    if (s != NATS_OK)
    {
        cloned = NULL;
        NATS_UPDATE_ERR_STACK(s);
    }

    *newOptions = cloned;
    return NATS_OK;
}
