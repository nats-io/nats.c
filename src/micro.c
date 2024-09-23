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
#include "conn.h"
#include "opts.h"
#include "util.h"

static inline void _lock_service(microService *m) { natsMutex_Lock(m->service_mu); }
static inline void _unlock_service(microService *m) { natsMutex_Unlock(m->service_mu); }

static microError *_clone_service_config(microServiceConfig **out, microServiceConfig *cfg);
static microError *_new_service(microService **ptr, natsConnection *nc);
static microError *_wrap_connection_event_callbacks(microService *m);

static void _free_cloned_service_config(microServiceConfig *cfg);
static void _free_service(microService *m);
static void _release_service(microService *m);
static void _retain_service(microService *m);

microError *
micro_AddService(microService **new_m, natsConnection *nc, microServiceConfig *cfg)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    microService *m = NULL;

    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL) || !micro_is_valid_name(cfg->Name) || nats_IsStringEmpty(cfg->Version))
        return micro_ErrorInvalidArg;
    if ((cfg->QueueGroup != NULL) && nats_IsStringEmpty(cfg->QueueGroup))
        return micro_ErrorInvalidArg;

    // Make a microservice object, with a reference to a natsConnection.
    err = _new_service(&m, nc);
    if (err != NULL)
        return err;

    IFOK(s, natsMutex_Create(&m->service_mu));
    IFOK(s, natsNUID_Next(m->id, sizeof(m->id)));
    err = micro_ErrorFromStatus(s);

    MICRO_CALL(err, _clone_service_config(&m->cfg, cfg));

    // Wrap the connection callbacks before we subscribe to anything.
    MICRO_CALL(err, _wrap_connection_event_callbacks(m));

    MICRO_CALL(err, micro_init_monitoring(m));
    MICRO_CALL(err, microService_AddEndpoint(m, cfg->Endpoint));

    if (err != NULL)
    {
        microError_Ignore(microService_Destroy(m));
        return microError_Wrapf(err, "failed to add microservice %s", cfg->Name);
    }

    *new_m = m;
    return NULL;
}

microError *
micro_add_endpoint(microEndpoint **new_ep, microService *m, microGroup *g, microEndpointConfig *cfg, bool is_internal)
{
    microError *err = NULL;
    microEndpoint *ptr = NULL;
    microEndpoint *prev_ptr = NULL;
    microEndpoint *ep = NULL;
    microEndpoint *prev_ep = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;
    if (cfg == NULL)
        return NULL;

    err = micro_new_endpoint(&ep, m, g, cfg, is_internal);
    if (err != NULL)
        return microError_Wrapf(err, "failed to create endpoint %s", cfg->Name);

    _lock_service(m);

    if (m->stopped)
    {
        _unlock_service(m);
        return micro_Errorf("can't add an endpoint %s to service %s: the service is stopped", cfg->Name, m->cfg->Name);
    }

    if (m->first_ep != NULL)
    {
        if (strcmp(m->first_ep->subject, ep->subject) == 0)
        {
            ep->next = m->first_ep->next;
            prev_ep = m->first_ep;
            m->first_ep = ep;
        }
        else
        {
            prev_ptr = m->first_ep;
            for (ptr = m->first_ep->next; ptr != NULL; prev_ptr = ptr, ptr = ptr->next)
            {
                if (strcmp(ptr->subject, ep->subject) == 0)
                {
                    ep->next = ptr->next;
                    prev_ptr->next = ep;
                    prev_ep = ptr;
                    break;
                }
            }
            if (prev_ep == NULL)
            {
                prev_ptr->next = ep;
            }
        }
    }
    else
    {
        m->first_ep = ep;
    }

    _unlock_service(m);

    if (prev_ep != NULL)
    {
        // Rid of the previous endpoint with the same subject, if any. If this
        // fails we can return the error, leave the newly added endpoint in the
        // list, not started. A retry with the same subject will clean it up.
        if (err = micro_stop_endpoint(prev_ep), err != NULL)
            return err;
        micro_release_endpoint(prev_ep);
    }

    // retain `m` before the endpoint uses it for its on_complete callback.
    _retain_service(m);

    if (err = micro_start_endpoint(ep), err != NULL)
    {
        // Best effort, leave the new endpoint in the list, as is. A retry with
        // the same name will clean it up.
        _release_service(m);
        return microError_Wrapf(err, "failed to start endpoint %s", ep->name);
    }

    if (new_ep != NULL)
        *new_ep = ep;
    return NULL;
}

