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

static natsError *
stop_and_destroy_microservice(natsMicroservice *m);

natsError *
nats_AddMicroservice(natsMicroservice **new_m, natsConnection *nc, natsMicroserviceConfig *cfg)
{
    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL))
        return natsMicroserviceErrorInvalidArg;

    natsStatus s = NATS_OK;
    natsError *err = NULL;
    natsMicroservice *m = NULL;

    // Make a microservice object, with a reference to a natsConnection.
    m = (natsMicroservice *)NATS_CALLOC(1, sizeof(natsMicroservice));
    if (m == NULL)
        return natsMicroserviceErrorOutOfMemory;

    natsConn_retain(nc);
    m->nc = nc;
    m->refs = 1;
    m->started = nats_Now() * 1000000;

    IFOK(s, NATS_CALLOCS(&m->id, 1, NUID_BUFFER_LEN + 1))
    IFOK(s, natsMutex_Create(&m->mu));
    IFOK(s, natsNUID_Next(m->id, NUID_BUFFER_LEN + 1));
    IFOK(s, nats_clone_microservice_config(&m->cfg, cfg));
    if ((s == NATS_OK) && (cfg->endpoint != NULL))
    {
        err = natsMicroservice_AddEndpoint(NULL, m, cfg->endpoint);
        if (err != NULL)
        {
            // status is always set in AddEndpoint errors.
            s = err->status;
        }
    }

    // Set up monitoring (PING, INFO, STATS, etc.) responders.
    IFOK(s, micro_monitoring_init(m));

    if (s == NATS_OK)
    {
        *new_m = m;
        return NULL;
    }
    else
    {
        stop_and_destroy_microservice(m);
        return natsError_Wrapf(nats_NewStatusError(s), "failed to create microservice");
    }
}

natsError *
natsMicroservice_Release(natsMicroservice *m)
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

natsMicroservice *
natsMicroservice_Retain(natsMicroservice *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
    return m;
}

// TODO <>/<> update from Go
natsError *
natsMicroservice_Stop(natsMicroservice *m)
{
    natsStatus s = NATS_OK;
    int i;

    if (m == NULL)
        return natsMicroserviceErrorInvalidArg;
    if (natsMicroservice_IsStopped(m))
        return NULL;

    for (i = 0; (s == NATS_OK) && (i < m->endpoints_len); i++)
    {
        s = nats_stop_endpoint(m->endpoints[i]);
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
        return natsError_Wrapf(nats_NewStatusError(s), "failed to stop microservice");
    }
}

bool natsMicroservice_IsStopped(natsMicroservice *m)
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
natsError *
natsMicroservice_Run(natsMicroservice *m)
{
    if ((m == NULL) || (m->mu == NULL))
        return natsMicroserviceErrorInvalidArg;

    for (; !natsMicroservice_IsStopped(m);)
    {
        nats_Sleep(50);
    }

    return NULL;
}

NATS_EXTERN natsConnection *
natsMicroservice_GetConnection(natsMicroservice *m)
{
    if (m == NULL)
        return NULL;
    return m->nc;
}

natsError *
natsMicroservice_AddEndpoint(natsMicroserviceEndpoint **new_ep, natsMicroservice *m, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    int index = -1;
    int new_cap;
    natsMicroserviceEndpoint *ep = NULL;

    if ((m == NULL) || (cfg == NULL))
    {
        return natsMicroserviceErrorInvalidArg;
    }

    s = nats_new_endpoint(&ep, m, cfg);
    if (s != NATS_OK)
        return natsError_Wrapf(nats_NewStatusError(s), "failed to create endpoint");

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
            natsMicroserviceEndpoint **new_eps = (natsMicroserviceEndpoint **)NATS_CALLOC(new_cap, sizeof(natsMicroserviceEndpoint *));
            if (new_eps == NULL)
                return natsMicroserviceErrorOutOfMemory;
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

    s = nats_start_endpoint(ep);
    if (s == NATS_OK)
    {
        if (new_ep != NULL)
            *new_ep = ep;
        return NULL;
    }
    else
    {
        nats_stop_and_destroy_endpoint(ep);
        return natsError_Wrapf(nats_NewStatusError(NATS_UPDATE_ERR_STACK(s)), "failed to start endpoint");
    }
}

