// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "timer.h"
#include "util.h"
#include "asynccb.h"

#define WAIT_LIB_INITIALIZED \
        natsMutex_Lock(gLib.lock); \
        while (!(gLib.initialized)) \
            natsCondition_Wait(gLib.cond, gLib.lock); \
        natsMutex_Unlock(gLib.lock)

typedef struct __natsLibTimers
{
    natsMutex       *lock;
    natsCondition   *cond;
    natsThread      *thread;
    natsTimer       *timers;
    int             count;
    bool            changed;
    bool            shutdown;

} natsLibTimers;

typedef struct __natsLibAsyncCbs
{
    natsMutex       *lock;
    natsCondition   *cond;
    natsThread      *thread;
    natsAsyncCbInfo *head;
    natsAsyncCbInfo *tail;
    bool            shutdown;

} natsLibAsyncCbs;

typedef struct __natsGCList
{
    natsMutex       *lock;
    natsCondition   *cond;
    natsThread      *thread;
    natsGCItem      *head;
    bool            shutdown;
    bool            inWait;

} natsGCList;

typedef struct __natsLib
{
    // These 4 fields MUST NOT be moved!
    natsMutex       *lock;
    bool            initialized;
    bool            closed;
    int             refs;

    natsLibTimers   timers;
    natsLibAsyncCbs asyncCbs;

    natsCondition   *cond;

    natsMutex       *inboxesLock;
    uint64_t        inboxesSeq;

    natsGCList      gc;

} natsLib;

int64_t gLockSpinCount = 2000;

static const char *inboxPrefix = "_INBOX.";

static natsInitOnceType gInitOnce = NATS_ONCE_STATIC_INIT;
static natsLib          gLib;


static void
_freeTimers(void)
{
    natsLibTimers *timers = &(gLib.timers);

    natsThread_Destroy(timers->thread);
    natsCondition_Destroy(timers->cond);
    natsMutex_Destroy(timers->lock);
}

static void
_freeAsyncCbs(void)
{
    natsLibAsyncCbs *cbs = &(gLib.asyncCbs);

    natsThread_Destroy(cbs->thread);
    natsCondition_Destroy(cbs->cond);
    natsMutex_Destroy(cbs->lock);
}

static void
_freeGC(void)
{
    natsGCList *gc = &(gLib.gc);

    natsThread_Destroy(gc->thread);
    natsCondition_Destroy(gc->cond);
    natsMutex_Destroy(gc->lock);
}

static void
_freeLib(void)
{
    _freeTimers();
    _freeAsyncCbs();
    _freeGC();

    natsMutex_Destroy(gLib.inboxesLock);
    natsCondition_Destroy(gLib.cond);

    memset(&(gLib.refs), 0, sizeof(natsLib) - ((char *)&(gLib.refs) - (char*)&gLib));

    natsMutex_Lock(gLib.lock);
    gLib.closed = false;
    gLib.initialized = false;
    natsMutex_Unlock(gLib.lock);
}

void
natsLib_Retain(void)
{
    natsMutex_Lock(gLib.lock);

    gLib.refs++;

    natsMutex_Unlock(gLib.lock);
}

void
natsLib_Release(void)
{
    int refs = 0;

    natsMutex_Lock(gLib.lock);

    refs = --(gLib.refs);

    natsMutex_Unlock(gLib.lock);

    if (refs == 0)
        _freeLib();
}

static void
_doInitOnce(void)
{
    memset(&gLib, 0, sizeof(natsLib));

    if (natsMutex_Create(&(gLib.lock)) != NATS_OK)
    {
        fprintf(stderr, "FATAL ERROR: Unable to initialize library!\n");
        fflush(stderr);
        abort();
    }

    natsSys_Init();
}

static void
_insertTimer(natsTimer *t)
{
    natsTimer *cur  = gLib.timers.timers;
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
        gLib.timers.timers = t;
}

// Locks must be held before entering this function
static void
_removeTimer(natsLibTimers *timers, natsTimer *t)
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

        if (t == gLib.timers.timers)
            gLib.timers.timers = t->next;

        t->prev = NULL;
        t->next = NULL;
    }

    // Decrease the global count of timers
    timers->count--;
}

void
nats_resetTimer(natsTimer *t, int64_t newInterval)
{
    natsLibTimers *timers = &(gLib.timers);

    natsMutex_Lock(timers->lock);
    natsMutex_Lock(t->mu);

    // If timer is active, we need first to remove it. This call does the
    // right thing if the timer is in the callback.
    if (!(t->stopped))
        _removeTimer(timers, t);

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
        t->absoluteTime = nats_Now() + t->interval;
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
    natsLibTimers   *timers = &(gLib.timers);
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

    _removeTimer(timers, t);

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
    int         count = 0;

    natsMutex_Lock(gLib.timers.lock);

    count = gLib.timers.count;

    natsMutex_Unlock(gLib.timers.lock);

    return count;
}

