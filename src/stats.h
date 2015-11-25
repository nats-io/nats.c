// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef STATS_H_
#define STATS_H_

#include <stdint.h>

#include "status.h"

struct __natsStatistics
{
    uint64_t    inMsgs;
    uint64_t    outMsgs;
    uint64_t    inBytes;
    uint64_t    outBytes;
    uint64_t    reconnects;

};

#endif /* STATS_H_ */
