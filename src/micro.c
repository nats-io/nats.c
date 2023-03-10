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

static natsMicroserviceError *
_createMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg);
static void
_retainMicroservice(natsMicroservice *m);
static natsMicroserviceError *
_releaseMicroservice(natsMicroservice *m);
static void
_markMicroserviceStopped(natsMicroservice *m, bool stopped);

natsMicroserviceError *
nats_AddMicroservice(natsMicroservice **new_m, natsConnection *nc, natsMicroserviceConfig *cfg)
{
    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL))
        return natsMicroserviceErrorInvalidArg;

    return _createMicroservice(new_m, nc, cfg);
}

natsMicroserviceError *
natsMicroservice_Destroy(natsMicroservice *m)
{
    return _releaseMicroservice(m);
}

// TODO <>/<> update from Go
natsMicroserviceError *
natsMicroservice_Stop(natsMicroservice *m)
{
    natsStatus s = NATS_OK;

    if (m == NULL)
        return natsMicroserviceErrorInvalidArg;
    if (natsMicroservice_IsStopped(m))
        return NULL;

    s = natsMicroserviceEndpoint_Stop(m->root);
    if (s == NATS_OK)
    {
        _markMicroserviceStopped(m, true);
        return NULL;
    }
    else
    {
        return nats_NewMicroserviceError(NATS_UPDATE_ERR_STACK(s), 500, "failed to stop microservice");
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
natsMicroserviceError *
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

natsMicroserviceError *
natsMicroservice_AddEndpoint(natsMicroserviceEndpoint **new_ep, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg)
{
    natsStatus s = NATS_OK;
    if ((m == NULL) || (m->root == NULL))
    {
        return natsMicroserviceErrorInvalidArg;
    }
    s = natsMicroserviceEndpoint_AddEndpoint(new_ep, m->root, name, cfg);

    if (s == NATS_OK)
        return NULL;
    else
        return nats_NewMicroserviceError(NATS_UPDATE_ERR_STACK(s), 500, "failed to add endpoint");
}

static natsMicroserviceError *
_destroyMicroservice(natsMicroservice *m)
{
    natsStatus s = NATS_OK;

    if ((m == NULL) || (m->root == NULL))
        return NULL;

    s = natsMicroserviceEndpoint_Destroy(m->root);
    if (s != NATS_OK)
    {
        return nats_NewMicroserviceError(NATS_UPDATE_ERR_STACK(s), 500, "failed to destroy microservice");
    }

    natsConn_release(m->nc);
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->identity.id);
    NATS_FREE(m);
    return NULL;
}

static natsMicroserviceError *
_createMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceError *err = NULL;
    natsMicroservice *m = NULL;
    natsMutex *mu = NULL;
    char tmpNUID[NUID_BUFFER_LEN + 1];

    // Make a microservice object, with a reference to a natsConnection.
    m = (natsMicroservice *)NATS_CALLOC(1, sizeof(natsMicroservice));
    if (m == NULL)
        return natsMicroserviceErrorOutOfMemory;

    // TODO: <>/<> separate PR, make a natsConn_retain return a natsConnection*
    natsConn_retain(nc);
    m->nc = nc;
    m->refs = 1;

    // Need a mutex for concurrent operations.
    s = natsMutex_Create(&mu);
    if (s == NATS_OK)
    {
        m->mu = mu;
    }

    // Generate a unique ID for this microservice.
    IFOK(s, natsNUID_Next(tmpNUID, NUID_BUFFER_LEN + 1));
    IF_OK_DUP_STRING(s, m->identity.id, tmpNUID);

    // Copy the config data.
    if (s == NATS_OK)
    {
        m->identity.name = cfg->name;
        m->identity.version = cfg->version;
        m->cfg = cfg;
    }

    // Setup the root endpoint.
    if (s == NATS_OK)
    {
        s = _newMicroserviceEndpoint(&m->root, m, "", NULL);
    }

    // Set up the default endpoint.
    if (s == NATS_OK && cfg->endpoint != NULL)
    {
         err = natsMicroservice_AddEndpoint(NULL, m, natsMicroserviceDefaultEndpointName, cfg->endpoint);
         if (err != NULL)
         {
            // status is always set in AddEndpoint errors.
             s = err->status;
         }
    }

    // Set up monitoring (PING, STATS, etc.) responders.
    IFOK(s, _initMicroserviceMonitoring(m));

    if (s == NATS_OK)
    {
        *new_microservice = m;
        return NULL;
    }
    else
    {
        _destroyMicroservice(m);
        return nats_NewMicroserviceError(NATS_UPDATE_ERR_STACK(s), 500, "failed to create microservice");
    }
}

static void
_retainMicroservice(natsMicroservice *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
}

static natsMicroserviceError *
_releaseMicroservice(natsMicroservice *m)
{
    bool doFree;

    if (m == NULL)
        return NULL;

    natsMutex_Lock(m->mu);
    doFree = (--(m->refs) == 0);
    natsMutex_Unlock(m->mu);

    if (!doFree)
        return NULL;

    return _destroyMicroservice(m);
}

static void
_markMicroserviceStopped(natsMicroservice *m, bool stopped)
{
    natsMutex_Lock(m->mu);
    m->stopped = stopped;
    natsMutex_Unlock(m->mu);
}
