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

#include "natsp.h"
#include "micro.h"
#include "mem.h"
#include "js.h"
// #include "conn.h"
// #include "sub.h"

//////////////////////////////////////////////////////////////////////////////
// microservice management APIs
//////////////////////////////////////////////////////////////////////////////

static void
_freeMicroservice(jsMicroservice *m)
{
    jsCtx *js = NULL;

    if (m == NULL)
        return;

    js = m->js;
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->identity.id);
    NATS_FREE(m);
    js_release(js);
}

static void
_retainMicroservice(jsMicroservice *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
}

static void
_releaseMicroservice(jsMicroservice *m)
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

void jsMicroservice_Destroy(jsMicroservice *m)
{
    _releaseMicroservice(m);
}

static natsStatus
_createMicroservice(jsMicroservice **new_microservice, jsCtx *js, jsMicroserviceConfig *cfg)
{
    natsStatus s = NATS_OK;
    jsMicroservice *m = NULL;
    natsMutex *mu = NULL;

    m = (jsMicroservice *)NATS_CALLOC(1, sizeof(jsMicroservice));
    if (m == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = natsMutex_Create(&mu);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);
    m->mu = mu;

    char tmpNUID[NUID_BUFFER_LEN + 1];
    s = natsNUID_Next(tmpNUID, NUID_BUFFER_LEN + 1);
    IF_OK_DUP_STRING(s, m->identity.id, tmpNUID);
    if (s == NATS_OK)
    {
        m->identity.name = cfg->name;
        m->identity.version = cfg->version;
        *new_microservice = m;
        return NATS_OK;
    }
}

natsStatus
js_AddMicroservice(jsMicroservice **new_m, jsCtx *js, jsMicroserviceConfig *cfg, jsErrCode *errCode)
{
    natsStatus s;

    if ((new_m == NULL) || (js == NULL) || (cfg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _createMicroservice(new_m, js, cfg);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (errCode != NULL)
    {
        *errCode = 0;
    }
    return NATS_OK;
}

natsStatus
js_StopMicroservice(jsMicroservice *m, jsErrCode *errCode)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsMutex_Lock(m->mu);

    if (m->stopped)
        goto OK;

    m->stopped = true;

    // if s.DoneHandler != nil {
    // 	s.asyncDispatcher.push(func() { s.DoneHandler(s) })
    // 	s.asyncDispatcher.close()
    // }

OK:
    natsMutex_Unlock(m->mu);
    if (errCode != NULL)
    {
        *errCode = 0;
    }
    return NATS_OK;
}

bool js_IsMicroserviceStopped(jsMicroservice *m)
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
js_RunMicroservice(jsMicroservice *m, jsErrCode *errCode)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    for (; !js_IsMicroserviceStopped(m);)
    {
        nats_Sleep(50);
    }
    return NATS_OK;
}
