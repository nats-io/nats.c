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
