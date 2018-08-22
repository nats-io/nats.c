// Copyright 2018 The NATS Authors
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

#include "copts.h"
#include "../opts.h"

static void
_stanConnOpts_free(stanConnOptions *opts)
{
    if (opts == NULL)
        return;

    NATS_FREE(opts->url);
    NATS_FREE(opts->discoveryPrefix);
    natsOptions_Destroy(opts->ncOpts);
    natsMutex_Destroy(opts->mu);
    NATS_FREE(opts);
}

natsStatus
stanConnOptions_Create(stanConnOptions **newOpts)
{
    natsStatus      s     = NATS_OK;
    stanConnOptions *opts = NULL;

    // Ensure the library is loaded
    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    opts = (stanConnOptions*) NATS_CALLOC(1, sizeof(stanConnOptions));
    if (opts == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (natsMutex_Create(&(opts->mu)) != NATS_OK)
    {
        NATS_FREE(opts);
        return NATS_UPDATE_ERR_STACK(NATS_NO_MEMORY);
    }

    DUP_STRING(s, opts->url, NATS_DEFAULT_URL);
    IF_OK_DUP_STRING(s, opts->discoveryPrefix, STAN_CONN_OPTS_DEFAULT_DISCOVERY_PREFIX);
    if (s == NATS_OK)
    {
        opts->pubAckTimeout = STAN_CONN_OPTS_DEFAULT_PUB_ACK_TIMEOUT;
        opts->connTimeout = STAN_CONN_OPTS_DEFAULT_CONN_TIMEOUT;
        opts->maxPubAcksInflight = STAN_CONN_OPTS_DEFAULT_MAX_PUB_ACKS_INFLIGHT;
        opts->maxPubAcksInFlightPercentage = STAN_CONN_OPTS_DEFAULT_MAX_PUB_ACKS_INFLIGHT_PERCENTAGE;
        opts->pingInterval = STAN_CONN_OPTS_DEFAULT_PING_INTERVAL;
        opts->pingMaxOut = STAN_CONN_OPTS_DEFAULT_PING_MAX_OUT;
    }

    if (s == NATS_OK)
        *newOpts = opts;
    else
        _stanConnOpts_free(opts);

    return s;
}

natsStatus
stanConnOptions_SetURL(stanConnOptions *opts, const char *url)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, ((url == NULL) || (url[0] == '\0')));

    if (opts->url != NULL)
    {
        NATS_FREE(opts->url);
        opts->url = NULL;
    }
    DUP_STRING(s, opts->url, url);

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
stanConnOptions_SetNATSOptions(stanConnOptions *opts, natsOptions *nOpts)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if (opts->ncOpts != NULL)
    {
        natsOptions_Destroy(opts->ncOpts);
        opts->ncOpts = NULL;
    }
    if (nOpts != NULL)
    {
        opts->ncOpts = natsOptions_clone(nOpts);
        if (opts->ncOpts == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
stanConnOptions_SetConnectionWait(stanConnOptions *opts, int64_t wait)
{
    LOCK_AND_CHECK_OPTIONS(opts, (wait <= 0));

    opts->connTimeout = wait;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanConnOptions_SetPubAckWait(stanConnOptions *opts, int64_t wait)
{
    LOCK_AND_CHECK_OPTIONS(opts, (wait <= 0));

    opts->pubAckTimeout = wait;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanConnOptions_SetDiscoveryPrefix(stanConnOptions *opts, const char *prefix)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, ((prefix == NULL) || (prefix[0]=='\0')));

    if (opts->discoveryPrefix != NULL)
    {
        NATS_FREE(opts->discoveryPrefix);
        opts->discoveryPrefix = NULL;
    }
    DUP_STRING(s, opts->discoveryPrefix, prefix);

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
stanConnOptions_SetMaxPubAcksInflight(stanConnOptions *opts, int maxPubAcksInflight, float percentage)
{
    LOCK_AND_CHECK_OPTIONS(opts, ((maxPubAcksInflight <= 0) || (percentage <= 0.0) || (percentage > 1.0)));

    opts->maxPubAcksInflight = maxPubAcksInflight;
    opts->maxPubAcksInFlightPercentage = percentage;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanConnOptions_SetPings(stanConnOptions *opts, int interval, int maxOut)
{
    if (testAllowMillisecInPings)
    {
        if ((interval == 0) || (maxOut < 2))
            return nats_setDefaultError(NATS_INVALID_ARG);
    }
    else
    {
        if ((interval <= 0) || (maxOut < 2))
            return nats_setDefaultError(NATS_INVALID_ARG);
    }

    natsMutex_Lock(opts->mu);

    opts->pingInterval = interval;
    opts->pingMaxOut = maxOut;

    natsMutex_Unlock(opts->mu);

    return NATS_OK;
}

natsStatus
stanConnOptions_SetConnectionLostHandler(stanConnOptions *opts, stanConnectionLostHandler handler, void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->connectionLostCB = handler;
    opts->connectionLostCBClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanConnOptions_clone(stanConnOptions **clonedOpts, stanConnOptions *opts)
{
    natsStatus      s       = NATS_OK;
    stanConnOptions *cloned = NULL;
    int             muSize;

    s = stanConnOptions_Create(&cloned);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // The Create call sets (strdup) the default URL and discovery prefix.
    // So free those now.
    NATS_FREE(cloned->url);
    cloned->url = NULL;
    NATS_FREE(cloned->discoveryPrefix);
    cloned->discoveryPrefix = NULL;

    natsMutex_Lock(opts->mu);

    muSize = sizeof(cloned->mu);

    // Make a blind copy first...
    memcpy((char*)cloned + muSize, (char*)opts + muSize,
           sizeof(stanConnOptions) - muSize);

    // Then remove all pointers, so that if we fail while
    // copying them, and free the cloned, we don't free the pointers
    // from the original.
    cloned->url             = NULL;
    cloned->discoveryPrefix = NULL;
    cloned->ncOpts          = NULL;

    s = stanConnOptions_SetURL(cloned, opts->url);
    if (s == NATS_OK)
        s = stanConnOptions_SetDiscoveryPrefix(cloned, opts->discoveryPrefix);
    if (s == NATS_OK)
        s = stanConnOptions_SetNATSOptions(cloned, opts->ncOpts);

    if (s == NATS_OK)
        *clonedOpts = cloned;
    else
        _stanConnOpts_free(cloned);

    natsMutex_Unlock(opts->mu);

    return s;
}

void
stanConnOptions_Destroy(stanConnOptions *opts)
{
    if (opts == NULL)
        return;

    _stanConnOpts_free(opts);
}
