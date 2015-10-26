// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"
#include "mem.h"
#include "conn.h"

static void
_freeAsyncCbInfo(natsAsyncCbInfo *info)
{
    NATS_FREE(info);
}

static void
_createAndPostCb(natsAsyncCbType type, natsConnection *nc, natsSubscription *sub, natsStatus err)
{
    natsStatus          s  = NATS_OK;
    natsAsyncCbInfo     *cb;

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
    _createAndPostCb(type, nc, NULL, NATS_OK);
}

void
natsAsyncCb_PostErrHandler(natsConnection *nc, natsSubscription *sub, natsStatus err)
{
    _createAndPostCb(ASYNC_ERROR, nc, sub, err);
}

void
natsAsyncCb_Destroy(natsAsyncCbInfo *info)
{
    natsConnection *nc;

    if (info == NULL)
        return;

    nc = info->nc;

    _freeAsyncCbInfo(info);
    natsConn_release(nc);
}
