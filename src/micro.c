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
static void _on_connection_closed(natsConnection *nc, void *ignored);
static void _on_error(natsConnection *nc, natsSubscription *sub, natsStatus s, void *ignored);

static void _free_cloned_service_config(microServiceConfig *cfg);
static void _free_service(microService *m);

static microError *
_attach_service_to_connection(natsConnection *nc, microService *service);

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

    // Add the service to the connection.
    MICRO_CALL(err, _attach_service_to_connection(m->nc, m));

    // Initialize the monitoring endpoints.
    MICRO_CALL(err, micro_init_monitoring(m));

    // Add the default endpoint.
    MICRO_CALL(err, microService_AddEndpoint(m, cfg->Endpoint));

    if (err != NULL)
    {
        microError_Ignore(microService_Destroy(m));
        return microError_Wrapf(err, "failed to add microservice '%s'", cfg->Name);
    }

    *new_m = m;
    return NULL;
}

static microError *_subjectWithGroupPrefix(char **dst, microGroup *g, const char *src)
{
    size_t len = strlen(src) + 1;
    char *p;

    if (g != NULL)
        len += strlen(g->config->Prefix) + 1;

    *dst = NATS_CALLOC(1, len);
    if (*dst == NULL)
        return micro_ErrorOutOfMemory;

    p = *dst;
    if (g != NULL)
    {
        len = strlen(g->config->Prefix);
        memcpy(p, g->config->Prefix, len);
        p[len] = '.';
        p += len + 1;
    }
    memcpy(p, src, strlen(src) + 1);
    return NULL;
}

microError *
_new_endpoint(microEndpoint **new_ep, microService *m, microGroup *g, microEndpointConfig *cfg, bool is_internal, char *fullSubject)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;

    if (cfg == NULL)
        return microError_Wrapf(micro_ErrorInvalidArg, "NULL endpoint config");
    if (!micro_is_valid_name(cfg->Name))
        return microError_Wrapf(micro_ErrorInvalidArg, "invalid endpoint name '%s'", cfg->Name);
    if (cfg->Handler == NULL)
        return microError_Wrapf(micro_ErrorInvalidArg, "NULL endpoint request handler for '%s'", cfg->Name);

    ep = NATS_CALLOC(1, sizeof(microEndpoint));
    if (ep == NULL)
        return micro_ErrorOutOfMemory;
    ep->is_monitoring_endpoint = is_internal;
    ep->m = m;

    MICRO_CALL(err, micro_ErrorFromStatus(natsMutex_Create(&ep->endpoint_mu)));
    MICRO_CALL(err, micro_clone_endpoint_config(&ep->config, cfg));
    if (err != NULL)
    {
        micro_free_endpoint(ep);
        return err;
    }

    ep->subject = fullSubject; // already malloced, will be freed in micro_free_endpoint
    ep->group = g;
    *new_ep = ep;
    return NULL;
}


