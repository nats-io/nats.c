// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"
#include "../mem.h"

natsStatus
natsMutex_Create(natsMutex **newMutex)
{
    natsStatus          s = NATS_OK;
    pthread_mutexattr_t attr;
    natsMutex           *m = NATS_CALLOC(1, sizeof(natsMutex));
    bool                noAttrDestroy = false;

    if (m == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if ((s == NATS_OK)
        && (pthread_mutexattr_init(&attr) != 0)
        && (noAttrDestroy = true))
    {
        s = nats_setError(NATS_SYS_ERROR, "pthread_mutexattr_init error: %d",
                          errno);
    }

    if ((s == NATS_OK)
        && (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0))
    {
        s = nats_setError(NATS_SYS_ERROR, "pthread_mutexattr_settype error: %d",
                          errno);
    }

    if ((s == NATS_OK)
        && (pthread_mutex_init(m, &attr) != 0))
    {
        s = nats_setError(NATS_SYS_ERROR, "pthread_mutex_init error: %d",
                          errno);
    }

    if (!noAttrDestroy)
        pthread_mutexattr_destroy(&attr);

    if (s == NATS_OK)
        *newMutex = m;
    else
        NATS_FREE(m);

    return s;
}

bool
natsMutex_TryLock(natsMutex *m)
{
    if (pthread_mutex_trylock(m) == 0)
        return true;

    return false;
}

void
natsMutex_Lock(natsMutex *m)
{
    // The "rep" instruction used for spinning is not supported on ARM.
#ifndef __arm__
    if (gLockSpinCount > 0)
    {
        int64_t attempts = 0;

        while (pthread_mutex_trylock(m) != 0)
        {
            if (++attempts <= gLockSpinCount)
            {
                __asm__ __volatile__ ("rep; nop");
            }
            else
            {
                if (pthread_mutex_lock(m))
                    abort();

                break;
            }
        }
    }
    else
#endif
    {
        if (pthread_mutex_lock(m))
            abort();
    }
}


void
natsMutex_Unlock(natsMutex *m)
{
    if (pthread_mutex_unlock(m))
        abort();
}

void
natsMutex_Destroy(natsMutex *m)
{
    if (m == NULL)
        return;

    pthread_mutex_destroy(m);
    NATS_FREE(m);
}
