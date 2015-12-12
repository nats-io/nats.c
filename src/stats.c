// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <stdlib.h>
#include "status.h"
#include "stats.h"
#include "mem.h"

natsStatus
natsStatistics_Create(natsStatistics **newStats)
{
    natsStatistics *stats = NULL;

    stats = (natsStatistics*) NATS_CALLOC(1, sizeof(natsStatistics));
    if (stats == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    *newStats = stats;

    return NATS_OK;
}

natsStatus
natsStatistics_GetCounts(natsStatistics *stats,
                         uint64_t *inMsgs, uint64_t *inBytes,
                         uint64_t *outMsgs, uint64_t *outBytes,
                         uint64_t *reconnects)
{
    if (stats == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (inMsgs != NULL)
        *inMsgs = stats->inMsgs;
    if (inBytes != NULL)
        *inBytes = stats->inBytes;
    if (outMsgs != NULL)
        *outMsgs = stats->outMsgs;
    if (outBytes != NULL)
        *outBytes = stats->outBytes;
    if (reconnects != NULL)
        *reconnects = stats->reconnects;

    return NATS_OK;
}

void
natsStatistics_Destroy(natsStatistics *stats)
{
    if (stats == NULL)
        return;

    NATS_FREE(stats);
}
