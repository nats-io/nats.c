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

// TODO review includes
#include <ctype.h>
#include <stdarg.h>

#include "microp.h"
#include "mem.h"
#include "conn.h"

static bool
_isValidName(const char *name);
static bool
_isValidSubject(const char *subject);

static natsStatus
_newEndpoint(natsMicroserviceEndpoint **__new_endpoint, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg);
static natsStatus
_startEndpoint(natsMicroserviceEndpoint *endpoint);

static natsStatus
_newRequest(natsMicroserviceRequest **new_request, natsMsg *msg);
static void
_freeRequest(natsMicroserviceRequest *req);
static void
_handleEndpointRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

natsStatus
natsMicroservice_AddEndpoint(natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    int i;
    int add = -1;
    natsMicroserviceEndpoint **new_endpoints = m->endpoints;
    int new_num_endpoints = m->num_endpoints;
    natsMicroserviceEndpoint *new_endpoint = NULL;

    // TODO return more comprehensive errors
    if ((m == NULL) || (m->mu == NULL) || (cfg == NULL) || (cfg->subject == NULL) ||
        (!_isValidName(name)) || (!_isValidSubject(cfg->subject)))
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }

    // This is a rare call, usually happens at the initialization of the
    // microservice, so it's ok to lock for the duration of the function, may
    // not be necessary at all but will not hurt.
    natsMutex_Lock(m->mu);

    for (i = 0; i < m->num_endpoints; i++)
    {
        if (strcmp(m->endpoints[i]->config.subject, cfg->subject) == 0)
        {
            add = i;
            break;
        }
    }
    if (add == -1)
    {
        new_endpoints = (natsMicroserviceEndpoint **)
            NATS_REALLOC(m->endpoints, sizeof(natsMicroserviceEndpoint *) * (m->num_endpoints + 1));
        s = (new_endpoints != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
        if (s == NATS_OK)
        {
            add = m->num_endpoints;
            new_num_endpoints = m->num_endpoints + 1;
        }
    }

    IFOK(s, _newEndpoint(&new_endpoint, m, name, cfg));
    IFOK(s, _startEndpoint(new_endpoint));

    if (s == NATS_OK)
    {
        new_endpoints[add] = new_endpoint;
        m->endpoints = new_endpoints;
        m->num_endpoints = new_num_endpoints;
    }
    else
    {
        _freeMicroserviceEndpoint(new_endpoint);
    }

    natsMutex_Unlock(m->mu);
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
_newEndpoint(natsMicroserviceEndpoint **__new_endpoint, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceEndpoint *ep = NULL;
    char *dup_name = NULL;
    char *dup_subject = NULL;

    ep = (natsMicroserviceEndpoint *)NATS_CALLOC(1, sizeof(natsMicroserviceEndpoint));
    s = (ep != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        dup_name = NATS_STRDUP(name);
        s = (dup_name != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    }
    if (s == NATS_OK)
    {
        dup_subject = NATS_STRDUP(cfg->subject);
        s = (dup_subject != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    }
    if (s == NATS_OK)
    {
        ep->m = m;
        ep->stats.name = dup_name;
        ep->stats.subject = dup_subject;
        ep->config = *cfg;
        ep->config.subject = dup_subject;
    }
    if (s == NATS_OK)
    {
        *__new_endpoint = ep;
        return NATS_OK;
    }

    NATS_FREE(dup_name);
    NATS_FREE(dup_subject);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_startEndpoint(natsMicroserviceEndpoint *endpoint)
{
    if (endpoint->config.handler == NULL)
        // nothing to do
        return NATS_OK;

    return natsConnection_QueueSubscribe(&endpoint->sub, endpoint->m->nc, endpoint->config.subject,
                                      natsMicroserviceQueueGroup, _handleEndpointRequest, endpoint);
}

// TODO COPY FROM GO
natsStatus
_stopMicroserviceEndpoint(natsMicroserviceEndpoint *endpoint)
{
    natsStatus s = NATS_OK;

    // TODO review locking for modifying endpoints, may not be necessary or
    // endpoint may need its own lock (stats).

    // This is a rare call, usually happens at the initialization of the
    // microservice, so it's ok to lock for the duration of the function, may
    // not be necessary at all but will not hurt.
    natsMutex_Lock(endpoint->m->mu);

    if (endpoint->sub != NULL)
    {
        s = natsSubscription_Drain(endpoint->sub);
    }
    if (s == NATS_OK)
    {
        endpoint->sub = NULL;
        endpoint->stopped = true;
        endpoint->stats.stopped = true;
    }

    natsMutex_Unlock(endpoint->m->mu);
    return NATS_UPDATE_ERR_STACK(s);
}

void _freeMicroserviceEndpoint(natsMicroserviceEndpoint *endpoint)
{
    if (endpoint == NULL)
        return;

    // The struct fields are declared as const char * for the external API, but
    // we know that the strings were duplicated and need to be freed.
    // endpoint->config.name is the same as endpoint->stats.name, no need to
    // free it.
    NATS_FREE((char *)endpoint->stats.name);
    NATS_FREE((char *)endpoint->stats.subject);

    NATS_FREE(endpoint);
}

static void
_handleEndpointRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroserviceEndpoint *endpoint = (natsMicroserviceEndpoint *)closure;
    // natsMicroserviceEndpointStats *stats = &endpoint->stats;
    natsMicroserviceEndpointConfig *cfg = &endpoint->config;
    natsMicroserviceRequest *req = NULL;
    natsMicroserviceRequestHandler handler = cfg->handler;
    void *handlerClosure = cfg->closure;
    // const char *errorString = "";

    if (handler == NULL)
    {
        // This is a bug, we should not have received a message on this
        // subscription.
        natsMsg_Destroy(msg);
        return;
    }

    s = _newRequest(&req, msg);
    if (s == NATS_OK)
    {
        handler(endpoint->m, req, handlerClosure);
        // errorString = req->err;
    }
    else
    {
        // errorString = natsStatus_GetText(s);
    }

    // Update stats
    // natsMutex_Lock(endpoint->mu);
    // stats->numRequests++;
    // natsMutex_Unlock(endpoint->mu);

    _freeRequest(req);
    natsMsg_Destroy(msg);
}

static natsStatus
_newRequest(natsMicroserviceRequest **new_request, natsMsg *msg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceRequest *req = NULL;

    req = (natsMicroserviceRequest *)NATS_CALLOC(1, sizeof(natsMicroserviceRequest));
    s = (req != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        req->msg = msg;
        *new_request = req;
        return NATS_OK;
    }

    _freeRequest(req);
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_freeRequest(natsMicroserviceRequest *req)
{
    if (req == NULL)
        return;

    NATS_FREE(req->err);
    NATS_FREE(req);
}

natsMsg* 
natsMicroserviceRequest_GetMsg(natsMicroserviceRequest *req)
{
    return req != NULL ? req->msg : NULL;
}
