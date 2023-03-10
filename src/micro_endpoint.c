// Copyright 2021-2023 The NATS Authors
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

#include <ctype.h>

#include "micro.h"
#include "microp.h"
#include "mem.h"

static natsStatus
_newStats(natsMicroserviceEndpointStats **new_stats, const char *name, const char *subject);
static natsStatus
_setLastError(natsMicroserviceEndpointStats *stats, const char *error);
static void
_freeStats(natsMicroserviceEndpointStats *stats);

static bool
_isValidName(const char *name);
static bool
_isValidSubject(const char *subject);
static natsStatus
_lazyInitChildren(natsMicroserviceEndpoint *ep);
static natsStatus
_findAndRemovePreviousChild(int *found_index, natsMicroserviceEndpoint *ep, const char *name);
static void
_handleEndpointRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

natsStatus
natsMicroserviceEndpoint_AddEndpoint(natsMicroserviceEndpoint **new_ep, natsMicroserviceEndpoint *parent, const char *name, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    int index = -1;
    natsMicroserviceEndpoint *ep = NULL;

    // TODO <>/<> return more comprehensive errors
    if (parent == NULL)
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }
    if (cfg != NULL)
    {
        if ((cfg->subject == NULL) || (!_isValidName(name)) || (!_isValidSubject(cfg->subject)))
        {
            return nats_setDefaultError(NATS_INVALID_ARG);
        }
    }

    IFOK(s, _lazyInitChildren(parent));
    IFOK(s, _newMicroserviceEndpoint(&ep, parent->m, name, cfg));
    IFOK(s, _findAndRemovePreviousChild(&index, parent, name));
    IFOK(s, _microserviceEndpointList_Put(parent->children, index, ep));
    IFOK(s, natsMicroserviceEndpoint_Start(ep));

    if (s == NATS_OK)
    {
        if (new_ep != NULL)
            *new_ep = ep;
    }
    else
    {
        natsMicroserviceEndpoint_Destroy(ep);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMicroserviceEndpoint_Start(natsMicroserviceEndpoint *ep)
{
    if ((ep->subject == NULL) || (ep->config == NULL) || (ep->config->handler == NULL))
        // nothing to do
        return NATS_OK;

    return natsConnection_QueueSubscribe(&ep->sub, ep->m->nc, ep->subject,
                                         natsMicroserviceQueueGroup, _handleEndpointRequest, ep);
}

// TODO <>/<> COPY FROM GO
natsStatus
natsMicroserviceEndpoint_Stop(natsMicroserviceEndpoint *ep)
{
    natsStatus s = NATS_OK;

    // TODO <>/<> review locking for modifying endpoints, may not be necessary
    // or ep may need its own lock (stats).

    // This is a rare call, usually happens at the initialization of the
    // microservice, so it's ok to lock for the duration of the function, may
    // not be necessary at all but will not hurt.
    natsMutex_Lock(ep->m->mu);

    if (ep->sub != NULL)
    {
        s = natsSubscription_Drain(ep->sub);
    }
    if (s == NATS_OK)
    {
        ep->sub = NULL;
        ep->stopped = true;
        // TODO <>/<> unsafe
        ep->stats->stopped = true;
    }

    natsMutex_Unlock(ep->m->mu);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_freeMicroserviceEndpoint(natsMicroserviceEndpoint *ep)
{
    // ignore ep->children, must be taken care of by the caller.
    _freeStats(ep->stats);
    NATS_FREE(ep->name);
    NATS_FREE(ep->subject);
    NATS_FREE(ep);
    return NATS_OK;
}

natsStatus
natsMicroserviceEndpoint_Destroy(natsMicroserviceEndpoint *ep)
{
    natsStatus s = NATS_OK;

    if (ep == NULL)
        return NATS_OK;

    IFOK(s, _destroyMicroserviceEndpointList(ep->children));
    IFOK(s, natsMicroserviceEndpoint_Stop(ep));
    IFOK(s, _freeMicroserviceEndpoint(ep));

    return NATS_UPDATE_ERR_STACK(s);
}

static bool
_isValidName(const char *name)
{
    int i;
    int len = (int)strlen(name);

    if (len == 0)
        return false;

    for (i = 0; i < len; i++)
    {
        if (!isalnum(name[i]) && (name[i] != '_') && (name[i] != '-'))
            return false;
    }
    return true;
}

static bool
_isValidSubject(const char *subject)
{
    int i;
    int len = (int)strlen(subject);

    if (len == 0)
        return false;

    for (i = 0; i < len - 1; i++)
    {
        if ((subject[i] == ' ') || (subject[i] == '>'))
            return false;
    }

    if ((subject[i] == ' '))
        return false;

    return true;
}

static natsStatus
_lazyInitChildren(natsMicroserviceEndpoint *ep)
{
    if (ep->children == NULL)
    {
        return _newMicroserviceEndpointList(&ep->children);
    }
    return NATS_OK;
}

static natsStatus
_findAndRemovePreviousChild(int *found_index, natsMicroserviceEndpoint *ep, const char *name)
{
    natsMicroserviceEndpoint *found = _microserviceEndpointList_Find(ep->children, name, found_index);
    if (found != NULL)
    {
        return natsMicroserviceEndpoint_Destroy(found);
    }
    return NATS_OK;
}

natsStatus
_newMicroserviceEndpoint(natsMicroserviceEndpoint **new_endpoint, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceEndpoint *ep = NULL;
    // TODO <>/<> do I really need to duplicate name, subject?
    char *dup_name = NULL;
    char *dup_subject = NULL;

    IFOK(s, NATS_CALLOCS(&ep, 1, sizeof(natsMicroserviceEndpoint)));
    IFOK(s, NATS_STRDUPS(&dup_name, name));
    if ((cfg != NULL) && (cfg->subject != NULL))
        IFOK(s, NATS_STRDUPS(&dup_subject, cfg->subject));
    IFOK(s, _newStats(&ep->stats, dup_name, dup_subject));

    if (s == NATS_OK)
    {
        ep->m = m;
        ep->name = dup_name;
        ep->subject = dup_subject;
        ep->config = cfg;
        *new_endpoint = ep;
    }
    else
    {
        _freeStats(ep->stats);
        NATS_FREE(ep);
        NATS_FREE(dup_name);
        NATS_FREE(dup_subject);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_handleEndpointRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroserviceEndpoint *ep = (natsMicroserviceEndpoint *)closure;
    // natsMicroserviceEndpointStats *stats = &ep->stats;
    natsMicroserviceEndpointConfig *cfg = ep->config;
    natsMicroserviceRequest *req = NULL;
    natsMicroserviceRequestHandler handler = cfg->handler;

    if (handler == NULL)
    {
        // This is a bug, we should not have received a message on this
        // subscription.
        natsMsg_Destroy(msg);
        return;
    }

    s = _newMicroserviceRequest(&req, msg);
    if (s == NATS_OK)
    {
        req->ep = ep;
        handler(ep->m, req);
    }

    // Update stats
    // natsMutex_Lock(ep->mu);
    // stats->numRequests++;
    // natsMutex_Unlock(ep->mu);

    _freeMicroserviceRequest(req);
    natsMsg_Destroy(msg);
}

static natsStatus
_newStats(natsMicroserviceEndpointStats **new_stats, const char *name, const char *subject)
{
    natsStatus s = NATS_OK;
    natsMicroserviceEndpointStats *stats = NULL;

    stats = (natsMicroserviceEndpointStats *)NATS_CALLOC(1, sizeof(natsMicroserviceEndpointStats));
    s = (stats != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        stats->name = name;
        stats->subject = subject;
        *new_stats = stats;
        return NATS_OK;
    }

    NATS_FREE(stats);
    return NATS_UPDATE_ERR_STACK(s);
}

// All locking is to be done by the caller.
static natsStatus
_setLastError(natsMicroserviceEndpointStats *stats, const char *error)
{
    natsStatus s = NATS_OK;

    if (stats->last_error != NULL)
        NATS_FREE(stats->last_error);

    if (nats_IsStringEmpty(error))
    {
        stats->last_error = NULL;
        return NATS_OK;
    }

    stats->last_error = NATS_STRDUP(error);
    s = (stats->last_error != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_freeStats(natsMicroserviceEndpointStats *stats)
{
    if (stats == NULL)
        return;

    if (stats->last_error != NULL)
        NATS_FREE(stats->last_error);

    NATS_FREE(stats);
}