natsStatus
natsMicroservice_Info(natsMicroserviceInfo **new_info, natsMicroservice *m)
{
    natsMicroserviceInfo *info = NULL;
    int i;
    int len = 0;

    if ((new_info == NULL) || (m == NULL) || (m->mu == NULL))
        return NATS_INVALID_ARG;

    info = NATS_CALLOC(1, sizeof(natsMicroserviceInfo));
    if (info == NULL)
        return NATS_NO_MEMORY;

    info->name = m->cfg->name;
    info->version = m->cfg->version;
    info->description = m->cfg->description;
    info->id = m->id;
    info->type = natsMicroserviceInfoResponseType;

    info->subjects = NATS_CALLOC(m->endpoints_len, sizeof(char *));
    if (info->subjects == NULL)
    {
        NATS_FREE(info);
        return NATS_NO_MEMORY;
    }

    natsMutex_Lock(m->mu);
    for (i = 0; i < m->endpoints_len; i++)
    {
        natsMicroserviceEndpoint *ep = m->endpoints[i];
        if ((ep != NULL) && (ep->subject != NULL))
        {
            info->subjects[len] = ep->subject;
            len++;
        }
    }
    info->subjects_len = len;
    natsMutex_Unlock(m->mu);

    *new_info = info;
    return NATS_OK;
}

void natsMicroserviceInfo_Destroy(natsMicroserviceInfo *info)
{
    if (info == NULL)
        return;

    // subjects themselves must not be freed, just the collection.
    NATS_FREE(info->subjects);
    NATS_FREE(info);
}

natsStatus
natsMicroservice_Stats(natsMicroserviceStats **new_stats, natsMicroservice *m)
{
    natsMicroserviceStats *stats = NULL;
    int i;
    int len = 0;
    long double avg = 0.0;

    if ((new_stats == NULL) || (m == NULL) || (m->mu == NULL))
        return NATS_INVALID_ARG;

    stats = NATS_CALLOC(1, sizeof(natsMicroserviceStats));
    if (stats == NULL)
        return NATS_NO_MEMORY;

    stats->name = m->cfg->name;
    stats->version = m->cfg->version;
    stats->id = m->id;
    stats->started = m->started;
    stats->type = natsMicroserviceStatsResponseType;

    // allocate the actual structs, not pointers.
    stats->endpoints = NATS_CALLOC(m->endpoints_len, sizeof(natsMicroserviceEndpointStats));
    if (stats->endpoints == NULL)
    {
        NATS_FREE(stats);
        return NATS_NO_MEMORY;
    }

    natsMutex_Lock(m->mu);
    for (i = 0; i < m->endpoints_len; i++)
    {
        natsMicroserviceEndpoint *ep = m->endpoints[i];
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
    return NATS_OK;
}

void natsMicroserviceStats_Destroy(natsMicroserviceStats *stats)
{
    if (stats == NULL)
        return;

    NATS_FREE(stats->endpoints);
    NATS_FREE(stats);
}

static natsError *
stop_and_destroy_microservice(natsMicroservice *m)
{
    natsError *err = NULL;
    natsStatus s = NATS_OK;
    int i;

    if (m == NULL)
        return NULL;

    err = natsMicroservice_Stop(m);
    if (err != NULL)
        return err;

    for (i = 0; i < m->endpoints_len; i++)
    {
        s = nats_stop_and_destroy_endpoint(m->endpoints[i]);
        if (s != NATS_OK)
            return natsError_Wrapf(nats_NewStatusError(s), "failed to stop and destroy endpoint");
    }

    nats_free_cloned_microservice_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->id);
    NATS_FREE(m);
    return NULL;
}

natsStatus
nats_clone_microservice_config(natsMicroserviceConfig **out, natsMicroserviceConfig *cfg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceConfig *new_cfg = NULL;
    natsMicroserviceEndpointConfig *new_ep = NULL;
    char *new_name = NULL;
    char *new_version = NULL;
    char *new_description = NULL;

    if (out == NULL || cfg == NULL)
        return NATS_INVALID_ARG;

    s = NATS_CALLOCS(&new_cfg, 1, sizeof(natsMicroserviceConfig));
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
        s = nats_clone_endpoint_config(&new_ep, cfg->endpoint);
    }
    if (s == NATS_OK)
    {
        memcpy(new_cfg, cfg, sizeof(natsMicroserviceConfig));
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
        nats_free_cloned_endpoint_config(new_ep);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

void nats_free_cloned_microservice_config(natsMicroserviceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->name);
    NATS_FREE((char *)cfg->version);
    NATS_FREE((char *)cfg->description);
    nats_free_cloned_endpoint_config(cfg->endpoint);
    NATS_FREE(cfg);
}
