// Copyright 2018 The NATS Authors
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

#include "sopts.h"
#include "../opts.h"

static void
_stanSubOpts_free(stanSubOptions *opts)
{
    if (opts == NULL)
        return;

    NATS_FREE(opts->durableName);
    natsMutex_Destroy(opts->mu);
    NATS_FREE(opts);
}

natsStatus
stanSubOptions_Create(stanSubOptions **newOpts)
{
    natsStatus      s = NATS_OK;
    stanSubOptions  *opts = NULL;

    // Ensure the library is loaded
    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    opts = (stanSubOptions*) NATS_CALLOC(1, sizeof(stanSubOptions));
    if (opts == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (natsMutex_Create(&(opts->mu)) != NATS_OK)
    {
        NATS_FREE(opts);
        return NATS_UPDATE_ERR_STACK(NATS_NO_MEMORY);
    }

    opts->maxInflight = STAN_SUB_OPTS_DEFAULT_MAX_INFLIGHT;
    opts->ackWait = STAN_SUB_OPTS_DEFAULT_ACK_WAIT;
    opts->startAt = PB__START_POSITION__NewOnly;

    if (s == NATS_OK)
        *newOpts = opts;
    else
        _stanSubOpts_free(opts);

    return s;
}

natsStatus
stanSubOptions_SetDurableName(stanSubOptions *opts, const char *durableName)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if (opts->durableName != NULL)
    {
        NATS_FREE(opts->durableName);
        opts->durableName = NULL;
    }
    if (durableName != NULL)
        DUP_STRING(s, opts->durableName, durableName);

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
stanSubOptions_SetAckWait(stanSubOptions *opts, int64_t wait)
{
    LOCK_AND_CHECK_OPTIONS(opts, (wait <= 0));

    opts->ackWait = wait;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_SetMaxInflight(stanSubOptions *opts, int maxInflight)
{
    LOCK_AND_CHECK_OPTIONS(opts, (maxInflight < 1));

    opts->maxInflight = maxInflight;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_StartAtSequence(stanSubOptions *opts, uint64_t seq)
{
    LOCK_AND_CHECK_OPTIONS(opts, (seq < 1));

    opts->startAt = PB__START_POSITION__SequenceStart;
    opts->startSequence = seq;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_StartAtTime(stanSubOptions *opts, int64_t time)
{
    LOCK_AND_CHECK_OPTIONS(opts, (time < 0));

    opts->startAt = PB__START_POSITION__TimeDeltaStart;
    opts->startTime = time;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_StartAtTimeDelta(stanSubOptions *opts, int64_t delta)
{
    LOCK_AND_CHECK_OPTIONS(opts, (delta < 0));

    opts->startAt = PB__START_POSITION__TimeDeltaStart;
    opts->startTime = nats_Now() - delta;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_StartWithLastReceived(stanSubOptions *opts)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->startAt = PB__START_POSITION__LastReceived;

    UNLOCK_OPTS(opts);

    return NATS_OK;

}

natsStatus
stanSubOptions_DeliverAllAvailable(stanSubOptions *opts)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->startAt = PB__START_POSITION__First;

    UNLOCK_OPTS(opts);

    return NATS_OK;

}

natsStatus
stanSubOptions_SetManualAckMode(stanSubOptions *opts, bool manual)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->manualAcks = manual;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
stanSubOptions_clone(stanSubOptions **clonedOpts, stanSubOptions *opts)
{
    natsStatus      s       = NATS_OK;
    stanSubOptions  *cloned = NULL;
    int             muSize;

    s = stanSubOptions_Create(&cloned);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    natsMutex_Lock(opts->mu);

    muSize = sizeof(cloned->mu);

    // Make a blind copy first...
    memcpy((char*)cloned + muSize, (char*)opts + muSize,
           sizeof(stanSubOptions) - muSize);

    // Then remove all pointers, so that if we fail while
    // copying them, and free the cloned, we don't free the pointers
    // from the original.
    cloned->durableName = NULL;

    s = stanSubOptions_SetDurableName(cloned, opts->durableName);

    if (s == NATS_OK)
        *clonedOpts = cloned;
    else
        _stanSubOpts_free(cloned);

    natsMutex_Unlock(opts->mu);

    return s;
}

void
stanSubOptions_Destroy(stanSubOptions *opts)
{
    if (opts == NULL)
        return;

    _stanSubOpts_free(opts);
}
