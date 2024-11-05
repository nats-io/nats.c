// Copyright 2023 The NATS Authors
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

#include "microp.h"
#include "util.h"

static void _handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static void _retain_endpoint(microEndpoint *ep, bool lock);
static void _release_endpoint(microEndpoint *ep);

const char *
micro_queue_group_for_endpoint(microEndpoint *ep)
{
    if (ep->config->NoQueueGroup)
        return NULL;
    else if (!nats_IsStringEmpty(ep->config->QueueGroup))
        return ep->config->QueueGroup;

    if (ep->group != NULL)
    {
        if(ep->group->config->NoQueueGroup)
            return NULL;
        else if (!nats_IsStringEmpty(ep->group->config->QueueGroup))
            return ep->group->config->QueueGroup;
    }

    if (ep->m->cfg->NoQueueGroup)
        return NULL;
    else if(!nats_IsStringEmpty(ep->m->cfg->QueueGroup))
        return ep->m->cfg->QueueGroup;

    return MICRO_DEFAULT_QUEUE_GROUP;
}

microError *
micro_start_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;

    if ((ep->subject == NULL) || (ep->config == NULL) || (ep->config->Handler == NULL) || (ep->m == NULL))
        // nothing to do
        return NULL;

    // reset the stats.
    memset(&ep->stats, 0, sizeof(ep->stats));

    const char *queueGroup = micro_queue_group_for_endpoint(ep);
    if (ep->is_monitoring_endpoint || (queueGroup == NULL))
        s = natsConnection_Subscribe(&sub, ep->m->nc, ep->subject, _handle_request, ep);
    else
        s = natsConnection_QueueSubscribe(&sub, ep->m->nc, ep->subject, queueGroup, _handle_request, ep);

    if (s == NATS_OK)
    {
        // extra retain for the subscription since we'll need to hold it until
        // on_complete.
        micro_lock_endpoint(ep);
        ep->refs++;
        ep->sub = sub;
        micro_unlock_endpoint(ep);

        natsSubscription_SetOnCompleteCB(sub, micro_release_endpoint_when_unsubscribed, ep);
    }
    else
    {
        natsSubscription_Destroy(sub); // likely always a no-op.
    }

    return micro_ErrorFromStatus(s);
}

microError *
micro_stop_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;

    if (ep == NULL)
        return NULL;

    micro_lock_endpoint(ep);
    sub = ep->sub;
    micro_unlock_endpoint(ep);
    if (sub == NULL)
        return NULL;

    // When the drain is complete, the callback will free ep. We may get an
    // NATS_INVALID_SUBSCRIPTION if the subscription is already closed.
    s = natsSubscription_Drain(sub);
    if ((s != NATS_OK) && (s != NATS_INVALID_SUBSCRIPTION))
        return microError_Wrapf(micro_ErrorFromStatus(s), "failed to drain subscription");

    return NULL;
}

void micro_retain_endpoint(microEndpoint *ep)
{
    if (ep == NULL)
        return;

    micro_lock_endpoint(ep);

    ep->refs++;

    micro_unlock_endpoint(ep);
}

void micro_release_endpoint(microEndpoint *ep)
{
    int refs;

    if (ep == NULL)
        return;

    micro_lock_endpoint(ep);

    refs = --(ep->refs);

    micro_unlock_endpoint(ep);

    if (refs == 0)
        micro_free_endpoint(ep);
}

void micro_free_endpoint(microEndpoint *ep)
{
    if (ep == NULL)
        return;

    NATS_FREE(ep->subject);
    natsSubscription_Destroy(ep->sub);
    natsMutex_Destroy(ep->endpoint_mu);
    micro_free_cloned_endpoint_config(ep->config);
    NATS_FREE(ep);
}

static void
_update_last_error(microEndpoint *ep, microError *err)
{
    ep->stats.NumErrors++;
    microError_String(err, ep->stats.LastErrorString, sizeof(ep->stats.LastErrorString));
}

