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

#include "microp.h"
#include "conn.h"
#include "opts.h"

static microError *new_service(microService **ptr, natsConnection *nc);
static void free_service(microService *m);
static microError *wrap_connection_event_callbacks(microService *m);
static void unwrap_connection_event_callbacks(microService *m);

microError *
micro_AddService(microService **new_m, natsConnection *nc, microServiceConfig *cfg)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    microService *m = NULL;

    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL) || !micro_is_valid_name(cfg->Name) || nats_IsStringEmpty(cfg->Version))
        return micro_ErrorInvalidArg;

    // Make a microservice object, with a reference to a natsConnection.
    err = new_service(&m, nc);
    if (err != NULL)
        return err;

    IFOK(s, natsMutex_Create(&m->service_mu));
    IFOK(s, natsNUID_Next(m->id, sizeof(m->id)));
    err = micro_ErrorFromStatus(s);

    MICRO_CALL(err, micro_clone_service_config(&m->cfg, cfg));

    // Wrap the connection callbacks before we subscribe to anything.
    MICRO_CALL(err, wrap_connection_event_callbacks(m))
    MICRO_CALL(err, micro_init_monitoring(m));
    MICRO_CALL(err, microService_AddEndpoint(m, cfg->Endpoint));

    if (err != NULL)
    {
        micro_release_service(m);
        return microError_Wrapf(err, "failed to add microservice %s", cfg->Name);
    }

    *new_m = m;
    return NULL;
}

microError *
micro_add_endpoint(microEndpoint **new_ep, microService *m, const char *prefix, microEndpointConfig *cfg, bool is_internal)
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

    err = micro_new_endpoint(&ep, m, prefix, cfg, is_internal);
    if (err != NULL)
        return microError_Wrapf(err, "failed to create endpoint %s", cfg->Name);

    micro_lock_service(m);

    if (m->stopping || m->stopped)
    {
        micro_unlock_service(m);
        return micro_Errorf("can't add an endpoint %s to service %s: the service is stopped", cfg->Name, m->cfg->Name);
    }

    if (m->first_ep != NULL)
    {
        if (strcmp(m->first_ep->name, ep->name) == 0)
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
                if (strcmp(ptr->name, ep->name) == 0)
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

    // Increment the number of endpoints, unless we are replacing an existing
    // one.
    if (prev_ep == NULL)
    {
        m->num_eps++;
    }

    micro_unlock_service(m);

    if (prev_ep != NULL)
    {
        // Rid of the previous endpoint with the same name, if any. If this
        // fails we can return the error, leave the newly added endpoint in the
        // list, not started. A retry with the same name will clean it up.
        if (err = micro_destroy_endpoint(prev_ep), err != NULL)
            return err;
    }

    if (err = micro_start_endpoint(ep), err != NULL)
    {
        // Best effort, leave the new endpoint in the list, as is. A retry with
        // the same name will clean it up.
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

    return micro_add_endpoint(NULL, g->m, g->prefix, cfg, false);
}

static void micro_finalize_stopping_service(microService *m)
{
    microDoneHandler invoke_done = NULL;
    bool on_connection_closed_called = false;

    if (m == NULL)
        return;

    micro_lock_service(m);
    if (m->stopped || (m->num_eps_to_stop > 0))
    {
        micro_unlock_service(m);
        return;
    }

    on_connection_closed_called = m->connection_closed_called;
    m->stopped = true;
    m->stopping = false;
    if (m->cfg->DoneHandler != NULL)
        invoke_done = m->cfg->DoneHandler;
    micro_unlock_service(m);

    unwrap_connection_event_callbacks(m);

    // If the connection closed callback has not been called yet, it will not be
    // since we just removed it, so compensate for it by releaseing the service.
    if (!on_connection_closed_called)
    {
        micro_release_service(m);
    }

    if (invoke_done != NULL)
        invoke_done(m);
}

microError *
microService_Stop(microService *m)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;
    bool drain_endpoints = false;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    micro_lock_service(m);
    if (m->stopped)
    {
        micro_unlock_service(m);
        return NULL;
    }
    drain_endpoints = !m->stopping;
    m->stopping = true;
    ep = m->first_ep;
    micro_unlock_service(m);

    // Endpoints are never removed from the list (except when the service is
    // destroyed, but that is after Stop's already been called), so we can iterate
    // safely outside the lock.
    if (drain_endpoints)
    {
        for (; ep != NULL; ep = ep->next)
        {
            if (err = micro_stop_endpoint(ep), err != NULL)
                return microError_Wrapf(err, "failed to stop service %s", ep->config->Name);
        }
    }

    micro_finalize_stopping_service(m);
    return NULL;
}

