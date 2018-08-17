// Copyright 2015-2018 The NATS Authors
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

#include "natsp.h"
#include "mem.h"
#include "conn.h"
#if defined(NATS_HAS_STREAMING)
#include "stan/conn.h"
#endif

static void
_freeAsyncCbInfo(natsAsyncCbInfo *info)
{
    NATS_FREE(info);
}

static void
_createAndPostCb(natsAsyncCbType type, natsConnection *nc, natsSubscription *sub, natsStatus err, void* scPtr)
{
    natsStatus          s  = NATS_OK;
    natsAsyncCbInfo     *cb;
#if defined(NATS_HAS_STREAMING)
    stanConnection      *sc = (stanConnection*) scPtr;
#endif

    cb = NATS_CALLOC(1, sizeof(natsAsyncCbInfo));
    if (cb == NULL)
    {
        // We will ignore for now since that makes the caller handle the
        // possibility of having to run the callbacks in-line...
        return;
    }

    cb->type = type;
    cb->nc   = nc;
    cb->sub  = sub;
    cb->err  = err;
#if defined(NATS_HAS_STREAMING)
    cb->sc   = sc;
    stanConn_retain(sc);
#endif
    natsConn_retain(nc);

    s = nats_postAsyncCbInfo(cb);
    if (s != NATS_OK)
    {
        _freeAsyncCbInfo(cb);
        natsConn_release(nc);
    }
}

void
natsAsyncCb_PostConnHandler(natsConnection *nc, natsAsyncCbType type)
{
    _createAndPostCb(type, nc, NULL, NATS_OK, NULL);
}

void
natsAsyncCb_PostErrHandler(natsConnection *nc, natsSubscription *sub, natsStatus err)
{
    _createAndPostCb(ASYNC_ERROR, nc, sub, err, NULL);
}

#if defined(NATS_HAS_STREAMING)
void
natsAsyncCb_PostStanConnLostHandler(stanConnection *sc)
{
    _createAndPostCb(ASYNC_STAN_CONN_LOST, NULL, NULL, NATS_CONNECTION_CLOSED, (void*) sc);
}
#endif

void
natsAsyncCb_Destroy(natsAsyncCbInfo *info)
{
    natsConnection *nc = NULL;
#if defined(NATS_HAS_STREAMING)
    stanConnection *sc = NULL;
#endif

    if (info == NULL)
        return;

    nc = info->nc;
#if defined(NATS_HAS_STREAMING)
    sc = info->sc;
#endif

    _freeAsyncCbInfo(info);
    natsConn_release(nc);
#if defined(NATS_HAS_STREAMING)
    stanConn_release(sc);
#endif
}
