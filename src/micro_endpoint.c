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

#include "microp.h"
#include "mem.h"

static void
handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static microError *new_endpoint(microEndpoint **ptr);

static microError *dup_with_prefix(char **dst, const char *prefix, const char *src)
{
    int len = (int)strlen(src) + 1;
    char *p;

    if (!nats_IsStringEmpty(prefix))
        len += (int)strlen(prefix) + 1;

    *dst = NATS_CALLOC(1, len);
    if (*dst == NULL)
        return micro_ErrorOutOfMemory;

    p = *dst;
    if (prefix != NULL)
    {
        len = strlen(prefix);
        memcpy(p, prefix, len);
        p[len] = '.';
        p += len + 1;
    }
    memcpy(p, src, strlen(src) + 1);
    return NULL;
}

microError *
micro_new_endpoint(microEndpoint **new_ep, microService *m, const char *prefix, microEndpointConfig *cfg)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;
    const char *subj;

    if (!micro_is_valid_name(cfg->Name) || (cfg->Handler == NULL))
        return micro_ErrorInvalidArg;

    if ((cfg->Subject != NULL) && !micro_is_valid_subject(cfg->Subject))
        return micro_ErrorInvalidArg;

    subj = nats_IsStringEmpty(cfg->Subject) ? cfg->Name : cfg->Subject;

    MICRO_CALL(err, new_endpoint(&ep));
    MICRO_CALL(err, micro_ErrorFromStatus(natsMutex_Create(&ep->mu)));
    MICRO_CALL(err, micro_clone_endpoint_config(&ep->config, cfg));
    MICRO_CALL(err, dup_with_prefix(&ep->name, prefix, cfg->Name));
    MICRO_CALL(err, dup_with_prefix(&ep->subject, prefix, subj));
    if (err != NULL)
    {
        micro_stop_and_destroy_endpoint(ep);
        return err;
    }

    ep->m = m;
    *new_ep = ep;
    return NULL;
}

microError *
micro_start_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;
    if ((ep->subject == NULL) || (ep->config == NULL) || (ep->config->Handler == NULL))
        // nothing to do
        return NULL;

    // reset the stats.
    memset(&ep->stats, 0, sizeof(ep->stats));

    s = natsConnection_QueueSubscribe(&ep->sub, ep->m->nc, ep->subject,
                                      MICRO_QUEUE_GROUP, handle_request, ep);
    if (s != NATS_OK)
        return micro_ErrorFromStatus(s);

    return NULL;
}

microError *
micro_stop_and_destroy_endpoint(microEndpoint *ep)
{
    natsStatus s = NATS_OK;

    if ((ep == NULL) || (ep->sub == NULL))
        return NULL;

    if (!natsConnection_IsClosed(ep->m->nc))
    {
        s = natsSubscription_Drain(ep->sub);
        if (s != NATS_OK)
            return micro_ErrorFromStatus(s);
    }

    NATS_FREE(ep->name);
    NATS_FREE(ep->subject);
    natsSubscription_Destroy(ep->sub);
    natsMutex_Destroy(ep->mu);
    micro_destroy_cloned_endpoint_config(ep->config);
    NATS_FREE(ep);

    return NULL;
}

static void
handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    // natsStatus s = NATS_OK;
    microError *err = NULL;
    microEndpoint *ep = (microEndpoint *)closure;
    microEndpointStats *stats;
    microRequestHandler handler;
    microRequest *req = NULL;
    int64_t start, elapsed_ns, full_s;

    if (ep == NULL || ep->config == NULL || ep->config->Handler == NULL)
    {
        // This is a bug, we should not have received a message on this
        // subscription.
        natsMsg_Destroy(msg);
        return;
    }
    stats = &ep->stats;
    handler = ep->config->Handler;

    err = micro_new_request(&req, ep->m, ep, msg);
    if (err != NULL)
    {
        natsMsg_Destroy(msg);
        return;
    }
    req->Endpoint = ep;

    // handle the request.
    start = nats_NowInNanoSeconds();
    handler(req);
    elapsed_ns = nats_NowInNanoSeconds() - start;

    // Update stats. Note that unlike in the go client, the error stats are
    // handled by req.Respond function.
    natsMutex_Lock(ep->mu);
    stats->num_requests++;
    stats->processing_time_ns += elapsed_ns;
    full_s = stats->processing_time_ns / 1000000000;
    stats->processing_time_s += full_s;
    stats->processing_time_ns -= full_s * 1000000000;
    natsMutex_Unlock(ep->mu);

    micro_destroy_request(req);
    natsMsg_Destroy(msg);
}

void micro_update_last_error(microEndpoint *ep, microError *err)
{
    if (err == NULL || ep == NULL)
        return;

    natsMutex_Lock(ep->mu);
    ep->stats.num_errors++;
    microError_String(err, ep->stats.last_error_string, sizeof(ep->stats.last_error_string));
    natsMutex_Unlock(ep->mu);
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

static inline void
destroy_schema(microSchema *schema)
{
    if (schema == NULL)
        return;

    NATS_FREE((char *)schema->Request);
    NATS_FREE((char *)schema->Response);
    NATS_FREE(schema);
}

static microError *
clone_schema(microSchema **to, const microSchema *from)
{
    microError *err = NULL;
    if (from == NULL)
    {
        *to = NULL;
        return NULL;
    }

    *to = NATS_CALLOC(1, sizeof(microSchema));
    if (*to == NULL)
        return micro_ErrorOutOfMemory;

    MICRO_CALL(err, micro_strdup((char **)&((*to)->Request), from->Request));
    MICRO_CALL(err, micro_strdup((char **)&((*to)->Response), from->Response));

    if (err != NULL)
    {
        destroy_schema(*to);
        *to = NULL;
        return err;
    }

    return NULL;
}

static microError *
new_endpoint(microEndpoint **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microEndpoint));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

static inline microError *
new_endpoint_config(microEndpointConfig **ptr)
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
        return micro_ErrorInvalidArg;

    if (cfg == NULL)
    {
        *out = NULL;
        return NULL;
    }

    err = new_endpoint_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microEndpointConfig));
    }

    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Subject, cfg->Subject));
    MICRO_CALL(err, clone_schema(&new_cfg->Schema, cfg->Schema));

    if (err != NULL)
    {
        micro_destroy_cloned_endpoint_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

void micro_destroy_cloned_endpoint_config(microEndpointConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Subject);

    destroy_schema(cfg->Schema);
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
