// Copyright 2015 Apcera Inc. All rights reserved.

#include "examples.h"

static const char *usage = "" \
"-sync          receive synchronously (default is asynchronous)\n" \
"-count         number of expected requests\n";

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s;

    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    if (start == 0)
        start = nats_Now();

    s = natsConnection_PublishString(nc, natsMsg_GetReply(msg),
                                     "here's some help");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
        elapsed = nats_Now() - start;

    if (s != NATS_OK)
        errors++;

    natsMsg_Destroy(msg);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    if (print)
        printf("Async error: %d - %s\n", err, natsStatus_GetText(err));
    errors++;
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatistics      *stats = NULL;
    natsMsg             *msg   = NULL;
    natsStatus          s;

    opts = parseArgs(argc, argv, usage);

    printf("Listening %ssynchronously for requests on '%s'\n",
           (async ? "a" : ""), subj);

    s = natsOptions_SetErrorHandler(opts, asyncCb, NULL);

    // This is setting the maximum number of pending messages allowed in
    // the library for each subscriber. For max performance, we set it
    // here to the expected total number of messages. You can remove, or
    // set it to a low number to see the effect of messages being dropped
    // by the client library.
    if (s == NATS_OK)
        s = natsOptions_SetMaxPendingMsgs(opts, (int) total);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        if (async)
            s = natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);
        else
            s = natsConnection_SubscribeSync(&sub, conn, subj);
    }
    if (s == NATS_OK)
        s = natsSubscription_NoDeliveryDelay(sub);
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, (int) total);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if ((s == NATS_OK) && async)
    {
        while (s == NATS_OK)
        {
            s = printStats(STATS_IN|STATS_COUNT|STATS_ERRORS,conn, sub, stats,
                           count, errors);

            if (count + errors == total)
                break;

            if (s == NATS_OK)
                nats_Sleep(1000);
        }
    }
    else if (s == NATS_OK)
    {
        int64_t last = 0;

        for (count = 0; (s == NATS_OK) && (count < total); count++)
        {
            s = natsSubscription_NextMsg(&msg, sub, 10000);
            if (s == NATS_OK)
                s = natsConnection_PublishString(conn,
                                                 natsMsg_GetReply(msg),
                                                 "here's some help");
            if (s == NATS_OK)
                s = natsConnection_Flush(conn);
            if (s == NATS_OK)
            {
                if (start == 0)
                    start = nats_Now();

                if (nats_Now() - last >= 1000)
                {
                    s = printStats(STATS_IN|STATS_COUNT|STATS_ERRORS,conn, sub, stats,
                                   count, errors);
                    last = nats_Now();
                }
            }

            natsMsg_Destroy(msg);
        }

        if (s == NATS_OK)
            s = natsConnection_FlushTimeout(conn, 1000);
    }

    if (s == NATS_OK)
    {
        printPerf("Received", count, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    natsStatistics_Destroy(stats);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
