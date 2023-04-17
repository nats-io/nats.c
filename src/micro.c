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

// TODO <>/<> review includes
#include "micro.h"
#include "microp.h"
#include "conn.h"
#include "mem.h"

static microError *
stop_and_destroy_microservice(microService *m);
static microError *
wrap_connection_event_callbacks(microService *m);
static microError *
unwrap_connection_event_callbacks(microService *m);

static microError *
new_service(microService **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microService));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

microError *
micro_AddService(microService **new_m, natsConnection *nc, microServiceConfig *cfg)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    microService *m = NULL;

    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL))
        return micro_ErrorInvalidArg;

    // Make a microservice object, with a reference to a natsConnection.
    MICRO_CALL(err, new_service(&m));
    if (err != NULL)
        return err;

    natsConn_retain(nc);
    m->nc = nc;
    m->refs = 1;
    m->started = nats_Now() * 1000000;

    IFOK(s, natsMutex_Create(&m->mu));
    IFOK(s, natsNUID_Next(m->id, sizeof(m->id)));
    err = micro_ErrorFromStatus(s);

    MICRO_CALL(err, micro_clone_service_config(&m->cfg, cfg));

    // Wrap the connection callbacks before we subscribe to anything.
    MICRO_CALL(err, wrap_connection_event_callbacks(m))

    // Add the endpoints and monitoring subscriptions.
    MICRO_CALL(err, microService_AddEndpoint(NULL, m, cfg->endpoint));
    MICRO_CALL(err, micro_init_monitoring(m));

    if (err != NULL)
    {
        microService_Destroy(m);
        return microError_Wrapf(micro_ErrorFromStatus(s), "failed to add microservice");
    }

    *new_m = m;
    return NULL;
}

// TODO <>/<> update from Go
microError *
microService_Stop(microService *m)
{
    microError *err = NULL;
    natsStatus s = NATS_OK;
    int i;
    void *free_eps, *free_subs;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    natsMutex_Lock(m->mu);
    if (m->is_stopping || m->stopped)
    {
        natsMutex_Unlock(m->mu);
        return NULL;
    }
    m->is_stopping = true;
    natsMutex_Unlock(m->mu);

    err = unwrap_connection_event_callbacks(m);
    if (err != NULL)
        return err;

    for (i = 0; i < m->endpoints_len; i++)
    {
        if (err = micro_stop_and_destroy_endpoint(m->endpoints[i]), err != NULL)
        {
            return microError_Wrapf(err, "failed to stop endpoint");
        }
    }
    for (i = 0; i < m->monitoring_subs_len; i++)
    {
        if (!natsConnection_IsClosed(m->nc))
        {
            s = natsSubscription_Drain(m->monitoring_subs[i]);
            if (s != NATS_OK)
                return microError_Wrapf(micro_ErrorFromStatus(s), "failed to drain monitoring subscription");
        }
        natsSubscription_Destroy(m->monitoring_subs[i]);
    }

    // probably being paranoid here, there should be no need to lock.
    free_eps = m->endpoints;
    free_subs = m->monitoring_subs;

    natsMutex_Lock(m->mu);
    m->stopped = true;
    m->is_stopping = false;
    m->started = 0;
    m->monitoring_subs_len = 0;
    m->monitoring_subs = NULL;
    m->endpoints_len = 0;
    m->endpoints = NULL;
    natsMutex_Unlock(m->mu);

    NATS_FREE(free_eps);
    NATS_FREE(free_subs);

    return NULL;
}

bool microService_IsStopped(microService *m)
{
    bool stopped;

    if ((m == NULL) || (m->mu == NULL))
        return true;

    natsMutex_Lock(m->mu);
    stopped = m->stopped;
    natsMutex_Unlock(m->mu);

    return stopped;
}