microError *
microService_AddEndpoint(microService *m, microEndpointConfig *cfg)
{
    return micro_add_endpoint(NULL, m, NULL, cfg, false);
}

microError *
microGroup_AddEndpoint(microGroup *g, microEndpointConfig *cfg)
{
    if (g == NULL)
        return micro_ErrorInvalidArg;

    return micro_add_endpoint(NULL, g->m, g, cfg, false);
}

microError *
microService_Stop(microService *m)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;
    bool finalize = false;
    microDoneHandler doneHandler = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    _lock_service(m);

    if (m->stopped)
    {
        _unlock_service(m);
        return NULL;
    }
    ep = m->first_ep;

    for (; ep != NULL; ep = ep->next)
    {
        if (err = micro_stop_endpoint(ep), err != NULL)
        {
            _unlock_service(m);
            return microError_Wrapf(err, "failed to stop service '%s', stopping endpoint '%s'", m->cfg->Name, ep->name);
        }
    }

    finalize = (m->first_ep == NULL);
    if (finalize)
    {
        natsLib_stopServiceCallbacks(m);
        m->stopped = true;
        doneHandler = m->cfg->DoneHandler;
    }

    _unlock_service(m);

    if (finalize)
    {
        if (doneHandler != NULL)
            doneHandler(m);

        // Relase the endpoint's server reference from `micro_add_endpoint`.
        _release_service(m);
    }

    return NULL;
}

static bool
_find_endpoint(microEndpoint **prevp, microService *m, microEndpoint *to_find)
{
    microEndpoint *ep = NULL;
    microEndpoint *prev_ep = NULL;

    if ((m == NULL) || (to_find == NULL))
        return false;

    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if (ep == to_find)
        {
            *prevp = prev_ep;
            return true;
        }
        prev_ep = ep;
    }

    return false;
}

void micro_release_on_endpoint_complete(void *closure)
{
    microEndpoint *ep = (microEndpoint *)closure;
    microEndpoint *prev_ep = NULL;
    microService *m = NULL;
    natsSubscription *sub = NULL;
    microDoneHandler doneHandler = NULL;
    bool free_ep = false;
    bool finalize = false;

    if (ep == NULL)
        return;

    m = ep->m;
    if ((m == NULL) || (m->service_mu == NULL))
        return;

    micro_lock_endpoint(ep);
    ep->is_draining = false;
    sub = ep->sub;
    ep->sub = NULL;
    ep->refs--;
    free_ep = (ep->refs == 0);
    micro_unlock_endpoint(ep);

    // Force the subscription to be destroyed now.
    natsSubscription_Destroy(sub);

    _lock_service(m);

    // Release the service reference for the completed endpoint. It can not be
    // the last reference, so no need to free m.
    m->refs--;

    // Unlink the endpoint from the service.
    if (_find_endpoint(&prev_ep, m, ep))
    {
        if (prev_ep != NULL)
        {
            prev_ep->next = ep->next;
        }
        else
        {
            m->first_ep = ep->next;
        }
    }

    finalize = (!m->stopped) && (m->first_ep == NULL);
    if (finalize)
    {
        natsLib_stopServiceCallbacks(m);
        m->stopped = true;
        doneHandler = m->cfg->DoneHandler;
    }

    _unlock_service(m);

    if (free_ep)
        micro_free_endpoint(ep);

    if (finalize)
    {
        if (doneHandler != NULL)
            doneHandler(m);

        // Relase the endpoint's server reference from `micro_add_endpoint`.
        _release_service(m);
    }
}

bool microService_IsStopped(microService *m)
{
    bool stopped;

    if ((m == NULL) || (m->service_mu == NULL))
        return true;

    _lock_service(m);
    stopped = m->stopped;
    _unlock_service(m);

    return stopped;
}

microError *
microService_Destroy(microService *m)
{
    microError *err = NULL;

    err = microService_Stop(m);
    if (err != NULL)
        return err;

    _release_service(m);
    return NULL;
}