bool microService_IsStopped(microService *m)
{
    bool stopped;

    if ((m == NULL) || (m->service_mu == NULL))
        return true;

    micro_lock_service(m);
    stopped = m->stopped;
    micro_unlock_service(m);

    return stopped;
}

microError *
microService_Destroy(microService *m)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;

    err = microService_Stop(m);
    if (err != NULL)
        return err;

    // Unlink all endpoints from the service, they will self-destruct by their
    // onComplete handler.
    while (true)
    {
        micro_lock_service(m);

        ep = m->first_ep;
        if (ep == NULL)
        {
            micro_unlock_service(m);
            break;
        }
        m->first_ep = ep->next;
        micro_unlock_service(m);

        err = micro_destroy_endpoint(ep);
        if (err != NULL)
            return err;
    }

    micro_release_service(m);
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
new_service(microService **ptr, natsConnection *nc)
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

void micro_retain_service(microService *m)
{
    if (m == NULL)
        return;

    micro_lock_service(m);

    m->refs++;

    micro_unlock_service(m);
}

void micro_release_service(microService *m)
{
    int refs = 0;

    if (m == NULL)
        return;

    micro_lock_service(m);

    refs = --(m->refs);

    micro_unlock_service(m);

    if (refs == 0)
        free_service(m);
}

void micro_increment_endpoints_to_stop(microService *m)
{
    if (m == NULL)
        return;

    micro_lock_service(m);

    m->num_eps_to_stop++;

    micro_unlock_service(m);
}

void micro_decrement_endpoints_to_stop(microService *m)
{
    if (m == NULL)
        return;

    micro_lock_service(m);

    if (m->num_eps_to_stop == 0)
    {
        micro_unlock_service(m);
        fprintf(stderr, "FATAL ERROR: should be unreachable: unbalanced stopping refs on a microservice %s\n", m->cfg->Name);
        return;
    }

    m->num_eps_to_stop--;

    micro_unlock_service(m);

    micro_finalize_stopping_service(m);
}

static void free_service(microService *m)
{
    microGroup *next = NULL;

    if (m == NULL)
        return;

    // destroy all groups.
    if (m->groups != NULL)
    {
        microGroup *g = m->groups;
        while (g != NULL)
        {
            next = g->next;
            NATS_FREE(g);
            g = next;
        }
    }

    micro_free_cloned_service_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->service_mu);
    NATS_FREE(m);
}

static inline microError *new_service_config(microServiceConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microServiceConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

microError *
micro_clone_service_config(microServiceConfig **out, microServiceConfig *cfg)
{
    microError *err = NULL;
    microServiceConfig *new_cfg = NULL;

    if (out == NULL || cfg == NULL)
        return micro_ErrorInvalidArg;

    err = new_service_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microServiceConfig));
    }
    // the strings are declared const for the public, but in a clone these need
    // to be duplicated.
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Version, cfg->Version));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Description, cfg->Description));
    MICRO_CALL(err, micro_clone_endpoint_config(&new_cfg->Endpoint, cfg->Endpoint));
    if (err != NULL)
    {
        micro_free_cloned_service_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

void micro_free_cloned_service_config(microServiceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Version);
    NATS_FREE((char *)cfg->Description);
    micro_free_cloned_endpoint_config(cfg->Endpoint);
    NATS_FREE(cfg);
}