microError *
micro_add_endpoint(microEndpoint **new_ep, microService *m, microGroup *g, microEndpointConfig *cfg, bool is_internal)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;
    bool update = false;

    if (m == NULL)
        return micro_ErrorInvalidArg;
    if (cfg == NULL)
        return NULL;

    char *fullSubject = NULL;
    if (err = _subjectWithGroupPrefix(&fullSubject, g, nats_IsStringEmpty(cfg->Subject) ? cfg->Name : cfg->Subject), err != NULL)
        return microError_Wrapf(err, "failed to create full subject for endpoint '%s'", cfg->Name);
    if (!micro_is_valid_subject(fullSubject))
    {
        NATS_FREE(fullSubject);
        return microError_Wrapf(micro_ErrorInvalidArg, "invalid subject '%s' for endpoint '%s'", fullSubject, cfg->Name);
    }

    _lock_service(m);
    if (m->stopped)
        err = micro_Errorf("can't add an endpoint '%s' to service '%s': the service is stopped", cfg->Name, m->cfg->Name);

    // See if there is already an endpoint with the same subject. ep->subject is
    // immutable after the EP's creation so we don't need to lock it.
    for (ep = m->first_ep; (err == NULL) && (ep != NULL); ep = ep->next)
    {
        if (strcmp(ep->subject, fullSubject) == 0)
        {
            // Found an existing endpoint with the same subject. We will update
            // it as long as we can re-use the existing subscription, which at
            // the moment means we can't change the queue group settings.
            if (cfg->NoQueueGroup != ep->config->NoQueueGroup)
                err = micro_Errorf("can't change the queue group settings for endpoint '%s'", cfg->Name);
            else if (!nats_StringEquals(cfg->QueueGroup, ep->config->QueueGroup))
                err = micro_Errorf("can't change the queue group for endpoint '%s'", cfg->Name);
            if (err == NULL)
            {
                NATS_FREE(fullSubject);
                update = true;
                fullSubject = NULL;
                break;
            }
        }
    }

    if (err == NULL)
    {
        if (update)
        {
            // If the endpoint already exists, update its config and stats.
            // This will make it use the new handler for subsequent
            // requests.
            micro_lock_endpoint(ep);
            micro_free_cloned_endpoint_config(ep->config);
            err = micro_clone_endpoint_config(&ep->config, cfg);
            if (err == NULL)
                memset(&ep->stats, 0, sizeof(ep->stats));
            micro_unlock_endpoint(ep);
        }
        else
        {
            err = _new_endpoint(&ep, m, g, cfg, is_internal, fullSubject);
            if (err == NULL)
            {
                // Set up the endpoint in the service before it starts
                // processing messages.
                ep->next = m->first_ep;
                m->first_ep = ep;
                m->numEndpoints++;

                err = micro_start_endpoint(ep);
                if (err != NULL)
                {
                    // Unwind, remove the endpoint from the list.
                    m->first_ep = ep->next;
                    m->numEndpoints--;
                    micro_free_endpoint(ep);
                }
            }
        }
    }

    _unlock_service(m);
    if (err != NULL)
        return microError_Wrapf(err, "can't add an endpoint '%s' to service '%s'", cfg->Name, m->cfg->Name);

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

