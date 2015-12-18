// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"
#include "../mem.h"

natsStatus
natsMutex_Create(natsMutex **newMutex)
{
    natsMutex *m = NATS_CALLOC(1, sizeof(natsMutex));

    if (m == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (gLockSpinCount > 0)
        InitializeCriticalSectionAndSpinCount(m, (DWORD) gLockSpinCount);
    else
        InitializeCriticalSection(m);
    *newMutex = m;

    return NATS_OK;
}

bool
natsMutex_TryLock(natsMutex *m)
{
    if (TryEnterCriticalSection(m) == 0)
        return false;

    return true;
}

void
natsMutex_Lock(natsMutex *m)
{
    // If gLockSpinCount > 0, the mutex has been created as as spin lock,
    // so we don't have to do anything special here.
    EnterCriticalSection(m);
}

void
natsMutex_Unlock(natsMutex *m)
{
    LeaveCriticalSection(m);
}

void
natsMutex_Destroy(natsMutex *m)
{
    if (m == NULL)
        return;

    DeleteCriticalSection(m);
    NATS_FREE(m);
}