static void
on_connection_closed(natsConnection *nc, void *closure)
{
    microService *m = (microService *)closure;
    if (m == NULL)
        return;

    micro_lock_service(m);
    m->connection_closed_called = true;
    micro_unlock_service(m);

    micro_release_service(m);

    microService_Stop(m);
}

static void
on_error(natsConnection *nc, natsSubscription *sub, natsStatus s, void *closure)
{
    microService *m = (microService *)closure;
    microEndpoint *ep = NULL;
    microError *err = NULL;
    bool our_subject = false;
    const char *subject = NULL;

    if ((m == NULL) || (sub == NULL))
        return;

    subject = natsSubscription_GetSubject(sub);

    micro_lock_service(m);
    for (ep = m->first_ep; (!our_subject) && (ep != NULL); ep = ep->next)
    {
        if (micro_match_endpoint_subject(ep->subject, subject))
        {
            our_subject = true;
        }
    }
    micro_unlock_service(m);

    if (m->cfg->ErrHandler != NULL && our_subject)
    {
        (*m->cfg->ErrHandler)(m, ep, s);
    }

    if (ep != NULL)
    {
        err = microError_Wrapf(micro_ErrorFromStatus(s), "NATS error on endpoint %s", ep->name);
        micro_update_last_error(ep, err);
        microError_Destroy(err);
    }

    // TODO: Should we stop the service? The Go client does.
    microService_Stop(m);
}

static microError *
wrap_connection_event_callbacks(microService *m)
{
    natsStatus s = NATS_OK;

    if ((m == NULL) || (m->nc == NULL) || (m->nc->opts == NULL))
        return micro_ErrorInvalidArg;

    // add an extra reference to the service for the callbacks.
    micro_retain_service(m);
    IFOK(s, natsOptions_addConnectionClosedCallback(m->nc->opts, on_connection_closed, m));
    IFOK(s, natsOptions_addErrorCallback(m->nc->opts, on_error, m));

    return microError_Wrapf(micro_ErrorFromStatus(s), "failed to wrap connection event callbacks");
}

static void
unwrap_connection_event_callbacks(microService *m)
{
    if ((m == NULL) || (m->nc == NULL) || (m->nc->opts == NULL))
        return;

    natsOptions_removeConnectionClosedCallback(m->nc->opts, on_connection_closed, m);
    natsOptions_removeErrorCallback(m->nc->opts, on_error, m);
}

microError *
microService_AddGroup(microGroup **new_group, microService *m, const char *prefix)
{
    if ((m == NULL) || (new_group == NULL) || (prefix == NULL))
        return micro_ErrorInvalidArg;

    *new_group = NATS_CALLOC(1, sizeof(microGroup) +
                                    strlen(prefix) + 1); // "prefix\0"
    if (new_group == NULL)
    {
        return micro_ErrorOutOfMemory;
    }

    memcpy((*new_group)->prefix, prefix, strlen(prefix) + 1);
    (*new_group)->m = m;
    (*new_group)->next = m->groups;
    m->groups = *new_group;

    return NULL;
}

