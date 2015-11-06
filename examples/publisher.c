// Copyright 2015 Apcera Inc. All rights reserved.

#include "examples.h"

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

    printf("Sending %" PRId64 " messages to subject '%s'\n", total, subj);

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
            s = printStats(STATS_OUT, conn, NULL, stats, 0, 0);
            last = nats_Now();
        }
    }

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    if (s == NATS_OK)
    {
        printPerf("Sent", total, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
    }

    // Destroy all our objects to avoid report of memory leak
    natsStatistics_Destroy(stats);
    natsConnection_Destroy(conn);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
