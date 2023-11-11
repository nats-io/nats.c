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

natsStatus
natsMutex_Create(natsMutex **newMutex)
{
    natsStatus          s = NATS_OK;
    pthread_mutexattr_t attr;
    natsMutex           *m = NATS_CALLOC(1, sizeof(natsMutex));

    if (m == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (pthread_mutexattr_init(&attr) != 0)
    {
        NATS_FREE(m);
        return nats_setError(NATS_SYS_ERROR, "pthread_mutexattr_init error: %d",
                             errno);
    }

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
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

    if (s == NATS_OK)
        *newMutex = m;
    else
    {
        NATS_FREE(m);
        pthread_mutexattr_destroy(&attr);
    }

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
#if !defined(NATS_NO_SPIN)
    if (gLockSpinCount > 0)
    {
        int64_t attempts = 0;
        while (pthread_mutex_trylock(m) != 0)
        {
            if (++attempts <= gLockSpinCount)
            {
                #if defined(__x86_64__) || \
                    defined(__mips__)
                    __asm__ __volatile__ ("pause" ::: "memory");
                #elif (defined(__arm__) && __ARM_ARCH >=6) || \
                      defined(__aarch64__)
                    __asm__ __volatile__ ("yield" ::: "memory");
                #elif defined(__powerpc__) || \
                      defined(__powerpc64__)
                    __asm__ __volatile__ ("or 27,27,27" ::: "memory");
                #else
                    usleep(0);
                #endif
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