int
nats_getTimersCountInList(void)
{
    int         count = 0;
    natsTimer   *t;

    natsMutex_Lock(gLib.timers.lock);

    t = gLib.timers.timers;
    while (t != NULL)
    {
        count++;
        t = t->next;
    }

    natsMutex_Unlock(gLib.timers.lock);

    return count;
}


static void
_timerThread(void *arg)
{
    natsLibTimers   *timers = &(gLib.timers);
    natsTimer       *t      = NULL;
    natsStatus      s       = NATS_OK;
    bool            doStopCb;
    int64_t         target;

    WAIT_LIB_INITIALIZED;

    natsMutex_Lock(timers->lock);

    while (!(timers->shutdown))
    {
        // Take the first timer that needs to fire.
        t = timers->timers;

        if (t == NULL)
        {
            // No timer, fire in an hour...
            target = nats_Now() + 3600 * 1000;
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
            t->absoluteTime = nats_Now() + t->interval;
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
        _removeTimer(timers, t);

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

static void
_asyncCbsThread(void *arg)
{
    natsLibAsyncCbs *asyncCbs = &(gLib.asyncCbs);
    natsAsyncCbInfo *cb       = NULL;
    natsConnection  *nc       = NULL;

    WAIT_LIB_INITIALIZED;

    natsMutex_Lock(asyncCbs->lock);

    while (!(asyncCbs->shutdown))
    {
        while (!(asyncCbs->shutdown)
               && ((cb = asyncCbs->head) == NULL))
        {
            natsCondition_Wait(asyncCbs->cond, asyncCbs->lock);
        }

        if (asyncCbs->shutdown)
            break;

        asyncCbs->head = cb->next;

        if (asyncCbs->tail == cb)
            asyncCbs->tail = NULL;

        cb->next = NULL;

        natsMutex_Unlock(asyncCbs->lock);

        nc = cb->nc;

        switch (cb->type)
        {
            case ASYNC_CLOSED:
               (*(nc->opts->closedCb))(nc, nc->opts->closedCbClosure);
               break;
            case ASYNC_DISCONNECTED:
                (*(nc->opts->disconnectedCb))(nc, nc->opts->disconnectedCbClosure);
                break;
            case ASYNC_RECONNECTED:
                (*(nc->opts->reconnectedCb))(nc, nc->opts->reconnectedCbClosure);
                break;
            case ASYNC_ERROR:
                (*(nc->opts->asyncErrCb))(nc, cb->sub, cb->err, nc->opts->asyncErrCbClosure);
                break;
            default:
                break;
        }

        natsAsyncCb_Destroy(cb);

        natsMutex_Lock(asyncCbs->lock);
    }

    while ((cb = asyncCbs->head) != NULL)
    {
        asyncCbs->head = cb->next;

        natsAsyncCb_Destroy(cb);
    }

    natsMutex_Unlock(asyncCbs->lock);

    natsLib_Release();
}

natsStatus
nats_postAsyncCbInfo(natsAsyncCbInfo *info)
{
    natsMutex_Lock(gLib.asyncCbs.lock);

    if (gLib.asyncCbs.shutdown)
    {
        natsMutex_Unlock(gLib.asyncCbs.lock);
        return NATS_NOT_INITIALIZED;
    }

    info->next = NULL;

    if (gLib.asyncCbs.head == NULL)
        gLib.asyncCbs.head = info;

    if (gLib.asyncCbs.tail != NULL)
        gLib.asyncCbs.tail->next = info;

    gLib.asyncCbs.tail = info;

    natsCondition_Signal(gLib.asyncCbs.cond);

    natsMutex_Unlock(gLib.asyncCbs.lock);

    return NATS_OK;
}

static void
_garbageCollector(void *closure)
{
    natsGCList *gc = &(gLib.gc);
    natsGCItem *item;
    natsGCItem *list;

    WAIT_LIB_INITIALIZED;

    natsMutex_Lock(gc->lock);

    // Repeat until notified to shutdown.
    while (!(gc->shutdown))
    {
        // Go into wait until we are notified to shutdown
        // or there is something to garbage collect
        gc->inWait = true;

        while (!(gc->shutdown) && (gc->head == NULL))
        {
            natsCondition_Wait(gc->cond, gc->lock);
        }

        // Out of the wait. Setting this boolean avoids unnecessary
        // signaling when an item is added to the collector.
        gc->inWait = false;

        // Do not break out on shutdown here, we want to clear the list,
        // even on exit so that valgrind and the like are happy.

        // Under the lock, we will switch to a local list and reset the
        // GC's list (so that others can add to the list without contention
        // (at least from the GC itself).
        do
        {
            list = gc->head;
            gc->head = NULL;

            natsMutex_Unlock(gc->lock);

            // Now that we are outside of the lock, we can empty the list.
            while ((item = list) != NULL)
            {
                // Pops item from the beginning of the list.
                list = item->next;
                item->next = NULL;

                // Invoke the freeCb associated with this object
                (*(item->freeCb))((void*) item);
            }

            natsMutex_Lock(gc->lock);
        }
        while (gc->head != NULL);
    }

    natsMutex_Unlock(gc->lock);

    natsLib_Release();
}

bool
natsGC_collect(natsGCItem *item)
{
    natsGCList  *gc;
    bool        signal;

    // If the object was not setup for garbage collection, return false
    // so the caller frees the object.
    if (item->freeCb == NULL)
        return false;

    gc = &(gLib.gc);

    natsMutex_Lock(gc->lock);

    // We will signal only if the GC is in the condition wait.
    signal = gc->inWait;

    // Add to the front of the list.
    item->next = gc->head;

    // Update head.
    gc->head = item;

    if (signal)
        natsCondition_Signal(gc->cond);

    natsMutex_Unlock(gc->lock);

    return true;
}

static void
_libTearDown(void)
{
    if (gLib.timers.thread != NULL)
        natsThread_Join(gLib.timers.thread);

    if (gLib.asyncCbs.thread != NULL)
        natsThread_Join(gLib.asyncCbs.thread);

    if (gLib.gc.thread != NULL)
        natsThread_Join(gLib.gc.thread);

    natsLib_Release();
}

natsStatus
nats_Open(int64_t lockSpinCount)
{
    natsStatus s = NATS_OK;

    if (!nats_InitOnce(&gInitOnce, _doInitOnce))
        return NATS_FAILED_TO_INITIALIZE;

    natsMutex_Lock(gLib.lock);

    if (gLib.closed || gLib.initialized)
    {
        if (gLib.closed)
            s = NATS_FAILED_TO_INITIALIZE;

        natsMutex_Unlock(gLib.lock);
        return s;
    }

    signal(SIGPIPE, SIG_IGN);

    srand((unsigned int) nats_NowInNanoSeconds());

    gLib.refs = 1;

    // If the caller specifies negative value, then we use the default
    if (lockSpinCount >= 0)
        gLockSpinCount = lockSpinCount;

    s = natsCondition_Create(&(gLib.cond));
    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.inboxesLock));

    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.timers.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.timers.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.timers.thread), _timerThread, NULL);
        if (s == NATS_OK)
            gLib.refs++;
    }

    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.asyncCbs.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.asyncCbs.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.asyncCbs.thread), _asyncCbsThread, NULL);
        if (s == NATS_OK)
            gLib.refs++;
    }
    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.gc.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.gc.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.gc.thread), _garbageCollector, NULL);
        if (s == NATS_OK)
            gLib.refs++;
    }

    gLib.initialized = true;

    // In case of success or error, broadcast so that lib's threads
    // can proceed.
    if (gLib.cond != NULL)
    {
        if (s != NATS_OK)
        {
            gLib.timers.shutdown = true;
            gLib.asyncCbs.shutdown = true;
            gLib.gc.shutdown = true;
        }
        natsCondition_Broadcast(gLib.cond);
    }

    natsMutex_Unlock(gLib.lock);

    if (s != NATS_OK)
        _libTearDown();

    return s;
}

