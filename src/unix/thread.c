// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"
#include "../mem.h"


bool
nats_InitOnce(natsInitOnceType *control, natsInitOnceCb cb)
{
    if (pthread_once(control, cb) != 0)
        return false;

    return true;
}

struct threadCtx
{
    natsThreadCb    entry;
    void            *arg;
};

static void*
_threadStart(void *arg)
{
    struct threadCtx *c = (struct threadCtx*) arg;

    c->entry(c->arg);

    NATS_FREE(c);

    return NULL;
}

natsStatus
natsThread_Create(natsThread **thread, natsThreadCb cb, void *arg)
{
    struct threadCtx    *ctx = NULL;
    natsThread          *t   = NULL;
    natsStatus          s    = NATS_OK;
    int                 err;

    ctx = (struct threadCtx*) NATS_CALLOC(1, sizeof(*ctx));
    t = (natsThread*) NATS_CALLOC(1, sizeof(natsThread));

    if ((ctx == NULL) || (t == NULL))
        s = NATS_NO_MEMORY;

    if (s == NATS_OK)
    {
        ctx->entry  = cb;
        ctx->arg    = arg;

        err = pthread_create(t, NULL, _threadStart, ctx);
        if (err)
            s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        *thread = t;
    }
    else
    {
        NATS_FREE(ctx);
        NATS_FREE(t);
    }

    return s;
}

void
natsThread_Join(natsThread *t)
{
    // I think that 'join' should automatically detect if the call is made
    // from the current thread. This simplify the use. That is, you don't
    // need to do:
    // if (!natsThread_IsCurrent(t))
    //     natsThread_Join(t)

    if (!natsThread_IsCurrent(t))
    {
        if (pthread_join(*t, NULL) != 0)
            abort();
    }
    else
    {
        pthread_detach(*t);
    }
}

bool
natsThread_IsCurrent(natsThread *t)
{
    if (pthread_equal(pthread_self(), *t) == 0)
        return false;

    return true;
}

void
natsThread_Destroy(natsThread *t)
{
    if (t == NULL)
        return;

    NATS_FREE(t);
}

