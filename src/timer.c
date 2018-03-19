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

#include "natsp.h"
#include "mem.h"
#include "util.h"

static void
_freeTimer(natsTimer *t)
{
    if (t == NULL)
        return;

    natsMutex_Destroy(t->mu);
    NATS_FREE(t);
}

void
natsTimer_Release(natsTimer *t)
{
    int refs = 0;

    natsMutex_Lock(t->mu);

    refs = --(t->refs);

    natsMutex_Unlock(t->mu);

    if (refs == 0)
        _freeTimer(t);
}

natsStatus
natsTimer_Create(natsTimer **timer, natsTimerCb timerCb, natsTimerStopCb stopCb,
                 int64_t interval, void* closure)
{
    natsStatus  s  = NATS_OK;
    natsTimer   *t = (natsTimer*) NATS_CALLOC(1, sizeof(natsTimer));

    if (t == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    t->refs    = 1;
    t->cb      = timerCb;
    t->stopCb  = stopCb;
    t->closure = closure;

    s = natsMutex_Create(&(t->mu));
    if (s == NATS_OK)
    {
        // Doing so, so that nats_resetTimer() does not try to remove the timer
        // from the list (since it is new it would not be there!).
        t->stopped = true;

        nats_resetTimer(t, interval);

        *timer = t;
    }
    else
        _freeTimer(t);

    return NATS_UPDATE_ERR_STACK(s);
}

void
natsTimer_Stop(natsTimer *timer)
{
    // Proxy for this call:
    nats_stopTimer(timer);
}

void
natsTimer_Reset(natsTimer *timer, int64_t interval)
{
    // Proxy for this call:
    nats_resetTimer(timer, interval);
}

void
natsTimer_Destroy(natsTimer *timer)
{
    if (timer == NULL)
        return;

    nats_stopTimer(timer);
    natsTimer_Release(timer);
}