static microError *
_attach_service_to_connection(natsConnection *nc, microService *service)
{
    natsStatus s = NATS_OK;
    if (nc == NULL || service == NULL)
        return micro_ErrorInvalidArg;

    natsConn_Lock(nc);

    if (natsConn_isClosed(nc) || natsConn_isDraining(nc))
    {
        natsConn_Unlock(nc);
        return micro_Errorf("can't add service '%s' to a closed or draining connection", service->cfg->Name);
    }

    // Wrap the connection callbacks before we subscribe to anything.
    s = natsOptions_setMicroCallbacks(nc->opts, _on_connection_closed, _on_error);

    if (s == NATS_OK)
    {
        natsMutex_Lock(nc->servicesMu);
        if (nc->services == NULL)
        {
            nc->services = NATS_CALLOC(1, sizeof(microService *));
            if (nc->services == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            microService **tmp = NATS_REALLOC(nc->services, (nc->numServices + 1) * sizeof(microService *));
            if (tmp == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
                nc->services = tmp;
        }

        if (s == NATS_OK)
        {
            service->refs++; // no lock needed, called from the constructor
            nc->services[nc->numServices] = service;
            nc->numServices++;
        }
        natsMutex_Unlock(nc->servicesMu);
    }

    natsConn_Unlock(nc);

    return micro_ErrorFromStatus(s);
}

static void
_detach_service_from_connection(natsConnection *nc, microService *m)
{
    if (nc == NULL || m == NULL)
        return;

    natsMutex_Lock(nc->servicesMu);
    for (int i = 0; i < nc->numServices; i++)
    {
        if (nc->services[i] != m)
            continue;

        nc->services[i] = nc->services[nc->numServices - 1];
        nc->numServices--;
        break;
    }
    natsMutex_Unlock(nc->servicesMu);
}

static microError *
_stop_service(microService *m, bool unsubscribe, bool release)
{
    microError      *err            = NULL;
    microEndpoint   *ep             = NULL;
    int             refs            = 0;
    int             numEndpoints    = 0;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    _lock_service(m);
    if (!m->stopped)
    {
        m->stopped = true;

        if (unsubscribe)
        {
            for (ep = m->first_ep; ep != NULL; ep = ep->next)
            {
                if (err = micro_stop_endpoint(ep), err != NULL)
                {
                    err = microError_Wrapf(err, "failed to stop service '%s', stopping endpoint '%s'", m->cfg->Name, ep->config->Name);
                    _unlock_service(m);
                    return err;
                }
            }
        }
    }

    if (release)
        m->refs--;

    refs = m->refs;
    numEndpoints = m->numEndpoints;
    _unlock_service(m);

    if ((refs == 0) && (numEndpoints == 0))
        _free_service(m);

    return NULL;
}

microError *
microService_Stop(microService *m)
{
    return _stop_service(m, true, false);
}

// service lock must be held by the caller.
static void
_detach_endpoint_from_service(microService *m, microEndpoint *toRemove)
{
    microEndpoint *ep = NULL;
    microEndpoint *prev_ep = NULL;

    if ((m == NULL) || (toRemove == NULL))
        return;

    for (ep = m->first_ep; (ep != NULL) && (ep != toRemove); prev_ep = ep, ep = ep->next)
       ;
    if (ep == NULL)
        return;

    m->numEndpoints--;
    if (prev_ep == NULL)
        m->first_ep = ep->next;
    else
    {
        micro_lock_endpoint(prev_ep);
        prev_ep->next = ep->next;
        micro_unlock_endpoint(prev_ep);
    }
}

// Callback for when an endpoint's subscription is finished. It is called in the
// context of the subscription's delivery thread; this is where we want to call
// the service's complete callback when the last subscription is done.
void micro_release_endpoint_when_unsubscribed(void *closure)
{
    microEndpoint       *ep         = (microEndpoint *)closure;
    microService        *m          = NULL;
    microDoneHandler    doneHandler = NULL;
    int                 refs        = 0;

    if (ep == NULL)
        return;

    m = ep->m;
    if ((m == NULL) || (m->service_mu == NULL))
        return;

    _lock_service(m);

    micro_lock_endpoint(ep);
    _detach_endpoint_from_service(m, ep);
    refs = --(ep->refs);
    micro_unlock_endpoint(ep);

    if (refs == 0)
        micro_free_endpoint(ep);
    if (m->numEndpoints == 0)
    {
        // Mark the service as stopped before calling the done handler.
        m->stopped = true;
        doneHandler = m->cfg->DoneHandler;
    }

    _unlock_service(m);

    // Special processing for the last endpoint.
    if (doneHandler != NULL)
    {
        doneHandler(m);

        _detach_service_from_connection(m->nc, m);
        _stop_service(m, false, true); // just release
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
    return _stop_service(m, true, true);
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

    (*ptr)->refs = 1;
    (*ptr)->nc = nc;
    (*ptr)->started = nats_Now() * 1000000;
    return NULL;
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

static void
_on_connection_closed(natsConnection *nc, void *ignored)
{
    natsMutex_Lock(nc->servicesMu);

    // Stop all services. They will get detached from the connection when their
    // subs are complete.
    for (int i = 0; i < nc->numServices; i++)
        _stop_service(nc->services[i], false, false);

    natsMutex_Unlock(nc->servicesMu);
}

static bool
_on_service_error(microService *m, const char *subject, natsStatus s)
{
    microEndpoint   *found      = NULL;
    microError      *err        = NULL;

    if (m == NULL)
        return false;

    _lock_service(m);

    for (microEndpoint *ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if (!micro_match_endpoint_subject(ep->subject, subject))
            continue;
        found = ep;
        break;
    }

    if (found != NULL)
        micro_retain_endpoint(found);

    _unlock_service(m);

    if (found == NULL)
        return false;

    err = microError_Wrapf(micro_ErrorFromStatus(s), "NATS error on endpoint '%s'", subject);
    micro_update_last_error(found, err);
    microError_Destroy(err);

    if (m->cfg->ErrHandler != NULL)
        (*m->cfg->ErrHandler)(m, found, s);

    micro_release_endpoint(found);
    return true;
}

static void
_on_error(natsConnection *nc, natsSubscription *sub, natsStatus s, void *ignored)
{
    const char *subject = NULL;

    if (sub == NULL)
        return;

    subject = natsSubscription_GetSubject(sub);

    // TODO: this would be a lot easier if sub had a ref to ep.
    natsMutex_Lock(nc->servicesMu);
    for (int i = 0; i < nc->numServices; i++)
    {
        microService *m = nc->services[i];

        // See if the service owns the affected subscription, based on matching
        // the subjects; notify it of the error.
        if (!_on_service_error(m, subject, s))
            continue;

        // Stop the service in error. It will get detached from the connection
        // and released when all of its subs are complete.
        _stop_service(m, true, false);
    }
    natsMutex_Unlock(nc->servicesMu);
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
                MICRO_CALL(err, micro_strdup((char **)&info->Endpoints[len].Name, ep->config->Name));
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

                MICRO_CALL(err, micro_strdup((char **)&stats->Endpoints[len].Name, ep->config->Name));
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
