// Copyright 2015 Apcera Inc. All rights reserved.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "natsp.h"
#include "mem.h"
#include "timer.h"
#include "util.h"
#include "asynccb.h"

typedef struct __natsLibTimers
{
    natsMutex       *lock;
    natsCondition   *cond;
    natsThread      *thread;
    natsTimer       *timers;
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
_freeLib(void)
{
    _freeTimers();
    _freeAsyncCbs();

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

void
nats_AddTimer(natsTimer *t)
{
    natsLibTimers *timers = &(gLib.timers);

    natsMutex_Lock(timers->lock);

    _insertTimer(t);

    if (!(timers->changed))
        natsCondition_Signal(timers->cond);

    timers->changed = true;

    natsMutex_Unlock(timers->lock);
}

void
nats_RemoveTimer(natsTimer *t)
{
    natsLibTimers *timers = &(gLib.timers);

    natsMutex_Lock(timers->lock);

    if (t->prev != NULL)
            t->prev->next = t->next;
    if (t->next != NULL)
        t->next->prev = t->prev;

    if (t == gLib.timers.timers)
        gLib.timers.timers = t->next;

    t->prev = NULL;
    t->next = NULL;

    if (!(timers->changed))
        natsCondition_Signal(timers->cond);

    timers->changed = true;

    natsMutex_Unlock(timers->lock);
}

static void
_timerThread(void *arg)
{
    natsLibTimers   *timers = &(gLib.timers);
    natsTimer       *t      = NULL;
    natsStatus      s       = NATS_OK;
    bool            doCb;
    int64_t         target;

    natsMutex_Lock(gLib.lock);

    while (!(gLib.initialized))
        natsCondition_Wait(gLib.cond, gLib.lock);

    natsMutex_Unlock(gLib.lock);

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

        doCb = (t->stopped ? false : true);
        if (doCb)
            t->inCallback = true;

        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        if (doCb)
            (*(t->cb))(t, t->closure);

        natsMutex_Lock(timers->lock);
        natsMutex_Lock(t->mu);

        t->inCallback = false;

        // Timer may have been stopped from within the callback,
        // or any time while the timer's lock was not held.
        doCb = (t->stopped);

        if (!doCb)
        {
            // Reset our view of what is the time this timer should
            // fire.
            t->absoluteTime = nats_Now() + t->interval;
            _insertTimer(t);
        }

        natsMutex_Unlock(t->mu);
        natsMutex_Unlock(timers->lock);

        if (doCb)
        {
            (*(t->stopCb))(t, t->closure);

            natsTimer_Release(t);
        }

        natsMutex_Lock(timers->lock);
    }

    while ((t = timers->timers) != NULL)
    {
        timers->timers = t->next;

        natsMutex_Lock(t->mu);
        t->stopped = true;
        t->inCallback = false;
        natsMutex_Unlock(t->mu);

        natsMutex_Unlock(timers->lock);

        (*(t->stopCb))(t, t->closure);

        natsTimer_Release(t);

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

    natsMutex_Lock(gLib.lock);

    while (!(gLib.initialized))
        natsCondition_Wait(gLib.cond, gLib.lock);

    natsMutex_Unlock(gLib.lock);

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
nats_PostAsyncCbInfo(natsAsyncCbInfo *info)
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
_libTearDown(void)
{
    if (gLib.timers.thread != NULL)
        natsThread_Join(gLib.timers.thread);

    if (gLib.asyncCbs.thread != NULL)
        natsThread_Join(gLib.asyncCbs.thread);

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

    gLib.initialized = true;

    // In case of success or error, broadcast so that lib's threads
    // can proceed.
    if (gLib.cond != NULL)
    {
        if (s != NATS_OK)
        {
            gLib.timers.shutdown = true;
            gLib.asyncCbs.shutdown = true;
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

    res = asprintf(&inbox, "%s%x.%llu", inboxPrefix, rand(), ++(gLib.inboxesSeq));
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

    gLib.timers.shutdown = true;
    natsCondition_Signal(gLib.timers.cond);

    gLib.asyncCbs.shutdown = true;
    natsCondition_Signal(gLib.asyncCbs.cond);

    natsMutex_Unlock(gLib.lock);

    _libTearDown();
}
