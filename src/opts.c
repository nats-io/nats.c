// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <string.h>

#include "mem.h"
#include "opts.h"

#define LOCK_AND_CHECK_OPTIONS(o, c) \
    if (((o) == NULL) || ((c))) \
        return NATS_INVALID_ARG; \
    natsMutex_Lock((o)->mu);

#define UNLOCK_OPTS(o) natsMutex_Unlock((o)->mu)

natsStatus
natsOptions_SetURL(natsOptions *opts, const char* url)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if (opts->url != NULL)
    {
        NATS_FREE(opts->url);
        opts->url = NULL;
    }

    if (url != NULL)
    {
        opts->url = NATS_STRDUP(url);
        if (opts->url == NULL)
            s = NATS_NO_MEMORY;
    }

    UNLOCK_OPTS(opts);

    return s;
}

static void
_freeServers(natsOptions *opts)
{
    int i;

    if ((opts->servers == NULL) || (opts->serversCount == 0))
        return;

    for (i = 0; i < opts->serversCount; i++)
        NATS_FREE(opts->servers[i]);

    NATS_FREE(opts->servers);

    opts->servers       = NULL;
    opts->serversCount  = 0;
}

natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount)
{
    natsStatus  s = NATS_OK;
    int         i;

    LOCK_AND_CHECK_OPTIONS(opts,
                           (((servers != NULL) && (serversCount <= 0))
                            || ((servers == NULL) && (serversCount != 0))));

    _freeServers(opts);

    if (servers != NULL)
    {
        opts->servers = (char**) NATS_CALLOC(serversCount, sizeof(char*));
        if (opts->servers == NULL)
            s = NATS_NO_MEMORY;

        for (i = 0; i < serversCount; i++)
        {
            opts->servers[i] = (char*) NATS_STRDUP(servers[i]);
            if (opts->servers[i] == NULL)
                s = NATS_NO_MEMORY;
            else
                opts->serversCount++;
        }
    }

    if (s != NATS_OK)
        _freeServers(opts);

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->noRandomize = noRandomize;

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout)
{
    LOCK_AND_CHECK_OPTIONS(opts, (timeout < 0));

    opts->timeout = timeout;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}


natsStatus
natsOptions_SetName(natsOptions *opts, const char *name)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    NATS_FREE(opts->name);
    opts->name = NULL;
    if (name != NULL)
    {
        opts->name = NATS_STRDUP(name);
        if (opts->name == NULL)
            s = NATS_NO_MEMORY;
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->verbose = verbose;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->pedantic = pedantic;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->pingInterval = interval;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPignsOut)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->maxPingsOut = maxPignsOut;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}


natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->allowReconnect = allow;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->maxReconnect = maxReconnect;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait)
{
    LOCK_AND_CHECK_OPTIONS(opts, (reconnectWait < 0));

    opts->reconnectWait = reconnectWait;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending)
{
    LOCK_AND_CHECK_OPTIONS(opts, (maxPending <= 0));

    opts->maxPendingMsgs = maxPending;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->asyncErrCb = errHandler;
    opts->asyncErrCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->closedCb = closedCb;
    opts->closedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->disconnectedCb = disconnectedCb;
    opts->disconnectedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->reconnectedCb = reconnectedCb;
    opts->reconnectedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}


static void
_freeOptions(natsOptions *opts)
{
    if (opts == NULL)
        return;

    NATS_FREE(opts->url);
    NATS_FREE(opts->name);
    _freeServers(opts);
    NATS_FREE(opts);
}

natsStatus
natsOptions_Create(natsOptions **newOpts)
{
    natsOptions *opts = (natsOptions*) NATS_CALLOC(1, sizeof(natsOptions));

    if (opts == NULL)
        return NATS_NO_MEMORY;

    if (natsMutex_Create(&(opts->mu)) != NATS_OK)
    {
        NATS_FREE(opts);
        return NATS_NO_MEMORY;
    }

    opts->allowReconnect = true;
    opts->maxReconnect   = NATS_OPTS_DEFAULT_MAX_RECONNECT;
    opts->reconnectWait  = NATS_OPTS_DEFAULT_RECONNECT_WAIT;
    opts->pingInterval   = NATS_OPTS_DEFAULT_PING_INTERVAL;
    opts->maxPingsOut    = NATS_OPTS_DEFAULT_MAX_PING_OUT;
    opts->maxPendingMsgs = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;
    opts->timeout        = NATS_OPTS_DEFAULT_TIMEOUT;

    *newOpts = opts;

    return NATS_OK;
}

natsOptions*
natsOptions_clone(natsOptions *opts)
{
    natsStatus  s       = NATS_OK;
    natsOptions *cloned = NULL;
    int         muSize;

    if (natsOptions_Create(&cloned) != NATS_OK)
        return NULL;

    natsMutex_Lock(opts->mu);

    muSize = sizeof(cloned->mu);

    // Make a blind copy first...
    memcpy((char*)cloned + muSize, (char*)opts + muSize,
           sizeof(natsOptions) - muSize);

    // Then remove all refs to strings, so that if we fail while
    // strduping them, and free the cloned, we don't free the strings
    // from the original.
    cloned->name    = NULL;
    cloned->servers = NULL;
    cloned->url     = NULL;

    if (opts->name != NULL)
    {
        cloned->name = NATS_STRDUP(opts->name);
        if (cloned->name == NULL)
            s = NATS_NO_MEMORY;
    }
    if ((s == NATS_OK) && (opts->url != NULL))
    {
        cloned->url = NATS_STRDUP(opts->url);
        if (cloned->url == NULL)
            s = NATS_NO_MEMORY;
    }
    if ((s == NATS_OK) && (opts->servers != NULL))
    {
        cloned->servers = NATS_CALLOC(opts->serversCount, sizeof(char *));
        if (cloned->servers == NULL)
            s = NATS_NO_MEMORY;
        for (int i=0; (s == NATS_OK) && (i<opts->serversCount); i++)
        {
            cloned->servers[i] = NATS_STRDUP(opts->servers[i]);
            if (cloned->servers[i] == NULL)
                s = NATS_NO_MEMORY;
        }
    }

    if (s != NATS_OK)
    {
        _freeOptions(cloned);
        cloned = NULL;
    }

    natsMutex_Unlock(opts->mu);

    return cloned;
}

void
natsOptions_Destroy(natsOptions *opts)
{
    if (opts == NULL)
        return;

    _freeOptions(opts);
}