microError *
microGroup_AddGroup(microGroup **new_group, microGroup *parent, const char *prefix)
{
    char *p;
    size_t len;

    if ((parent == NULL) || (new_group == NULL) || (prefix == NULL))
        return micro_ErrorInvalidArg;

    *new_group = NATS_CALLOC(1, sizeof(microGroup) +
                                    strlen(parent->prefix) + 1 + // "parent_prefix."
                                    strlen(prefix) + 1);         // "prefix\0"
    if (new_group == NULL)
    {
        return micro_ErrorOutOfMemory;
    }

    p = (*new_group)->prefix;
    len = strlen(parent->prefix);
    memcpy(p, parent->prefix, len);
    p[len] = '.';
    p += len + 1;
    memcpy(p, prefix, strlen(prefix) + 1);
    (*new_group)->m = parent->m;
    (*new_group)->next = parent->m->groups;
    parent->m->groups = *new_group;

    return NULL;
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
    microServiceInfo *info = NULL;
    microEndpoint *ep = NULL;
    int len = 0;

    if ((new_info == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    info = NATS_CALLOC(1, sizeof(microServiceInfo));
    if (info == NULL)
        return micro_ErrorOutOfMemory;

    micro_strdup((char **)&info->Name, m->cfg->Name);
    micro_strdup((char **)&info->Version, m->cfg->Version);
    micro_strdup((char **)&info->Description, m->cfg->Description);
    micro_strdup((char **)&info->Id, m->id);
    info->Type = MICRO_INFO_RESPONSE_TYPE;

    micro_lock_service(m);

    // Overallocate subjects, will filter out internal ones.
    info->Subjects = NATS_CALLOC(m->num_eps, sizeof(char *));
    if (info->Subjects == NULL)
    {
        micro_unlock_service(m);
        NATS_FREE(info);
        return micro_ErrorOutOfMemory;
    }

    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
        {
            micro_strdup((char **)&info->Subjects[len], ep->subject);
            len++;
        }
    }
    info->SubjectsLen = len;
    micro_unlock_service(m);

    *new_info = info;
    return NULL;
}

void microServiceInfo_Destroy(microServiceInfo *info)
{
    int i;

    if (info == NULL)
        return;

    // casts to quiet the compiler.
    for (i = 0; i < info->SubjectsLen; i++)
        NATS_FREE((char *)info->Subjects[i]);
    NATS_FREE((char *)info->Subjects);
    NATS_FREE((char *)info->Name);
    NATS_FREE((char *)info->Version);
    NATS_FREE((char *)info->Description);
    NATS_FREE((char *)info->Id);
    NATS_FREE(info);
}

microError *
microService_GetStats(microServiceStats **new_stats, microService *m)
{
    microServiceStats *stats = NULL;
    microEndpoint *ep = NULL;
    int len = 0;
    long double avg = 0.0;

    if ((new_stats == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    stats = NATS_CALLOC(1, sizeof(microServiceStats));
    if (stats == NULL)
        return micro_ErrorOutOfMemory;

    micro_strdup((char **)&stats->Name, m->cfg->Name);
    micro_strdup((char **)&stats->Version, m->cfg->Version);
    micro_strdup((char **)&stats->Id, m->id);
    stats->Started = m->started;
    stats->Type = MICRO_STATS_RESPONSE_TYPE;

    micro_lock_service(m);

    // Allocate the actual structs, not pointers. Overallocate for the internal
    // endpoints even though they are filtered out.
    stats->Endpoints = NATS_CALLOC(m->num_eps, sizeof(microEndpointStats));
    if (stats->Endpoints == NULL)
    {
        micro_unlock_service(m);
        NATS_FREE(stats);
        return micro_ErrorOutOfMemory;
    }

    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((ep != NULL) && (!ep->is_monitoring_endpoint) && (ep->endpoint_mu != NULL))
        {
            micro_lock_endpoint(ep);
            // copy the entire struct, including the last error buffer.
            stats->Endpoints[len] = ep->stats;

            micro_strdup((char **)&stats->Endpoints[len].Name, ep->name);
            micro_strdup((char **)&stats->Endpoints[len].Subject, ep->subject);
            avg = (long double)ep->stats.ProcessingTimeSeconds * 1000000000.0 + (long double)ep->stats.ProcessingTimeNanoseconds;
            avg = avg / (long double)ep->stats.NumRequests;
            stats->Endpoints[len].AverageProcessingTimeNanoseconds = (int64_t)avg;
            len++;
            micro_unlock_endpoint(ep);
        }
    }

    micro_unlock_service(m);
    stats->EndpointsLen = len;

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
    }
    NATS_FREE(stats->Endpoints);
    NATS_FREE((char *)stats->Name);
    NATS_FREE((char *)stats->Version);
    NATS_FREE((char *)stats->Id);
    NATS_FREE(stats);
}
