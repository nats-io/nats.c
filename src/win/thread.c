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

  nats_ReleaseThreadMemory();

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
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if (s == NATS_OK)
    {
        ctx->entry  = cb;
        ctx->arg    = arg;

        t->t = (HANDLE) _beginthreadex(NULL, 0, _threadStart, ctx, 0, NULL);
        if (t->t == NULL)
            s = nats_setError(NATS_SYS_ERROR,
                              "_beginthreadex error: %d",
                              GetLastError());
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
natsThread_Yield()
{
    // The correct way would be to call the following function.
    // However, it looks like the connection reconnect test
    // is failing on Windows without a proper sleep (that is,
    // even Sleep(0) does not help).
//    SwitchToThread();
    nats_Sleep(1);
}

void
natsThread_Destroy(natsThread *t)
{
    if (t == NULL)
        return;

    CloseHandle(t->t);

    NATS_FREE(t);
}

natsStatus
natsThreadLocal_CreateKey(natsThreadLocal *tl, void (*destructor)(void*))
{
    if ((*tl = TlsAlloc()) == TLS_OUT_OF_INDEXES)
    {
        return nats_setError(NATS_SYS_ERROR,
                             "TlsAlloc error: %d",
                              GetLastError());
    }

    return NATS_OK;
}

void*
natsThreadLocal_Get(natsThreadLocal tl)
{
    return (void*) TlsGetValue(tl);
}

natsStatus
natsThreadLocal_SetEx(natsThreadLocal tl, const void *value, bool setErr)
{
    if (TlsSetValue(tl, (LPVOID) value) == 0)
    {
        if (setErr)
            return nats_setError(NATS_SYS_ERROR,
                                 "TlsSetValue error: %d",
                                 GetLastError());
        else
            return NATS_SYS_ERROR;
    }

    return NATS_OK;
}

void
natsThreadLocal_DestroyKey(natsThreadLocal tl)
{
    TlsFree(tl);
}

