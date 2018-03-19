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
natsCondition_Create(natsCondition **cond)
{
    natsCondition   *c = (natsCondition*) NATS_CALLOC(1, sizeof(natsCondition));
    natsStatus      s  = NATS_OK;

    if (c == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    InitializeConditionVariable(c);
    *cond = c;

    return s;
}

void
natsCondition_Wait(natsCondition *cond, natsMutex *mutex)
{
    if (SleepConditionVariableCS(cond, mutex, INFINITE) == 0)
        abort();
}

natsStatus
natsCondition_TimedWait(natsCondition *cond, natsMutex *mutex, int64_t timeout)
{
    if (timeout <= 0)
        return NATS_TIMEOUT;

    if (SleepConditionVariableCS(cond, mutex, (DWORD) timeout) == 0)
    {
        if (GetLastError() == ERROR_TIMEOUT)
            return NATS_TIMEOUT;

        abort();
    }

    return NATS_OK;
}

natsStatus
natsCondition_AbsoluteTimedWait(natsCondition *cond, natsMutex *mutex, int64_t absoluteTime)
{
    int64_t now = nats_Now();;
    int64_t sleepTime = absoluteTime - now;

    if (sleepTime <= 0)
        return NATS_TIMEOUT;

    if (SleepConditionVariableCS(cond, mutex, (DWORD) sleepTime) == 0)
    {
        if (GetLastError() == ERROR_TIMEOUT)
            return NATS_TIMEOUT;

        return nats_setError(NATS_SYS_ERROR,
                             "SleepConditionVariableCS error: %d",
                             GetLastError());
    }

    return NATS_OK;
}

void
natsCondition_Signal(natsCondition *cond)
{
    WakeConditionVariable(cond);
}

void
natsCondition_Broadcast(natsCondition *cond)
{
    WakeAllConditionVariable(cond);
}

void
natsCondition_Destroy(natsCondition *cond)
{
    if (cond == NULL)
        return;

    NATS_FREE(cond);
}
