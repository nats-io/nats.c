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

void nats_freeTimers(natsLib *lib)
{
    natsLibTimers *timers = &(lib->timers);

    natsThread_Destroy(timers->thread);
    natsCondition_Destroy(timers->cond);
    natsMutex_Destroy(timers->lock);
}

static void
_insertTimer(natsTimer *t)
{
    natsTimer *cur  = nats_lib()->timers.timers;
    natsTimer *prev = NULL;

    while ((cur != NULL) && (cur->absoluteTime <= t->absoluteTime))
    {
        prev = cur;
        cur  = cur->next;
    }

    if (cur != NULL)
    {
        t->prev = prev;
        t->next = cur;
        cur->prev = t;

        if (prev != NULL)
            prev->next = t;
    }
    else if (prev != NULL)
    {
        prev->next = t;
        t->prev = prev;
        t->next = NULL;
    }

    if (prev == NULL)
        nats_lib()->timers.timers = t;
}

// Locks must be held before entering this function
static inline void
_removeTimer(natsLib *lib, natsTimer *t)
{
    // Switch flag
    t->stopped = true;

    // It the timer was in the callback, it has already been removed from the
    // list, so skip that.
    if (!(t->inCallback))
    {
        if (t->prev != NULL)
            t->prev->next = t->next;
        if (t->next != NULL)
            t->next->prev = t->prev;

        if (t == lib->timers.timers)
            lib->timers.timers = t->next;

        t->prev = NULL;
        t->next = NULL;
    }

    // Decrease the global count of timers
    lib->timers.count--;
}

void
nats_resetTimer(natsTimer *t, int64_t newInterval)
{
    natsLib       *lib    = nats_lib();
    natsLibTimers *timers = &(lib->timers);

    natsMutex_Lock(timers->lock);
    natsMutex_Lock(t->mu);

    // If timer is active, we need first to remove it. This call does the
    // right thing if the timer is in the callback.
    if (!(t->stopped))
        _removeTimer(lib, t);

    // Bump the timer's global count (it as decreased in the _removeTimers call
    timers->count++;

    // Switch stopped flag
    t->stopped = false;

    // Set the new interval (may be same than it was before, but that's ok)
    t->interval = newInterval;

    // If the timer is in the callback, the insertion and setting of the
    // absolute time will be done by the timer thread when returning from
    // the timer's callback.
    if (!(t->inCallback))
    {
        t->absoluteTime = nats_setTargetTime(t->interval);
        _insertTimer(t);
    }

    natsMutex_Unlock(t->mu);

    if (!(timers->changed))
        natsCondition_Signal(timers->cond);

    timers->changed = true;

    natsMutex_Unlock(timers->lock);
}

void
nats_stopTimer(natsTimer *t)
{
    natsLib         *lib    = nats_lib();
    natsLibTimers   *timers = &(lib->timers);
    bool            doCb    = false;

    natsMutex_Lock(timers->lock);
    natsMutex_Lock(t->mu);

    // If the timer was already stopped, nothing to do.
    if (t->stopped)
    {
        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        return;
    }

    _removeTimer(lib, t);

    doCb = (!(t->inCallback) && (t->stopCb != NULL));

    natsMutex_Unlock(t->mu);

    if (!(timers->changed))
        natsCondition_Signal(timers->cond);

    timers->changed = true;

    natsMutex_Unlock(timers->lock);

    if (doCb)
        (*(t->stopCb))(t, t->closure);
}

int
nats_getTimersCount(void)
{
    natsLib     *lib  = nats_lib();
    int         count = 0;

    natsMutex_Lock(lib->timers.lock);

    count = lib->timers.count;

    natsMutex_Unlock(lib->timers.lock);

    return count;
}

int
nats_getTimersCountInList(void)
{
    natsLib     *lib  = nats_lib();
    int         count = 0;
    natsTimer   *t;

    natsMutex_Lock(lib->timers.lock);

    t = lib->timers.timers;
    while (t != NULL)
    {
        count++;
        t = t->next;
    }

    natsMutex_Unlock(lib->timers.lock);

    return count;
}

void nats_timerThreadf(void *arg)
{
    natsLib         *lib    = (natsLib *)arg;
    natsLibTimers   *timers = &lib->timers;
    natsTimer       *t      = NULL;
    natsStatus      s       = NATS_OK;
    bool            doStopCb;
    int64_t         target;

    WAIT_LIB_INITIALIZED(lib);

    natsMutex_Lock(timers->lock);

    while (!(timers->shutdown))
    {
        // Take the first timer that needs to fire.
        t = timers->timers;

        if (t == NULL)
        {
            // No timer, fire in an hour...
            target = nats_setTargetTime(3600 * 1000);
        }
        else
        {
            target = t->absoluteTime;
        }

        timers->changed = false;

        s = NATS_OK;

        while (!(timers->shutdown)
               && (s != NATS_TIMEOUT)
               && !(timers->changed))
        {
            s = natsCondition_AbsoluteTimedWait(timers->cond, timers->lock,
                                                target);
        }

        if (timers->shutdown)
            break;

        if ((t == NULL) || timers->changed)
            continue;

        natsMutex_Lock(t->mu);

        // Remove timer from the list:
        timers->timers = t->next;
        if (t->next != NULL)
            t->next->prev = NULL;

        t->prev = NULL;
        t->next = NULL;

        t->inCallback = true;

        // Retain the timer, since we are going to release the locks for the
        // callback. The user may "destroy" the timer from there, so we need
        // to be protected with reference counting.
        t->refs++;

        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        (*(t->cb))(t, t->closure);

        natsMutex_Lock(timers->lock);
        natsMutex_Lock(t->mu);

        t->inCallback = false;

        // Timer may have been stopped from within the callback, or during
        // the window the locks were released.
        doStopCb = (t->stopped && (t->stopCb != NULL));

        // If not stopped, we need to put it back in our list
        if (!doStopCb)
        {
            // Reset our view of what is the time this timer should fire
            // because:
            // 1- the callback may have taken longer than it should
            // 2- the user may have called Reset() with a new interval
            t->absoluteTime = nats_setTargetTime(t->interval);
            _insertTimer(t);
        }

        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        if (doStopCb)
            (*(t->stopCb))(t, t->closure);

        // Compensate for the retain that we made before invoking the timer's
        // callback
        natsTimer_Release(t);

        natsMutex_Lock(timers->lock);
    }

    // Process the timers that were left in the list (not stopped) when the
    // library is shutdown.
    while ((t = timers->timers) != NULL)
    {
        natsMutex_Lock(t->mu);

        // Check if we should invoke the callback. Note that although we are
        // releasing the locks below, a timer present in the list here is
        // guaranteed not to have been stopped (because it would not be in
        // the list otherwise, since there is no chance that it is in the
        // timer's callback). So just check if there is a stopCb to invoke.
        doStopCb = (t->stopCb != NULL);

        // Remove the timer from the list.
        _removeTimer(lib, t);

        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        // Invoke the callback now.
        if (doStopCb)
            (*(t->stopCb))(t, t->closure);

        // No release of the timer here. The user is still responsible to call
        // natsTimer_Destroy().

        natsMutex_Lock(timers->lock);
    }

    natsMutex_Unlock(timers->lock);

    natsLib_Release();
}