static void
_handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    microError *err = NULL;
    microError *service_err = NULL;
    microEndpoint *ep = (microEndpoint *)closure;
    microService *m;
    microEndpointStats *stats = NULL;
    microRequestHandler handler;
    microRequest *req = NULL;
    int64_t start, elapsed_ns = 0, full_s;

    if ((ep == NULL) || (ep->endpoint_mu == NULL) || (ep->config == NULL) || (ep->config->Handler == NULL))
    {
        // This would be a bug, we should not have received a message on this
        // subscription.
        return;
    }

    stats = &ep->stats;
    m = ep->m;
    handler = ep->config->Handler;

    err = micro_new_request(&req, m, ep, msg);
    if (err == NULL)
    {
        // handle the request.
        start = nats_NowInNanoSeconds();
        service_err = handler(req);
        if (service_err != NULL)
        {
            // if the handler returned an error, we attempt to respond with it.
            // Note that if the handler chose to do its own RespondError which
            // fails, and then the handler returns its error - we'll try to
            // RespondError again, double-counting the error.
            err = microRequest_RespondError(req, service_err);
        }

        elapsed_ns = nats_NowInNanoSeconds() - start;
    }

    // Update stats.
    micro_lock_endpoint(ep);
    stats->NumRequests++;
    stats->ProcessingTimeNanoseconds += elapsed_ns;
    full_s = stats->ProcessingTimeNanoseconds / 1000000000;
    stats->ProcessingTimeSeconds += full_s;
    stats->ProcessingTimeNanoseconds -= full_s * 1000000000;
    _update_last_error(ep, err);
    micro_unlock_endpoint(ep);

    microError_Destroy(err);
    micro_free_request(req);
    natsMsg_Destroy(msg);
}

void micro_update_last_error(microEndpoint *ep, microError *err)
{
    if (err == NULL || ep == NULL)
        return;

    micro_lock_endpoint(ep);
    _update_last_error(ep, err);
    micro_unlock_endpoint(ep);
}

bool micro_is_valid_name(const char *name)
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

bool micro_is_valid_subject(const char *subject)
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

static inline microError *
_new_endpoint_config(microEndpointConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microEndpointConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

microError *
micro_clone_endpoint_config(microEndpointConfig **out, microEndpointConfig *cfg)
{
    microError *err = NULL;
    microEndpointConfig *new_cfg = NULL;

    if (out == NULL)
        return microError_Wrapf(micro_ErrorInvalidArg, "failed to clone endpoint config: '%s'", cfg->Name);

    if (cfg == NULL)
    {
        *out = NULL;
        return NULL;
    }

    err = _new_endpoint_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microEndpointConfig));
    }

    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Subject, cfg->Subject));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->QueueGroup, cfg->QueueGroup));
    MICRO_CALL(err, micro_ErrorFromStatus(
                        nats_cloneMetadata(&new_cfg->Metadata, cfg->Metadata)));

    if (err != NULL)
    {
        micro_free_cloned_endpoint_config(new_cfg);
        return microError_Wrapf(err, "failed to clone endpoint config: '%s'", cfg->Name);
    }

    *out = new_cfg;
    return NULL;
}

void micro_free_cloned_endpoint_config(microEndpointConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Subject);
    nats_freeMetadata(&cfg->Metadata);

    NATS_FREE(cfg);
}

bool micro_match_endpoint_subject(const char *ep_subject, const char *actual_subject)
{
    const char *e = ep_subject;
    const char *a = actual_subject;
    const char *etok, *enext;
    int etok_len;
    bool last_etok = false;
    const char *atok, *anext;
    int atok_len;
    bool last_atok = false;

    if (e == NULL || a == NULL)
        return false;

    while (true)
    {
        enext = strchr(e, '.');
        if (enext == NULL)
        {
            enext = e + strlen(e);
            last_etok = true;
        }
        etok = e;
        etok_len = (int)(enext - e);
        e = enext + 1;

        anext = strchr(a, '.');
        if (anext == NULL)
        {
            anext = a + strlen(a);
            last_atok = true;
        }
        atok = a;
        atok_len = (int)(anext - a);
        a = anext + 1;

        if (last_etok)
        {
            if (etok_len == 1 && etok[0] == '>')
                return true;

            if (!last_atok)
                return false;
        }
        if (!(etok_len == 1 && etok[0] == '*') &&
            !(etok_len == atok_len && strncmp(etok, atok, etok_len) == 0))
        {
            return false;
        }
        if (last_atok)
        {
            return last_etok;
        }
    }
}

