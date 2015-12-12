// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"

#include <errno.h>

#include "../util.h"
#include "../mem.h"

natsStatus
natsCondition_Create(natsCondition **cond)
{
    natsCondition   *c = (natsCondition*) NATS_CALLOC(1, sizeof(natsCondition));
    natsStatus      s  = NATS_OK;

    if (c == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (pthread_cond_init(c, NULL) != 0)
        s = nats_setError(NATS_SYS_ERROR, "pthread_cond_init error: %d", errno);

    if (s == NATS_OK)
        *cond = c;
    else
        NATS_FREE(c);

    return s;
}

void
natsCondition_Wait(natsCondition *cond, natsMutex *mutex)
{
    if (pthread_cond_wait(cond, mutex) != 0)
        abort();
}

static natsStatus
_timedWait(natsCondition *cond, natsMutex *mutex, bool isAbsolute, int64_t timeout)
{
    int     r;
    struct  timespec ts;
    int64_t target;

    if (timeout <= 0)
        return NATS_TIMEOUT;

    target = (isAbsolute ? timeout : (nats_Now() + timeout));

    ts.tv_sec = target / 1000;
    ts.tv_nsec = (target % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    r = pthread_cond_timedwait(cond, mutex, &ts);

    if (r == 0)
        return NATS_OK;

    if (r == ETIMEDOUT)
        return NATS_TIMEOUT;

    return nats_setError(NATS_SYS_ERROR, "pthread_cond_timedwait error: %d", errno);
}

natsStatus
natsCondition_TimedWait(natsCondition *cond, natsMutex *mutex, int64_t timeout)
{
    return _timedWait(cond, mutex, false, timeout);
}

natsStatus
natsCondition_AbsoluteTimedWait(natsCondition *cond, natsMutex *mutex, int64_t absoluteTime)
{
    return _timedWait(cond, mutex, true, absoluteTime);
}

void
natsCondition_Signal(natsCondition *cond)
{
    if (pthread_cond_signal(cond) != 0)
      abort();
}

void
natsCondition_Broadcast(natsCondition *cond)
{
    if (pthread_cond_broadcast(cond) != 0)
      abort();
}

void
natsCondition_Destroy(natsCondition *cond)
{
    if (cond == NULL)
        return;

    pthread_cond_destroy(cond);
    NATS_FREE(cond);
}
