// Copyright 2015 Apcera Inc. All rights reserved.

#include "examples.h"

static const char *usage = "" \
"-txt           text to send (default is 'hello')\n" \
"-count         number of requests to send\n";

int main(int argc, char **argv)
{
    natsConnection  *conn  = NULL;
    natsStatistics  *stats = NULL;
    natsOptions     *opts  = NULL;
    natsMsg         *reply = NULL;
    int64_t         last   = 0;
    natsStatus      s;

    opts = parseArgs(argc, argv, usage);

    printf("Sending %" PRId64 " requests to subject '%s'\n", total, subj);

    s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if (s == NATS_OK)
        start = nats_Now();

    for (count = 0; (s == NATS_OK) && (count < total); count++)
    {
        s = natsConnection_RequestString(&reply, conn, subj, txt, 1000);
        if (s != NATS_OK)
            break;

        if (print)
        {
            printf("Received reply: %s - %.*s\n",
                   natsMsg_GetSubject(reply),
                   natsMsg_GetDataLength(reply),
                   natsMsg_GetData(reply));
        }

        if (nats_Now() - last >= 1000)
        {
            s = printStats(STATS_OUT,conn, NULL, stats);
            last = nats_Now();
        }

        natsMsg_Destroy(reply);
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
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    natsStatistics_Destroy(stats);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
