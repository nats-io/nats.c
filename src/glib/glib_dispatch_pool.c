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
#include "../sub.h"

static inline void
_destroyDispatcher(natsDispatcher *d)
{
    if (d == NULL)
        return;

    natsThread_Destroy(d->thread);
    nats_destroyQueuedMessages(&d->queue); // there's NEVER anything there, remove?
    natsCondition_Destroy(d->cond);
    natsMutex_Destroy(d->mu);
    NATS_FREE(d);
}

static inline natsStatus
_newDispatcher(natsDispatcher **newDispatcher, void (*threadf)(void *))
{
    natsStatus s = NATS_OK;
    natsDispatcher *d = NATS_CALLOC(1, sizeof(natsDispatcher));
    if (d == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = natsMutex_Create(&d->mu);
    if (s != NATS_OK)
        return s;

    natsCondition_Create(&d->cond);

    natsMutex_Lock(d->mu);
    natsLib_Retain();
    s = natsThread_Create(&d->thread, threadf, (void *)d);
    if (s == NATS_OK)
        d->running = true;
    natsMutex_Unlock(d->mu);

    if (s != NATS_OK)
    {
        _destroyDispatcher(d);
        natsLib_Release();
        return NATS_UPDATE_ERR_STACK(s);
    }

    *newDispatcher = d;
    return NATS_OK;
}

static natsStatus
_growPool(natsDispatcherPool *pool, int cap)
{
    natsStatus s = NATS_OK;

    if (cap <= 0)
        return nats_setError(NATS_ERR, "%s", "Pool size cannot be negative or zero");

    // Do not error on max < workers->maxSize in case we allow shrinking
    // the pool in the future. Make it a no-op for now.
    if (cap > pool->cap)
    {
        natsDispatcher **newDispatchers = NATS_CALLOC(cap, sizeof(natsDispatcher*));
        if (newDispatchers == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        if (s == NATS_OK)
        {
            memcpy(
                newDispatchers,
                pool->dispatchers,
                pool->cap * sizeof(natsDispatcher*));
            NATS_FREE(pool->dispatchers);
            pool->dispatchers = newDispatchers;
            pool->cap = cap;
        }
    }
    return NATS_UPDATE_ERR_STACK(s);
}

void nats_freeDispatcherPool(natsDispatcherPool *pool)
{
    for (int i = 0; i < pool->cap; i++)
        _destroyDispatcher(pool->dispatchers[i]);
    natsMutex_Destroy(pool->lock);
    NATS_FREE(pool->dispatchers);
    memset(pool, 0, sizeof(*pool));
}

natsStatus
nats_initDispatcherPool(natsDispatcherPool *pool, int cap)
{
    natsStatus s = NATS_OK;

    memset(pool, 0, sizeof(*pool));

    s = natsMutex_Create(&pool->lock);
    if (cap > 0)
        IFOK(s, _growPool(pool, cap));

    if (s != NATS_OK)
        nats_freeDispatcherPool(pool);
    return NATS_UPDATE_ERR_STACK(s);
}

void nats_signalDispatcherPoolToShutdown(natsDispatcherPool *pool)
{
    for (int i = 0; i < pool->cap; i++)
    {
        natsDispatcher *d = pool->dispatchers[i];
        if (d == NULL)
            continue;

        nats_lockDispatcher(d);
        d->shutdown = true;
        if (d->cond != NULL)
            natsCondition_Signal(d->cond);
        nats_unlockDispatcher(d);
    }
}

void nats_waitForDispatcherPoolShutdown(natsDispatcherPool *pool)
{
    for (int i = 0; i < pool->cap; i++)
    {
        if (pool->dispatchers[i] != NULL)
            natsThread_Join(pool->dispatchers[i]->thread);
    }
}

natsStatus nats_setMessageDispatcherPoolCap(int max)
{
    natsLib *lib = nats_lib();

    natsMutex_Lock(lib->messageDispatchers.lock);
    natsStatus s = _growPool(&lib->messageDispatchers, max);
    natsMutex_Unlock(lib->messageDispatchers.lock);

    return NATS_UPDATE_ERR_STACK(s);
}

// no lock on sub->mu needed because we are called during subscription creation.
natsStatus
nats_assignSubToDispatch(natsSubscription *sub)
{
    natsLib *lib = nats_lib();
    natsStatus s = NATS_OK;
    natsDispatcherPool *pool = &lib->messageDispatchers;

    natsMutex_Lock(pool->lock);

    if (pool->cap == 0)
        s = nats_setError(NATS_FAILED_TO_INITIALIZE, "%s", "No message dispatchers available, the pool is empty.");

    // Get the next dispatcher
    if (pool->dispatchers[pool->useNext] == NULL)
        s = _newDispatcher(&pool->dispatchers[pool->useNext], nats_dispatchThreadPool);

    // Assign it to the sub.
    if (s == NATS_OK)
        sub->dispatcher = pool->dispatchers[pool->useNext];

    pool->useNext = (pool->useNext + 1) % pool->cap;

    natsMutex_Unlock(pool->lock);

    return NATS_UPDATE_ERR_STACK(s);
}
