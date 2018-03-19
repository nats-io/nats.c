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

    nats_ReleaseThreadMemory();

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
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if (s == NATS_OK)
    {
        ctx->entry  = cb;
        ctx->arg    = arg;

        err = pthread_create(t, NULL, _threadStart, ctx);
        if (err)
            s = nats_setError(NATS_SYS_ERROR,
                              "pthread_create error: %d", errno);
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

void
natsThread_Detach(natsThread *t)
{
    if (pthread_detach(*t) !=0)
        abort();
}

bool
natsThread_IsCurrent(natsThread *t)
{
    if (pthread_equal(pthread_self(), *t) == 0)
        return false;

    return true;
}

void
natsThread_Yield(void)
{
    sched_yield();
}

void
natsThread_Destroy(natsThread *t)
{
    if (t == NULL)
        return;

    NATS_FREE(t);
}

natsStatus
natsThreadLocal_CreateKey(natsThreadLocal *tl, void (*destructor)(void*))
{
    int ret;

    if ((ret = pthread_key_create(tl, destructor)) != 0)
    {
        return nats_setError(NATS_SYS_ERROR,
                             "pthread_key_create error: %d", ret);
    }

    return NATS_OK;
}

void*
natsThreadLocal_Get(natsThreadLocal tl)
{
    return pthread_getspecific(tl);
}

natsStatus
natsThreadLocal_SetEx(natsThreadLocal tl, const void *value, bool setErr)
{
    int ret;

    if ((ret = pthread_setspecific(tl, value)) != 0)
    {
        return nats_setError(NATS_SYS_ERROR,
                             "pthread_setspecific: %d",
                             ret);
    }

    return NATS_OK;
}

void
natsThreadLocal_DestroyKey(natsThreadLocal tl)
{
    pthread_key_delete(tl);
}

