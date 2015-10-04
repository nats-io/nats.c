// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef STATS_H_
#define STATS_H_

#include <stdint.h>

#include "status.h"

typedef struct __natsStatistics
{
    uint64_t    inMsgs;
    uint64_t    outMsgs;
    uint64_t    inBytes;
    uint64_t    outBytes;
    uint64_t    reconnects;

} natsStatistics;

natsStatus
natsStatistics_Create(natsStatistics **newStats);

natsStatus
natsStatistics_GetCounts(natsStatistics *stats,
                         uint64_t *inMsgs, uint64_t *inBytes,
                         uint64_t *outMsgs, uint64_t *outBytes,
                         uint64_t *reconnects);

void
natsStatistics_Destroy(natsStatistics *stats);

#endif /* STATS_H_ */