microError *
microService_Run(microService *m)
{
    if ((m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    while (!microService_IsStopped(m))
    {
        nats_Sleep(50);
    }

    return NULL;
}

void *
microService_GetState(microService *m)
{
    if (m == NULL)
        return NULL;

    return m->cfg->State;
}

static microError *
_new_service(microService **ptr, natsConnection *nc)
{
    *ptr = NATS_CALLOC(1, sizeof(microService));
    if (*ptr == NULL)
        return micro_ErrorOutOfMemory;

    natsConn_retain(nc);
    (*ptr)->refs = 1;
    (*ptr)->nc = nc;
    (*ptr)->started = nats_Now() * 1000000;
    return NULL;
}

static void
_retain_service(microService *m)
{
    if (m == NULL)
        return;

    _lock_service(m);

    ++(m->refs);

    _unlock_service(m);
}

static void
_release_service(microService *m)
{
    int refs = 0;

    if (m == NULL)
        return;

    _lock_service(m);

    refs = --(m->refs);

    _unlock_service(m);

    if (refs == 0)
        _free_service(m);
}

static inline void
_free_cloned_group_config(microGroupConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Prefix);
    NATS_FREE((char *)cfg->QueueGroup);
    NATS_FREE(cfg);
}

static inline void
_free_group(microGroup *g)
{
    if (g == NULL)
        return;

    _free_cloned_group_config(g->config);
    NATS_FREE(g);
}

static inline void
_free_service(microService *m)
{
    if (m == NULL)
        return;

    // destroy all groups.
    if (m->groups != NULL)
    {
        microGroup *next = NULL;
        microGroup *g;
        for (g = m->groups; g != NULL; g = next)
        {
            next = g->next;
            _free_group(g);
        }
    }

    _free_cloned_service_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->service_mu);
    NATS_FREE(m);
}

static inline microError *
_new_service_config(microServiceConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microServiceConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

static microError *
_clone_service_config(microServiceConfig **out, microServiceConfig *cfg)
{
    microError *err = NULL;
    microServiceConfig *new_cfg = NULL;

    if ((out == NULL) || (cfg == NULL))
        return micro_ErrorInvalidArg;

    err = _new_service_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microServiceConfig));
    }
    // the strings are declared const for the public, but in a clone these need
    // to be duplicated.
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Version, cfg->Version));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Description, cfg->Description));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->QueueGroup, cfg->QueueGroup));
    MICRO_CALL(err, micro_ErrorFromStatus(
                        nats_cloneMetadata(&new_cfg->Metadata, cfg->Metadata)));
    MICRO_CALL(err, micro_clone_endpoint_config(&new_cfg->Endpoint, cfg->Endpoint));
    if (err != NULL)
    {
        _free_cloned_service_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

static void
_free_cloned_service_config(microServiceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Version);
    NATS_FREE((char *)cfg->Description);
    NATS_FREE((char *)cfg->QueueGroup);
    nats_freeMetadata(&cfg->Metadata);
    micro_free_cloned_endpoint_config(cfg->Endpoint);
    NATS_FREE(cfg);
}

static microError *
_start_service_callbacks(microService *m)
{
    natsStatus s = NATS_OK;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    // Extra reference to the service as long as its callbacks are registered.
    _retain_service(m);

    s = natsLib_startServiceCallbacks(m);
    if (s != NATS_OK)
    {
        _release_service(m);
    }

    return micro_ErrorFromStatus(s);
}

static microError *
_services_for_connection(microService ***to_call, int *num_microservices, natsConnection *nc)
{
    natsMutex *mu = natsLib_getServiceCallbackMutex();
    natsHash *h = natsLib_getAllServicesToCallback();
    microService *m = NULL;
    microService **p = NULL;
    natsHashIter iter;
    int n = 0;
    int i;

    natsMutex_Lock(mu);

    natsHashIter_Init(&iter, h);
    while (natsHashIter_Next(&iter, NULL, (void **)&m))
        if (m->nc == nc)
            n++;
    natsHashIter_Done(&iter);
    if (n > 0)
    {
        p = NATS_CALLOC(n, sizeof(microService *));
        if (p == NULL)
        {
            natsMutex_Unlock(mu);
            return micro_ErrorOutOfMemory;
        }

        natsHashIter_Init(&iter, h);
        i = 0;
        while (natsHashIter_Next(&iter, NULL, (void **)&m))
        {
            if (m->nc == nc)
            {
                _retain_service(m); // for the callback
                p[i++] = m;
            }
        }
        natsHashIter_Done(&iter);
    }

    natsMutex_Unlock(mu);

    *to_call = p;
    *num_microservices = n;
    return NULL;
}

