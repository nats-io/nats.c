// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef EXAMPLES_H_
#define EXAMPLES_H_

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <string.h>
#define strcasecmp  _stricmp
#else
#include <strings.h>
#endif

#include <nats.h>

#define STATS_IN        0x1
#define STATS_OUT       0x2
#define STATS_COUNT     0x4
#define STATS_ERRORS    0x8

static natsStatus
printStats(int mode, natsConnection *conn, natsSubscription *sub,
           natsStatistics *stats, uint64_t count, uint64_t errors)
{
    natsStatus  s = NATS_OK;
    uint64_t    inMsgs, inBytes, outMsgs, outBytes, queued, reconnected;

    s = natsConnection_GetStats(conn, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, &inMsgs, &inBytes,
                                     &outMsgs, &outBytes, &reconnected);
    if ((s == NATS_OK) && (sub != NULL))
    {
        s = natsSubscription_QueuedMsgs(sub, &queued);

        // Since we use AutoUnsubscribe(), when the max has been reached,
        // the subscription is automatically closed, so this call would
        // return "Invalid Subscription". Ignore this error.
        if (s == NATS_INVALID_SUBSCRIPTION)
        {
            s = NATS_OK;
            queued = 0;
        }
    }

    if (s == NATS_OK)
    {
        if (mode & STATS_IN)
        {
            printf("In Msgs: %9" PRIu64 " - "\
                   "In Bytes: %9" PRIu64 " - ", inMsgs, inBytes);
        }
        if (mode & STATS_OUT)
        {
            printf("Out Msgs: %9" PRIu64 " - "\
                   "Out Bytes: %9" PRIu64 " - ", outMsgs, outBytes);
        }
        if (mode & STATS_COUNT)
        {
            printf("Delivered: %9" PRIu64 " - ", count);
        }
        if (sub != NULL)
        {
            printf("Queued: %5" PRIu64 " - ", queued);
        }
        if (mode & STATS_ERRORS)
        {
            printf("Errors: %9" PRIu64 " - ", errors);
        }
        printf("Reconnected: %3" PRIu64 "\n", reconnected);
    }

    return s;
}

static void
printPerf(const char *txt, int64_t count, int64_t start, int64_t elapsed)
{
    if (elapsed == 0)
        elapsed = nats_Now() - start;

    if (elapsed <= 0)
        printf("\nNot enough messages or too fast to report performance!\n");
    else
        printf("\n%s %" PRId64 " messages in "\
               "%" PRId64 " milliseconds (%d msgs/sec)\n",
               txt, count, elapsed, (int)((count * 1000) / elapsed));
}

#endif /* EXAMPLES_H_ */
