// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"

#include <process.h>

#include "../mem.h"

static BOOL CALLBACK
_initHandleFunction(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext)
{
    natsInitOnceCb cb = (natsInitOnceCb) Parameter;

    (*cb)();

	return TRUE;
}

bool
nats_InitOnce(natsInitOnceType *control, natsInitOnceCb cb)
{
    BOOL  bStatus;

    // Execute the initialization callback function
    bStatus = InitOnceExecuteOnce(control,
                                  _initHandleFunction,
                                  (PVOID) cb,
                                  NULL);

    // InitOnceExecuteOnce function succeeded.
    if (bStatus)
        return true;

    return false;
}

struct threadCtx
{
    natsThreadCb    entry;
    void            *arg;
};

static unsigned __stdcall _threadStart(void* arg)
{
  struct threadCtx *c = (struct threadCtx*) arg;

  c->entry(c->arg);

  NATS_FREE(c);

  return 0;
}

natsStatus
natsThread_Create(natsThread **thread, natsThreadCb cb, void *arg)
{
    struct threadCtx    *ctx = NULL;
    natsThread          *t   = NULL;
    natsStatus          s    = NATS_OK;

    ctx = (struct threadCtx*) NATS_CALLOC(1, sizeof(*ctx));
    t = (natsThread*) NATS_CALLOC(1, sizeof(natsThread));

    if ((ctx == NULL) || (t == NULL))
        s = NATS_NO_MEMORY;

    if (s == NATS_OK)
    {
        ctx->entry  = cb;
        ctx->arg    = arg;

        t->t = (HANDLE) _beginthreadex(NULL, 0, _threadStart, ctx, 0, NULL);
        if (t->t == NULL)
            s = NATS_NO_MEMORY;
        else
            t->id = GetThreadId(t->t);
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
    if (GetCurrentThreadId() != t->id)
    {
        if (WaitForSingleObject(t->t, INFINITE))
            abort();
    }
}

void
natsThread_Detach(natsThread *t)
{
    // nothing for now.
}

bool
natsThread_IsCurrent(natsThread *t)
{
    if (GetCurrentThreadId() == t->id)
        return true;

    return false;
}

void
natsThread_Destroy(natsThread *t)
{
    if (t == NULL)
        return;

    CloseHandle(t->t);

    NATS_FREE(t);
}

