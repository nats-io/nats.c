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
free_endpoint(microEndpoint *ep);
static bool
is_valid_name(const char *name);
static bool
is_valid_subject(const char *subject);
static void
handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

natsStatus
micro_new_endpoint(microEndpoint **new_endpoint, microService *m, microEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    microEndpoint *ep = NULL;
    microEndpointConfig *clone_cfg = NULL;

    if (!is_valid_name(cfg->name) || (cfg->handler == NULL))
    {
        s = NATS_INVALID_ARG;
    }
    if ((s == NATS_OK) && (cfg->subject != NULL) && !is_valid_subject(cfg->subject))
    {
        s = NATS_INVALID_ARG;
    }
    IFOK(s, micro_clone_endpoint_config(&clone_cfg, cfg));
    IFOK(s, NATS_CALLOCS(&ep, 1, sizeof(microEndpoint)));
    IFOK(s, natsMutex_Create(&ep->mu));
    if (s == NATS_OK)
    {
        ep->m = m;
        ep->subject = NATS_STRDUP(nats_IsStringEmpty(cfg->subject) ? cfg->name : cfg->subject);
        ep->config = clone_cfg;
        *new_endpoint = ep;
    }
    else
    {
        micro_free_cloned_endpoint_config(clone_cfg);
        free_endpoint(ep);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
micro_clone_endpoint_config(microEndpointConfig **out, microEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    microEndpointConfig *new_cfg = NULL;
    microSchema *new_schema = NULL;
    char *new_name = NULL;
    char *new_subject = NULL;
    char *new_request = NULL;
    char *new_response = NULL;

    if (out == NULL || cfg == NULL)
        return NATS_INVALID_ARG;

    s = NATS_CALLOCS(&new_cfg, 1, sizeof(microEndpointConfig));
    if (s == NATS_OK && cfg->name != NULL)
    {
        DUP_STRING(s, new_name, cfg->name);
    }
    if (s == NATS_OK && cfg->subject != NULL)
    {
        DUP_STRING(s, new_subject, cfg->subject);
    }
    if (s == NATS_OK && cfg->schema != NULL)
    {
        s = NATS_CALLOCS(&new_schema, 1, sizeof(microSchema));
        if (s == NATS_OK && cfg->schema->request != NULL)
        {
            DUP_STRING(s, new_request, cfg->schema->request);
        }
        if (s == NATS_OK && cfg->schema->response != NULL)
        {
            DUP_STRING(s, new_response, cfg->schema->response);
        }
        if (s == NATS_OK)
        {
            new_schema->request = new_request;
            new_schema->response = new_response;
        }
    }
    if (s == NATS_OK)
    {
        memcpy(new_cfg, cfg, sizeof(microEndpointConfig));
        new_cfg->schema = new_schema;
        new_cfg->name = new_name;
        new_cfg->subject = new_subject;
        *out = new_cfg;
    }
    else
    {
        NATS_FREE(new_cfg);
        NATS_FREE(new_schema);
        NATS_FREE(new_name);
        NATS_FREE(new_subject);
        NATS_FREE(new_request);
        NATS_FREE(new_response);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

void micro_free_cloned_endpoint_config(microEndpointConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->name);
    NATS_FREE((char *)cfg->subject);
    NATS_FREE((char *)cfg->schema->request);
    NATS_FREE((char *)cfg->schema->response);

    NATS_FREE(cfg->schema);
    NATS_FREE(cfg);
}

natsStatus
micro_start_endpoint(microEndpoint *ep)
{
    if ((ep->subject == NULL) || (ep->config == NULL) || (ep->config->handler == NULL))
        // nothing to do
        return NATS_OK;

    // reset the stats.
    memset(&ep->stats, 0, sizeof(ep->stats));

    return natsConnection_QueueSubscribe(&ep->sub, ep->m->nc, ep->subject,
                                         MICRO_QUEUE_GROUP, handle_request, ep);
}

// TODO <>/<> COPY FROM GO
natsStatus
micro_stop_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;

    if (ep->sub != NULL)
    {
        s = natsSubscription_Drain(ep->sub);
    }
    if (s == NATS_OK)
    {
        ep->sub = NULL;

        // reset the stats.
        memset(&ep->stats, 0, sizeof(ep->stats));
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
free_endpoint(microEndpoint *ep)
{
    NATS_FREE(ep->subject);
    natsMutex_Destroy(ep->mu);
    micro_free_cloned_endpoint_config(ep->config);
    NATS_FREE(ep);
    return NATS_OK;
}

natsStatus
micro_stop_and_destroy_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;

    if (ep == NULL)
        return NATS_OK;

    IFOK(s, micro_stop_endpoint(ep));
    IFOK(s, free_endpoint(ep));

    return NATS_UPDATE_ERR_STACK(s);
}

static bool
is_valid_name(const char *name)
{
    int i;
    int len;

    if (name == NULL)
        return false;

    len = (int)strlen(name);
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
is_valid_subject(const char *subject)
{
    int i;
    int len;

    if (subject == NULL)
        return false;

    len = (int)strlen(subject);
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

static void
handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    microEndpoint *ep = (microEndpoint *)closure;
    microEndpointStats *stats = &ep->stats;
    microEndpointConfig *cfg = ep->config;
    microRequest *req = NULL;
    microRequestHandler handler = cfg->handler;
    int64_t start, elapsed_ns, full_s;

    if (handler == NULL)
    {
        // This is a bug, we should not have received a message on this
        // subscription.
        natsMsg_Destroy(msg);
        return;
    }

    s = micro_new_request(&req, msg);
    if (s != NATS_OK)
    {
        natsMsg_Destroy(msg);
        return;
    }

    // handle the request.
    start = nats_NowInNanoSeconds();
    req->ep = ep;
    handler(ep->m, req);
    elapsed_ns = nats_NowInNanoSeconds() - start;

    // update stats.
    natsMutex_Lock(ep->mu);
    stats->num_requests++;
    stats->processing_time_ns += elapsed_ns;
    full_s = stats->processing_time_ns / 1000000000;
    stats->processing_time_s += full_s;
    stats->processing_time_ns -= full_s * 1000000000;

    natsMutex_Unlock(ep->mu);

    micro_free_request(req);
    natsMsg_Destroy(msg);
}

void micro_update_last_error(microEndpoint *ep, microError *err)
{
    if (err == NULL)
        return;

    natsMutex_Lock(ep->mu);
    ep->stats.num_errors++;
    microError_String(err, ep->stats.last_error_string, sizeof(ep->stats.last_error_string));
    natsMutex_Unlock(ep->mu);
}
