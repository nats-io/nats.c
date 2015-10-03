// Copyright 2015 Apcera Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nats.h>

static natsStatus
printStats(natsConnection *conn, natsStatistics *stats)
{
    natsStatus  s = NATS_OK;
    uint64_t    outMsgs, outBytes, reconnected;

    s = natsConnection_GetStats(conn, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, NULL, NULL, &outMsgs, &outBytes,
                                     &reconnected);
    if (s == NATS_OK)
    {
        printf("Out Msgs: %12llu - Out Bytes: %12llu - Reconnected: %3llu\n",
                outMsgs, outBytes, reconnected);
    }

    return s;
}

int main(int argc, char **argv)
{
    natsConnection  *conn  = NULL;
    natsStatistics  *stats = NULL;
    const char      *subj  = NULL;
    const char      *txt   = NULL;
    int64_t         total  = 0;
    int64_t         count  = 0;
    int64_t         start  = 0;
    int64_t         last   = 0;
    int64_t         elapsed= 0;
    natsStatus      s;

    if (argc != 4)
    {
        printf("Usage: %s <subject> <msg content> <count>\n", argv[0]);
        exit(1);
    }

    subj  = argv[1];
    txt   = argv[2];
    total = atol(argv[3]);

    printf("Sending %lld messages to subject '%s'\n", total, subj);

    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if (s == NATS_OK)
        start = nats_Now();

    for (count = 0; (s == NATS_OK) && (count < total); count++)
    {
        s = natsConnection_PublishString(conn, subj, txt);

        if (nats_Now() - last >= 1000)
        {
            s = printStats(conn, stats);
            last = nats_Now();
        }
    }

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    if (s == NATS_OK)
    {
        elapsed = nats_Now() - start;

        printf("\nSent %lld messages in %lld milliseconds (%d msgs/sec)\n",
               total, elapsed, (int)((total*1000)/elapsed));
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
    }

    natsStatistics_Destroy(stats);
    natsConnection_Destroy(conn);
}
