// Copyright 2015 Apcera Inc. All rights reserved.

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
        return NATS_NO_MEMORY;

    t->refs         = 1;
    t->cb           = timerCb;
    t->stopCb       = stopCb;
    t->interval     = interval;
    t->absoluteTime = nats_Now() + interval;
    t->closure      = closure;

    s = natsMutex_Create(&(t->mu));
    if (s == NATS_OK)
    {
        *timer = t;

        t->refs++;
        nats_AddTimer(t);
    }
    else
        _freeTimer(t);

    return s;
}

static void
_stop(natsTimer *timer, bool duringReset)
{
    bool doCb = false;

    natsMutex_Lock(timer->mu);

    if (timer->stopped)
    {
        natsMutex_Unlock(timer->mu);
        return;
    }

    timer->stopped = true;
    doCb = !duringReset && !(timer->inCallback);

    natsMutex_Unlock(timer->mu);

    nats_RemoveTimer(timer);

    if (doCb)
    {
        (*(timer->stopCb))(timer, timer->closure);

        natsTimer_Release(timer);
    }
}

void
natsTimer_Stop(natsTimer *timer)
{
    _stop(timer, false);
}

void
natsTimer_Reset(natsTimer *timer, int64_t interval)
{
    _stop(timer, true);

    natsMutex_Lock(timer->mu);

    timer->stopped      = false;
    timer->interval     = interval;
    timer->absoluteTime = nats_Now() + interval;

    if (!timer->inCallback)
        timer->refs++;

    natsMutex_Unlock(timer->mu);

    nats_AddTimer(timer);
}


void
natsTimer_Destroy(natsTimer *timer)
{
    bool doStop = false;

    if (timer == NULL)
        return;

    natsMutex_Lock(timer->mu);

    if (!(timer->stopped))
        doStop = true;

    natsMutex_Unlock(timer->mu);

    if (doStop)
        natsTimer_Stop(timer);

    natsTimer_Release(timer);
}
