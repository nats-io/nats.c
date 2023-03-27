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

microError *
microService_Create(microService **new_m, natsConnection *nc, microServiceConfig *cfg)
{
    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL))
        return micro_ErrorInvalidArg;

    natsStatus s = NATS_OK;
    microError *err = NULL;
    microService *m = NULL;

    // Make a microservice object, with a reference to a natsConnection.
    m = (microService *)NATS_CALLOC(1, sizeof(microService));
    if (m == NULL)
        return micro_ErrorOutOfMemory;

    natsConn_retain(nc);
    m->nc = nc;
    m->refs = 1;
    m->started = nats_Now() * 1000000;

    IFOK(s, NATS_CALLOCS(&m->id, 1, NUID_BUFFER_LEN + 1))
    IFOK(s, natsMutex_Create(&m->mu));
    IFOK(s, natsNUID_Next(m->id, NUID_BUFFER_LEN + 1));
    IFOK(s, micro_clone_service_config(&m->cfg, cfg));
    if ((s == NATS_OK) && (cfg->endpoint != NULL))
    {
        err = microService_AddEndpoint(NULL, m, cfg->endpoint);
        if (err != NULL)
        {
            // status is always set in AddEndpoint errors.
            s = err->status;
        }
    }

    // Set up monitoring (PING, INFO, STATS, etc.) responders.
    IFOK(s, micro_init_monitoring(m));

    if (s == NATS_OK)
    {
        *new_m = m;
        return NULL;
    }
    else
    {
        stop_and_destroy_microservice(m);
        return microError_Wrapf(microError_FromStatus(s), "failed to create microservice");
    }
}

microError *
microService_Release(microService *m)
{
    bool doFree;

    if (m == NULL)
        return NULL;

    natsMutex_Lock(m->mu);
    m->refs--;
    doFree = (m->refs == 0);
    natsMutex_Unlock(m->mu);

    if (!doFree)
        return NULL;

    return stop_and_destroy_microservice(m);
}

microService *
natsMicroservice_Retain(microService *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
    return m;
}

// TODO <>/<> update from Go
microError *
microService_Stop(microService *m)
{
    natsStatus s = NATS_OK;
    int i;

    if (m == NULL)
        return micro_ErrorInvalidArg;
    if (microService_IsStopped(m))
        return NULL;

    for (i = 0; (s == NATS_OK) && (i < m->endpoints_len); i++)
    {
        s = micro_stop_endpoint(m->endpoints[i]);
    }

    if (s == NATS_OK)
    {
        natsMutex_Lock(m->mu);
        m->stopped = true;
        m->started = 0;
        natsMutex_Unlock(m->mu);

        return NULL;
    }
    else
    {
        return microError_Wrapf(microError_FromStatus(s), "failed to stop microservice");
    }
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

// TODO: <>/<> eliminate sleep
microError *
microService_Run(microService *m)
{
    if ((m == NULL) || (m->mu == NULL))
        return micro_ErrorInvalidArg;

    for (; !microService_IsStopped(m);)
    {
        nats_Sleep(50);
    }

    return NULL;
}

NATS_EXTERN natsConnection *
microService_GetConnection(microService *m)
{
    if (m == NULL)
        return NULL;
    return m->nc;
}

microError *
microService_AddEndpoint(microEndpoint **new_ep, microService *m, microEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    int index = -1;
    int new_cap;
    microEndpoint *ep = NULL;

    if ((m == NULL) || (cfg == NULL))
    {
        return micro_ErrorInvalidArg;
    }

    s = micro_new_endpoint(&ep, m, cfg);
    if (s != NATS_OK)
        return microError_Wrapf(microError_FromStatus(s), "failed to create endpoint");

    // see if we already have an endpoint with this name
    for (int i = 0; i < m->endpoints_len; i++)
    {
        if (strcmp(m->endpoints[i]->config->name, cfg->name) == 0)
        {
            index = i;
            break;
        }
    }

    // if not, grow the array as needed.
    if (index == -1)
    {
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

    // add the endpoint.
    m->endpoints[index] = ep;

    s = micro_start_endpoint(ep);
    if (s == NATS_OK)
    {
        if (new_ep != NULL)
            *new_ep = ep;
        return NULL;
    }
    else
    {
        micro_stop_and_destroy_endpoint(ep);
        return microError_Wrapf(microError_FromStatus(NATS_UPDATE_ERR_STACK(s)), "failed to start endpoint");
    }
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
natsMicroservice_GetStats(microServiceStats **new_stats, microService *m)
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

static microError *
stop_and_destroy_microservice(microService *m)
{
    microError *err = NULL;
    natsStatus s = NATS_OK;
    int i;

    if (m == NULL)
        return NULL;

    err = microService_Stop(m);
    if (err != NULL)
        return err;

    for (i = 0; i < m->endpoints_len; i++)
    {
        s = micro_stop_and_destroy_endpoint(m->endpoints[i]);
        if (s != NATS_OK)
            return microError_Wrapf(microError_FromStatus(s), "failed to stop and destroy endpoint");
    }

    micro_free_cloned_service_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->id);
    NATS_FREE(m);
    return NULL;
}

natsStatus
micro_clone_service_config(microServiceConfig **out, microServiceConfig *cfg)
{
    natsStatus s = NATS_OK;
    microServiceConfig *new_cfg = NULL;
    microEndpointConfig *new_ep = NULL;
    char *new_name = NULL;
    char *new_version = NULL;
    char *new_description = NULL;

    if (out == NULL || cfg == NULL)
        return NATS_INVALID_ARG;

    s = NATS_CALLOCS(&new_cfg, 1, sizeof(microServiceConfig));
    if (s == NATS_OK && cfg->name != NULL)
    {
        DUP_STRING(s, new_name, cfg->name);
    }
    if (s == NATS_OK && cfg->version != NULL)
    {
        DUP_STRING(s, new_version, cfg->version);
    }
    if (s == NATS_OK && cfg->description != NULL)
    {
        DUP_STRING(s, new_description, cfg->description);
    }
    if (s == NATS_OK && cfg->endpoint != NULL)
    {
        s = micro_clone_endpoint_config(&new_ep, cfg->endpoint);
    }
    if (s == NATS_OK)
    {
        memcpy(new_cfg, cfg, sizeof(microServiceConfig));
        new_cfg->name = new_name;
        new_cfg->version = new_version;
        new_cfg->description = new_description;
        new_cfg->endpoint = new_ep;
        *out = new_cfg;
    }
    else
    {
        NATS_FREE(new_cfg);
        NATS_FREE(new_name);
        NATS_FREE(new_version);
        NATS_FREE(new_description);
        micro_free_cloned_endpoint_config(new_ep);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

void micro_free_cloned_service_config(microServiceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->name);
    NATS_FREE((char *)cfg->version);
    NATS_FREE((char *)cfg->description);
    micro_free_cloned_endpoint_config(cfg->endpoint);
    NATS_FREE(cfg);
}