static void
_on_connection_closed(natsConnection *nc, void *ignored)
{
    microService *m = NULL;
    microService **to_call = NULL;
    microError *err = NULL;
    int n = 0;
    int i;

    err = _services_for_connection(&to_call, &n, nc);
    if (err != NULL)
    {
        microError_Ignore(err);
        return;
    }

    for (i = 0; i < n; i++)
    {
        m = to_call[i];
        microError_Ignore(microService_Stop(m));

        _release_service(m);
    }

    NATS_FREE(to_call);
}

static void
_on_service_error(microService *m, const char *subject, natsStatus s)
{
    microEndpoint *ep = NULL;
    microError *err = NULL;

    if (m == NULL)
        return;

    _lock_service(m);
    for (ep = m->first_ep;
         (ep != NULL) && !micro_match_endpoint_subject(ep->subject, subject);
         ep = ep->next)
        ;
    micro_retain_endpoint(ep); // for the callback
    _unlock_service(m);

    if (ep != NULL)
    {
        if (m->cfg->ErrHandler != NULL)
            (*m->cfg->ErrHandler)(m, ep, s);

        err = microError_Wrapf(micro_ErrorFromStatus(s), "NATS error on endpoint %s", ep->subject);
        micro_update_last_error(ep, err);
        microError_Destroy(err);
    }
    micro_release_endpoint(ep); // after the callback

    // TODO: Should we stop the service? The Go client does.
    microError_Ignore(microService_Stop(m));
}

static void
_on_error(natsConnection *nc, natsSubscription *sub, natsStatus s, void *not_used)
{
    microService *m = NULL;
    microService **to_call = NULL;
    microError *err = NULL;
    const char *subject = NULL;
    int n = 0;
    int i;

    if (sub == NULL)
    {
        return;
    }
    subject = natsSubscription_GetSubject(sub);

    // `to_call` will have a list of retained service pointers.
    err = _services_for_connection(&to_call, &n, nc);
    if (err != NULL)
    {
        microError_Ignore(err);
        return;
    }

    for (i = 0; i < n; i++)
    {
        m = to_call[i];
        _on_service_error(m, subject, s);
        _release_service(m); // release the extra ref in `to_call`.
    }

    NATS_FREE(to_call);
}

static microError *
_wrap_connection_event_callbacks(microService *m)
{
    microError *err = NULL;

    if ((m == NULL) || (m->nc == NULL) || (m->nc->opts == NULL))
        return micro_ErrorInvalidArg;

    // The new service must be in the list for this to work.
    MICRO_CALL(err, _start_service_callbacks(m));
    MICRO_CALL(err, micro_ErrorFromStatus(
                        natsOptions_setMicroCallbacks(m->nc->opts, _on_connection_closed, _on_error)));

    return microError_Wrapf(err, "failed to wrap connection event callbacks");
}

