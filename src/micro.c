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

// TODO review includes
#include <ctype.h>
#include <stdarg.h>

#include "microp.h"
#include "mem.h"
#include "conn.h"

static natsStatus
_createMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg);
static void
_freeMicroservice(natsMicroservice *m);
static void
_retainMicroservice(natsMicroservice *m);
static void
_releaseMicroservice(natsMicroservice *m);

//////////////////////////////////////////////////////////////////////////////
// microservice management APIs
//////////////////////////////////////////////////////////////////////////////

natsStatus
nats_AddMicroservice(natsMicroservice **new_m, natsConnection *nc, natsMicroserviceConfig *cfg)
{
    natsStatus s;

    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _createMicroservice(new_m, nc, cfg);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    return NATS_OK;
}

void natsMicroservice_Destroy(natsMicroservice *m)
{
    _releaseMicroservice(m);
}

// TODO update from Go
natsStatus
natsMicroservice_Stop(natsMicroservice *m)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsMutex_Lock(m->mu);

    if (m->stopped)
        goto OK;

    m->stopped = true;

OK:
    natsMutex_Unlock(m->mu);
    return NATS_OK;
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

// TODO: eliminate sleep
natsStatus
natsMicroservice_Run(natsMicroservice *m)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    for (; !natsMicroservice_IsStopped(m);)
    {
        nats_Sleep(50);
    }
    return NATS_OK;
}

natsStatus
natsMicroservice_Respond(natsMicroservice *m, natsMicroserviceRequest *r, const char *data, int len)
{
    natsStatus s = NATS_OK;

    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConnection_Publish(m->nc, natsMsg_GetReply(r->msg), data, len);
    return NATS_UPDATE_ERR_STACK(s);
}

// Implementation functions.

static natsStatus
_createMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg)
{
    natsStatus s = NATS_OK;
    natsMicroservice *m = NULL;
    natsMutex *mu = NULL;
    char tmpNUID[NUID_BUFFER_LEN + 1];

    // Make a microservice object, with a reference to a natsConnection.
    m = (natsMicroservice *)NATS_CALLOC(1, sizeof(natsMicroservice));
    if (m == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);
    natsConn_retain(nc);
    m->nc = nc;

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
        m->cfg = *cfg;
    }

    // Set up the default endpoint.
    if (s == NATS_OK && cfg->endpoint != NULL)
    {
        s = natsMicroservice_AddEndpoint(m, natsMicroserviceDefaultEndpointName, cfg->endpoint);
    }

    // Set up monitoring (PING, STATS, etc.) responders.
    IFOK(s, _initMicroserviceMonitoring(m));

    if (s == NATS_OK)
        *new_microservice = m;
    else
    {
        _freeMicroservice(m);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_freeMicroservice(natsMicroservice *m)
{
    int i;
    natsStatus s;

    if (m == NULL)
        return;

    for (i = 0; i < m->num_endpoints; i++)
    {
        s = _stopMicroserviceEndpoint(m->endpoints[i]);
        
        if(s == NATS_OK)
            _freeMicroserviceEndpoint(m->endpoints[i]);
    }
    NATS_FREE(m->endpoints);

    natsConn_release(m->nc);
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->identity.id);
    NATS_FREE(m);
}

static void
_retainMicroservice(natsMicroservice *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
}

static void
_releaseMicroservice(natsMicroservice *m)
{
    bool doFree;

    if (m == NULL)
        return;

    natsMutex_Lock(m->mu);
    doFree = (--(m->refs) == 0);
    natsMutex_Unlock(m->mu);

    if (doFree)
        _freeMicroservice(m);
}