microError *
microService_Run(microService *m)
{
    if ((m == NULL) || (m->mu == NULL))
        return micro_ErrorInvalidArg;

    while (!microService_IsStopped(m))
    {
        nats_Sleep(50);
    }

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
microService_AddEndpoint(microEndpoint **new_ep, microService *m, microEndpointConfig *cfg)
{
    microError *err = NULL;
    int index = -1;
    int new_cap;
    microEndpoint *ep = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;
    if (cfg == NULL)
        return NULL;

    err = micro_new_endpoint(&ep, m, cfg);
    if (err != NULL)
        return microError_Wrapf(err, "failed to create endpoint");

    // see if we already have an endpoint with this name
    for (int i = 0; i < m->endpoints_len; i++)
    {
        if (strcmp(m->endpoints[i]->config->name, cfg->name) == 0)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        // if not, grow the array as needed.
        if (m->endpoints_len == m->endpoints_cap)
        {
            new_cap = m->endpoints_cap * 2;
            if (new_cap == 0)
                new_cap = 4;
            microEndpoint **new_eps = (microEndpoint **)NATS_CALLOC(new_cap, sizeof(microEndpoint *));
            if (new_eps == NULL)
                return micro_ErrorOutOfMemory;
            for (int i = 0; i < m->endpoints_len; i++)
                new_eps[i] = m->endpoints[i];
            NATS_FREE(m->endpoints);
            m->endpoints = new_eps;
            m->endpoints_cap = new_cap;
        }
        index = m->endpoints_len;
        m->endpoints_len++;
    }
    else
    {
        // stop and destroy the existing endpoint.
        micro_stop_and_destroy_endpoint(m->endpoints[index]);
    }

    err = micro_start_endpoint(ep);
    if (err != NULL)
    {
        micro_stop_and_destroy_endpoint(ep);
        return microError_Wrapf(err, "failed to start endpoint");
    }

    // add the endpoint.
    m->endpoints[index] = ep;
    if (new_ep != NULL)
        *new_ep = ep;
    return NULL;
}

microError *
microService_GetInfo(microServiceInfo **new_info, microService *m)
{
    microServiceInfo *info = NULL;
    int i;
    int len = 0;

    if ((new_info == NULL) || (m == NULL) || (m->mu == NULL))
        return micro_ErrorInvalidArg;

    info = NATS_CALLOC(1, sizeof(microServiceInfo));
    if (info == NULL)
        return micro_ErrorOutOfMemory;

    info->name = m->cfg->name;
    info->version = m->cfg->version;
    info->description = m->cfg->description;
    info->id = m->id;
    info->type = MICRO_INFO_RESPONSE_TYPE;

    info->subjects = NATS_CALLOC(m->endpoints_len, sizeof(char *));
    if (info->subjects == NULL)
    {
        NATS_FREE(info);
        return micro_ErrorOutOfMemory;
    }

    natsMutex_Lock(m->mu);
    for (i = 0; i < m->endpoints_len; i++)
    {
        microEndpoint *ep = m->endpoints[i];
        if ((ep != NULL) && (ep->subject != NULL))
        {
            info->subjects[len] = ep->subject;
            len++;
        }
    }
    info->subjects_len = len;
    natsMutex_Unlock(m->mu);

    *new_info = info;
    return NULL;
}

void microServiceInfo_Destroy(microServiceInfo *info)
{
    if (info == NULL)
        return;

    // subjects themselves must not be freed, just the collection.
    NATS_FREE(info->subjects);
    NATS_FREE(info);
}

microError *
microService_GetStats(microServiceStats **new_stats, microService *m)
{
    microServiceStats *stats = NULL;
    int i;
    int len = 0;
    long double avg = 0.0;

    if ((new_stats == NULL) || (m == NULL) || (m->mu == NULL))
        return micro_ErrorInvalidArg;

    stats = NATS_CALLOC(1, sizeof(microServiceStats));
    if (stats == NULL)
        return micro_ErrorOutOfMemory;

    stats->name = m->cfg->name;
    stats->version = m->cfg->version;
    stats->id = m->id;
    stats->started = m->started;
    stats->type = MICRO_STATS_RESPONSE_TYPE;

    // allocate the actual structs, not pointers.
    stats->endpoints = NATS_CALLOC(m->endpoints_len, sizeof(microEndpointStats));
    if (stats->endpoints == NULL)
    {
        NATS_FREE(stats);
        return micro_ErrorOutOfMemory;
    }

    natsMutex_Lock(m->mu);
    for (i = 0; i < m->endpoints_len; i++)
    {
        microEndpoint *ep = m->endpoints[i];
        if ((ep != NULL) && (ep->mu != NULL))
        {
            natsMutex_Lock(ep->mu);
            // copy the entire struct, including the last error buffer.
            stats->endpoints[len] = ep->stats;

            stats->endpoints[len].name = ep->config->name;
            stats->endpoints[len].subject = ep->subject;
            avg = (long double)ep->stats.processing_time_s * 1000000000.0 + (long double)ep->stats.processing_time_ns;
            avg = avg / (long double)ep->stats.num_requests;
            stats->endpoints[len].average_processing_time_ns = (int64_t)avg;
            len++;
            natsMutex_Unlock(ep->mu);
        }
    }
    natsMutex_Unlock(m->mu);
    stats->endpoints_len = len;

    *new_stats = stats;
    return NULL;
}

void natsMicroserviceStats_Destroy(microServiceStats *stats)
{
    if (stats == NULL)
        return;

    NATS_FREE(stats->endpoints);
    NATS_FREE(stats);
}

microError *
microService_Destroy(microService *m)
{
    microError *err = NULL;

    if (m == NULL)
        return NULL;

    err = microService_Stop(m);
    if (err != NULL)
        return err;

    micro_destroy_cloned_service_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->endpoints);
    NATS_FREE(m->monitoring_subs);
    NATS_FREE(m);
    return NULL;
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
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->name, cfg->name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->version, cfg->version));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->description, cfg->description));
    MICRO_CALL(err, micro_clone_endpoint_config(&new_cfg->endpoint, cfg->endpoint));
    if (err != NULL)
    {
        micro_destroy_cloned_service_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

void micro_destroy_cloned_service_config(microServiceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->name);
    NATS_FREE((char *)cfg->version);
    NATS_FREE((char *)cfg->description);
    micro_destroy_cloned_endpoint_config(cfg->endpoint);
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
    int i;

    if ((m == NULL) || (sub == NULL))
        return;

    for (i = 0; !our_subject && (i < m->monitoring_subs_len); i++)
    {
        if (strcmp(m->monitoring_subs[i]->subject, sub->subject) == 0)
        {
            our_subject = true;
        }
    }

    for (i = 0; !our_subject && i < m->endpoints_len; i++)
    {
        ep = m->endpoints[i];
        if (ep == NULL)
            continue; // shouldn't happen

        if (micro_match_endpoint_subject(ep->subject, sub->subject))
        {
            our_subject = true;
        }
    }

    if (our_subject)
    {
        if (m->cfg->err_handler != NULL)
        {
            (*m->cfg->err_handler)(m, ep, s);
        }

        if (ep != NULL)
        {
            err = micro_ErrorFromStatus(s);
            micro_update_last_error(ep, err);
            microError_Destroy(err);
        }
    }

    if (m->prev_on_error != NULL)
    {
        (*m->prev_on_error)(nc, sub, s, m->prev_on_error_closure);
    }

    microService_Stop(m);
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