static inline microError *
_new_group_config(microGroupConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microGroupConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

static inline microError *
_clone_group_config(microGroupConfig **out, microGroupConfig *in, microGroup *parent)
{
    microError *err = NULL;
    microGroupConfig *new_cfg = NULL;

    if ((out == NULL) || (in == NULL))
        return micro_ErrorInvalidArg;

    err = _new_group_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, in, sizeof(microGroupConfig));
    }

    // If the queue group is not explicitly set, copy from the parent.
    if (err == NULL)
    {
        if (in->NoQueueGroup)
            new_cfg->QueueGroup = NULL;
        else if (!nats_IsStringEmpty(in->QueueGroup))
            err = micro_strdup((char **)&new_cfg->QueueGroup, in->QueueGroup);
        else if (parent != NULL)
        {
            new_cfg->NoQueueGroup = parent->config->NoQueueGroup;
            err = micro_strdup((char **)&new_cfg->QueueGroup, parent->config->QueueGroup);
        }
    }

    // prefix = parent_prefix.prefix
    if (err == NULL)
    {
        size_t prefixSize = strlen(in->Prefix) + 1;
        if (parent != NULL)
            prefixSize += strlen(parent->config->Prefix) + 1;
        new_cfg->Prefix = NATS_CALLOC(1, prefixSize);
        if (new_cfg->Prefix != NULL)
        {
            if (parent != NULL)
                snprintf((char *)new_cfg->Prefix, prefixSize, "%s.%s", parent->config->Prefix, in->Prefix);
            else
                memcpy((char *)new_cfg->Prefix, in->Prefix, prefixSize);
        }
        else
            err = micro_ErrorOutOfMemory;
    }

    if (err != NULL)
    {
        _free_cloned_group_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

static inline microError *
_add_group(microGroup **new_group, microService *m, microGroup *parent, microGroupConfig *config)
{

    *new_group = NATS_CALLOC(1, sizeof(microGroup));
    if (new_group == NULL)
        return micro_ErrorOutOfMemory;

    microError *err = NULL;
    err =  _clone_group_config(&(*new_group)->config, config, parent);
    if (err != NULL)
    {
        NATS_FREE(*new_group);
        return err;
    }

    (*new_group)->m = m;
    (*new_group)->next = m->groups;
    m->groups = *new_group;

    return NULL;
}

microError *
microService_AddGroup(microGroup **new_group, microService *m, microGroupConfig *config)
{
    if ((m == NULL) || (new_group == NULL) || (config == NULL) || nats_IsStringEmpty(config->Prefix))
        return micro_ErrorInvalidArg;

    return _add_group(new_group, m, NULL, config);
}

microError *
microGroup_AddGroup(microGroup **new_group, microGroup *parent, microGroupConfig *config)
{
    if ((parent == NULL) || (new_group == NULL) || (config == NULL) || nats_IsStringEmpty(config->Prefix))
        return micro_ErrorInvalidArg;

    return _add_group(new_group, parent->m, parent, config);
}

natsConnection *
microService_GetConnection(microService *m)
{
    if (m == NULL)
        return NULL;
    return m->nc;
}

microError *
microService_GetInfo(microServiceInfo **new_info, microService *m)
{
    microError *err = NULL;
    microServiceInfo *info = NULL;
    microEndpoint *ep = NULL;
    int len;

    if ((new_info == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    info = NATS_CALLOC(1, sizeof(microServiceInfo));
    if (info == NULL)
        return micro_ErrorOutOfMemory;

    MICRO_CALL(err, micro_strdup((char **)&info->Name, m->cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&info->Version, m->cfg->Version));
    MICRO_CALL(err, micro_strdup((char **)&info->Description, m->cfg->Description));
    MICRO_CALL(err, micro_strdup((char **)&info->Id, m->id));
    MICRO_CALL(err, micro_ErrorFromStatus(
        nats_cloneMetadata(&info->Metadata, m->cfg->Metadata)));

    if (err == NULL)
    {
        info->Type = MICRO_INFO_RESPONSE_TYPE;

        _lock_service(m);

        len = 0;
        for (ep = m->first_ep; ep != NULL; ep = ep->next)
        {
            if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
                len++;
        }

        // Overallocate subjects, will filter out internal ones.
        info->Endpoints = NATS_CALLOC(len, sizeof(microEndpointInfo));
        if (info->Endpoints == NULL)
        {
            err = micro_ErrorOutOfMemory;
        }

        len = 0;
        for (ep = m->first_ep; (err == NULL) && (ep != NULL); ep = ep->next)
        {
            if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
            {
                MICRO_CALL(err, micro_strdup((char **)&info->Endpoints[len].Name, ep->name));
                MICRO_CALL(err, micro_strdup((char **)&info->Endpoints[len].Subject, ep->subject));
                MICRO_CALL(err, micro_strdup((char **)&info->Endpoints[len].QueueGroup, micro_queue_group_for_endpoint(ep)));
                MICRO_CALL(err, micro_ErrorFromStatus(
                                    nats_cloneMetadata(&info->Endpoints[len].Metadata, ep->config->Metadata)));
                if (err == NULL)
                {
                    len++;
                    info->EndpointsLen = len;
                }
            }
        }
        _unlock_service(m);
    }

    if (err != NULL)
    {
        microServiceInfo_Destroy(info);
        return err;
    }

    *new_info = info;
    return NULL;
}

void microServiceInfo_Destroy(microServiceInfo *info)
{
    int i;

    if (info == NULL)
        return;

    // casts to quiet the compiler.
    for (i = 0; i < info->EndpointsLen; i++)
    {
        NATS_FREE((char *)info->Endpoints[i].Name);
        NATS_FREE((char *)info->Endpoints[i].Subject);
        NATS_FREE((char *)info->Endpoints[i].QueueGroup);
        nats_freeMetadata(&info->Endpoints[i].Metadata);
    }
    NATS_FREE((char *)info->Endpoints);
    NATS_FREE((char *)info->Name);
    NATS_FREE((char *)info->Version);
    NATS_FREE((char *)info->Description);
    NATS_FREE((char *)info->Id);
    nats_freeMetadata(&info->Metadata);
    NATS_FREE(info);
}

microError *
microService_GetStats(microServiceStats **new_stats, microService *m)
{
    microError *err = NULL;
    microServiceStats *stats = NULL;
    microEndpoint *ep = NULL;
    int len;
    long double avg = 0.0;

    if ((new_stats == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    stats = NATS_CALLOC(1, sizeof(microServiceStats));
    if (stats == NULL)
        return micro_ErrorOutOfMemory;

    MICRO_CALL(err, micro_strdup((char **)&stats->Name, m->cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&stats->Version, m->cfg->Version));
    MICRO_CALL(err, micro_strdup((char **)&stats->Id, m->id));

    if (err == NULL)
    {
        stats->Started = m->started;
        stats->Type = MICRO_STATS_RESPONSE_TYPE;

        _lock_service(m);

        len = 0;
        for (ep = m->first_ep; ep != NULL; ep = ep->next)
        {
            if ((ep != NULL) && (!ep->is_monitoring_endpoint))
                len++;
        }

        // Allocate the actual structs, not pointers.
        stats->Endpoints = NATS_CALLOC(len, sizeof(microEndpointStats));
        if (stats->Endpoints == NULL)
        {
            err = micro_ErrorOutOfMemory;
        }

        len = 0;
        for (ep = m->first_ep; ((err == NULL) && (ep != NULL)); ep = ep->next)
        {
            if ((ep != NULL) && (!ep->is_monitoring_endpoint) && (ep->endpoint_mu != NULL))
            {
                micro_lock_endpoint(ep);
                // copy the entire struct, including the last error buffer.
                stats->Endpoints[len] = ep->stats;

                MICRO_CALL(err, micro_strdup((char **)&stats->Endpoints[len].Name, ep->name));
                MICRO_CALL(err, micro_strdup((char **)&stats->Endpoints[len].Subject, ep->subject));
                MICRO_CALL(err, micro_strdup((char **)&stats->Endpoints[len].QueueGroup, micro_queue_group_for_endpoint(ep)));
                if (err == NULL)
                {
                    avg = (long double)ep->stats.ProcessingTimeSeconds * 1000000000.0 + (long double)ep->stats.ProcessingTimeNanoseconds;
                    avg = avg / (long double)ep->stats.NumRequests;
                    stats->Endpoints[len].AverageProcessingTimeNanoseconds = (int64_t)avg;
                    len++;
                    stats->EndpointsLen = len;
                }
                micro_unlock_endpoint(ep);
            }
        }

        _unlock_service(m);
    }

    if (err != NULL)
    {
        microServiceStats_Destroy(stats);
        return err;
    }
    *new_stats = stats;
    return NULL;
}

void microServiceStats_Destroy(microServiceStats *stats)
{
    int i;

    if (stats == NULL)
        return;

    for (i = 0; i < stats->EndpointsLen; i++)
    {
        NATS_FREE((char *)stats->Endpoints[i].Name);
        NATS_FREE((char *)stats->Endpoints[i].Subject);
        NATS_FREE((char *)stats->Endpoints[i].QueueGroup);
    }
    NATS_FREE(stats->Endpoints);
    NATS_FREE((char *)stats->Name);
    NATS_FREE((char *)stats->Version);
    NATS_FREE((char *)stats->Id);
    NATS_FREE(stats);
}
