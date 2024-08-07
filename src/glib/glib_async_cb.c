// Copyright 2015-2024 The NATS Authors
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

#include "glibp.h"

void
nats_freeAsyncCbs(natsLib *lib)
{
    natsLibAsyncCbs *cbs = &(lib->asyncCbs);

    natsThread_Destroy(cbs->thread);
    natsCondition_Destroy(cbs->cond);
    natsMutex_Destroy(cbs->lock);
}


void
nats_asyncCbsThreadf(void *arg)
{
    natsLib *lib = (natsLib*) arg;
    natsLibAsyncCbs *asyncCbs = &(lib->asyncCbs);
    natsAsyncCbInfo *cb       = NULL;
    natsConnection  *nc       = NULL;
#if defined(NATS_HAS_STREAMING)
    stanConnection  *sc       = NULL;
#endif

    WAIT_LIB_INITIALIZED(lib);

    natsMutex_Lock(asyncCbs->lock);

    // We want all callbacks to be invoked even on shutdown
    while (true)
    {
        while (((cb = asyncCbs->head) == NULL) && !asyncCbs->shutdown)
            natsCondition_Wait(asyncCbs->cond, asyncCbs->lock);

        if ((cb == NULL) && asyncCbs->shutdown)
            break;

        asyncCbs->head = cb->next;

        if (asyncCbs->tail == cb)
            asyncCbs->tail = NULL;

        cb->next = NULL;

        natsMutex_Unlock(asyncCbs->lock);

        nc = cb->nc;
#if defined(NATS_HAS_STREAMING)
        sc = cb->sc;
#endif

        switch (cb->type)
        {
            case ASYNC_CLOSED:
            {
                (*(nc->opts->closedCb))(nc, nc->opts->closedCbClosure);
                if (nc->opts->microClosedCb != NULL)
                    (*(nc->opts->microClosedCb))(nc, NULL);
               break;
            }

            case ASYNC_DISCONNECTED:
                (*(nc->opts->disconnectedCb))(nc, nc->opts->disconnectedCbClosure);
                break;
            case ASYNC_RECONNECTED:
                (*(nc->opts->reconnectedCb))(nc, nc->opts->reconnectedCbClosure);
                break;
            case ASYNC_CONNECTED:
                (*(nc->opts->connectedCb))(nc, nc->opts->connectedCbClosure);
                break;
            case ASYNC_DISCOVERED_SERVERS:
                (*(nc->opts->discoveredServersCb))(nc, nc->opts->discoveredServersClosure);
                break;
            case ASYNC_LAME_DUCK_MODE:
                (*(nc->opts->lameDuckCb))(nc, nc->opts->lameDuckClosure);
                break;
            case ASYNC_ERROR:
            {
                if (cb->errTxt != NULL)
                    nats_setErrStatusAndTxt(cb->err, cb->errTxt);

                (*(nc->opts->asyncErrCb))(nc, cb->sub, cb->err, nc->opts->asyncErrCbClosure);
                if (nc->opts->microAsyncErrCb != NULL)
                    (*(nc->opts->microAsyncErrCb))(nc, cb->sub, cb->err, NULL);
                break;
            }
#if defined(NATS_HAS_STREAMING)
            case ASYNC_STAN_CONN_LOST:
                (*(sc->opts->connectionLostCB))(sc, sc->connLostErrTxt, sc->opts->connectionLostCBClosure);
                break;
#endif
            default:
                break;
        }

        natsAsyncCb_Destroy(cb);
        nats_clearLastError();

        natsMutex_Lock(asyncCbs->lock);
    }

    natsMutex_Unlock(asyncCbs->lock);

    natsLib_Release();
}

natsStatus
nats_postAsyncCbInfo(natsAsyncCbInfo *info)
{
    natsLib *lib = nats_lib();
    natsMutex_Lock(lib->asyncCbs.lock);

    if (lib->asyncCbs.shutdown)
    {
        natsMutex_Unlock(lib->asyncCbs.lock);
        return NATS_NOT_INITIALIZED;
    }

    info->next = NULL;

    if (lib->asyncCbs.head == NULL)
        lib->asyncCbs.head = info;

    if (lib->asyncCbs.tail != NULL)
        lib->asyncCbs.tail->next = info;

    lib->asyncCbs.tail = info;

    natsCondition_Signal(lib->asyncCbs.cond);

    natsMutex_Unlock(lib->asyncCbs.lock);

    return NATS_OK;
}