natsStatus
natsInbox_Create(natsInbox **newInbox)
{
    natsStatus  s      = NATS_OK;
    char        *inbox = NULL;
    int         res    = 0;

    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    natsMutex_Lock(gLib.inboxesLock);

    res = asprintf(&inbox, "%s%x.%" NATS_PRINTF_U64, inboxPrefix, rand(), ++(gLib.inboxesSeq));
    if (res < 0)
        s = NATS_NO_MEMORY;
    else
        *newInbox = inbox;

    natsMutex_Unlock(gLib.inboxesLock);

    return s;
}

void
natsInbox_Destroy(natsInbox *inbox)
{
    if (inbox == NULL)
        return;

    NATS_FREE(inbox);
}


void
nats_Close(void)
{
    natsMutex_Lock(gLib.lock);

    if (gLib.closed)
    {
        natsMutex_Unlock(gLib.lock);
        return;
    }

    gLib.closed = true;

    natsMutex_Lock(gLib.timers.lock);
    gLib.timers.shutdown = true;
    natsCondition_Signal(gLib.timers.cond);
    natsMutex_Unlock(gLib.timers.lock);

    natsMutex_Lock(gLib.asyncCbs.lock);
    gLib.asyncCbs.shutdown = true;
    natsCondition_Signal(gLib.asyncCbs.cond);
    natsMutex_Unlock(gLib.asyncCbs.lock);

    natsMutex_Lock(gLib.gc.lock);
    gLib.gc.shutdown = true;
    natsCondition_Signal(gLib.gc.cond);
    natsMutex_Unlock(gLib.gc.lock);

    natsMutex_Unlock(gLib.lock);

    _libTearDown();
}
