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
#include "mem.h"

static microError *new_service(microService **ptr, natsConnection *nc);
static void free_service(microService *m);
static microError *wrap_connection_event_callbacks(microService *m);
static microError *unwrap_connection_event_callbacks(microService *m);

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

static void
_remove_endpoint(microService *m, microEndpoint *to_remove)
{
    microEndpoint *ptr = NULL;
    microEndpoint *prev_ptr = NULL;

    if (m->first_ep == NULL)
        return;

    if (m->first_ep == to_remove)
    {
        m->first_ep = m->first_ep->next;
        return;
    }

    prev_ptr = m->first_ep;
    for (ptr = m->first_ep->next; ptr != NULL; ptr = ptr->next)
    {
        if (ptr == to_remove)
        {
            prev_ptr->next = ptr->next;
            return;
        }
    }
}

void micro_unlink_endpoint_from_service(microService *m, microEndpoint *to_remove)
{
    if ((m == NULL) || to_remove == NULL)
        return;

    micro_lock_service(m);
    _remove_endpoint(m, to_remove);
    micro_unlock_service(m);
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

microError *
microService_Stop(microService *m)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    // Stop is a rare call, it's ok to lock the service for the duration.
    micro_lock_service(m);

    if (m->is_stopped)
    {
        micro_unlock_service(m);
        return NULL;
    }

    err = unwrap_connection_event_callbacks(m);
    for (ep = m->first_ep; (err == NULL) && (ep != NULL); ep = m->first_ep)
    {
        m->first_ep = ep->next;
        m->num_eps--;

        if (err = micro_destroy_endpoint(ep), err != NULL)
        {
            err = microError_Wrapf(err, "failed to stop endpoint %s", ep->name);
        }
    }

    if (err == NULL)
    {
        m->is_stopped = true;
        m->started = 0;
        m->num_eps = 0;
    }

    micro_unlock_service(m);
    return err;
}

bool microService_IsStopped(microService *m)
{
    bool is_stopped;

    if ((m == NULL) || (m->service_mu == NULL))
        return true;

    micro_lock_service(m);
    is_stopped = m->is_stopped;
    micro_unlock_service(m);

    return is_stopped;
}

microError *
microService_Destroy(microService *m)
{
    microError *err = NULL;

    err = microService_Stop(m);
    if (err != NULL)
        return err;

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

microService *
micro_retain_service(microService *m)
{
    if (m == NULL)
        return NULL;

    micro_lock_service(m);

    m->refs++;

    micro_unlock_service(m);

    return m;
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

    microService_Stop(m);

    if (m->prev_on_connection_closed != NULL)
    {
        (*m->prev_on_connection_closed)(nc, m->prev_on_connection_closed_closure);
    }
}

static void
on_connection_disconnected(natsConnection *nc, void *closure)
{
    microService *m = (microService *)closure;

    if (m == NULL)
        return;

    // <>/<> TODO: Should we stop the service? Not 100% how the Go client does
    // it.
    microService_Stop(m);

    if (m->prev_on_connection_disconnected != NULL)
    {
        (*m->prev_on_connection_closed)(nc, m->prev_on_connection_disconnected_closure);
    }
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
            break;
        }
    }
    micro_unlock_service(m);

    if (m->cfg->ErrHandler != NULL)
    {
        (*m->cfg->ErrHandler)(m, ep, s);
    }

    if (ep != NULL)
    {
        err = microError_Wrapf(micro_ErrorFromStatus(s), "NATS error on endpoint %s", ep->name);
        micro_update_last_error(ep, err);
        microError_Destroy(err);
    }

    microService_Stop(m);

    if (m->prev_on_error != NULL)
    {
        (*m->prev_on_error)(nc, sub, s, m->prev_on_error_closure);
    }
}

static microError *
wrap_connection_event_callbacks(microService *m)
{
    natsStatus s = NATS_OK;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    IFOK(s, natsConn_getClosedCallback(&m->prev_on_connection_closed, &m->prev_on_connection_closed_closure, m->nc));
    IFOK(s, natsConn_setClosedCallback(m->nc, on_connection_closed, m));

    IFOK(s, natsConn_getDisconnectedCallback(&m->prev_on_connection_disconnected, &m->prev_on_connection_disconnected_closure, m->nc));
    IFOK(s, natsConn_setDisconnectedCallback(m->nc, on_connection_disconnected, m));

    IFOK(s, natsConn_getErrorCallback(&m->prev_on_error, &m->prev_on_error_closure, m->nc));
    IFOK(s, natsConn_setErrorCallback(m->nc, on_error, m));

    return microError_Wrapf(micro_ErrorFromStatus(s), "failed to wrap connection event callbacks");
}

static microError *
unwrap_connection_event_callbacks(microService *m)
{
    natsStatus s = NATS_OK;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    IFOK(s, natsConn_setClosedCallback(m->nc, m->prev_on_connection_closed, m->prev_on_connection_closed_closure));
    IFOK(s, natsConn_setDisconnectedCallback(m->nc, m->prev_on_connection_disconnected, m->prev_on_connection_disconnected_closure));
    IFOK(s, natsConn_setErrorCallback(m->nc, m->prev_on_error, m->prev_on_error_closure));

    return microError_Wrapf(micro_ErrorFromStatus(s), "failed to unwrap connection event callbacks");
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

    info->Name = m->cfg->Name;
    info->Version = m->cfg->Version;
    info->Description = m->cfg->Description;
    info->Id = m->id;
    info->Type = MICRO_INFO_RESPONSE_TYPE;

    // Overallocate subjects, will filter out internal ones.
    info->Subjects = NATS_CALLOC(m->num_eps, sizeof(char *));
    if (info->Subjects == NULL)
    {
        NATS_FREE(info);
        return micro_ErrorOutOfMemory;
    }

    micro_lock_service(m);
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
        {
            info->Subjects[len] = ep->subject;
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
    if (info == NULL)
        return;

    // subjects themselves must not be freed, just the collection.
    NATS_FREE(info->Subjects);
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

    stats->Name = m->cfg->Name;
    stats->Version = m->cfg->Version;
    stats->Id = m->id;
    stats->Started = m->started;
    stats->Type = MICRO_STATS_RESPONSE_TYPE;

    // Allocate the actual structs, not pointers. Overallocate for the internal
    // endpoints even though they are filtered out.
    stats->Endpoints = NATS_CALLOC(m->num_eps, sizeof(microEndpointStats));
    if (stats->Endpoints == NULL)
    {
        NATS_FREE(stats);
        return micro_ErrorOutOfMemory;
    }

    micro_lock_service(m);
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((ep != NULL) && (!ep->is_monitoring_endpoint) && (ep->endpoint_mu != NULL))
        {
            micro_lock_endpoint(ep);
            // copy the entire struct, including the last error buffer.
            stats->Endpoints[len] = ep->stats;

            stats->Endpoints[len].Name = ep->name;
            stats->Endpoints[len].Subject = ep->subject;
            avg = (long double)ep->stats.processing_time_s * 1000000000.0 + (long double)ep->stats.processing_time_ns;
            avg = avg / (long double)ep->stats.num_requests;
            stats->Endpoints[len].average_processing_time_ns = (int64_t)avg;
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
    if (stats == NULL)
        return;

    NATS_FREE(stats->Endpoints);
    NATS_FREE(stats);
}
