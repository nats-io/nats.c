// Copyright 2015 Apcera Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nats.h>

static volatile int64_t count   = 0;
static volatile int64_t errors  = 0;
static int64_t          total   = 0;
static int64_t          start   = 0;
static volatile int64_t elapsed = 0;
static bool             print   = false;

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
    if (count + 1 == total)
    {
        if (s == NATS_OK)
            s = natsConnection_FlushTimeout(nc, 1000);

        elapsed = nats_Now() - start;
    }

    if (s != NATS_OK)
        errors++;

    count++;

    natsMsg_Destroy(msg);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    if (print)
        printf("Async error: %d - %s\n", err, natsStatus_GetText(err));
    errors++;
}

static natsStatus
printStats(natsConnection *conn, natsSubscription *sub, natsStatistics *stats)
{
    natsStatus  s = NATS_OK;
    uint64_t    inMsgs, inBytes, queued, reconnected;

    s = natsConnection_GetStats(conn, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, &inMsgs, &inBytes, NULL, NULL,
                                     &reconnected);
    if (s == NATS_OK)
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
        printf("In Msgs: %9" NATS_PRINTF_U64 " - "\
               "In Bytes: %9" NATS_PRINTF_U64 " - "\
               "Delivered: %9" NATS_PRINTF_U64 " - "\
               "Queued: %5" NATS_PRINTF_U64 " - "\
               "Errors: %9" NATS_PRINTF_U64 " - "\
               "Reconnected: %3" NATS_PRINTF_U64 "\n",
                inMsgs, inBytes, count, queued, errors, reconnected);
    }

    return s;
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatistics      *stats = NULL;
    natsMsg             *msg   = NULL;
    bool                async  = true;
    const char          *subj  = NULL;
    natsStatus          s;

    if (argc != 4)
    {
        printf("Usage: %s <mode:async|sync> <subject> <count>\n", argv[0]);
        exit(1);
    }

    async = (strcasecmp(argv[1], "async") == 0);
    subj  = argv[2];
    total = atol(argv[3]);
    printf("Listening %ssynchronously for requests on '%s'\n",
           (async ? "a" : ""), subj);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, NATS_DEFAULT_URL);
    if ((s == NATS_OK) && async)
        s = natsOptions_SetErrorHandler(opts, asyncCb, NULL);

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
        s = natsSubscription_AutoUnsubscribe(sub, total);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if ((s == NATS_OK) && async)
    {
        while (s == NATS_OK)
        {
            s = printStats(conn, sub, stats);

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
                    s = printStats(conn, sub, stats);
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
        if (elapsed == 0)
            elapsed = nats_Now() - start;

        if (elapsed <= 0)
            printf("\nNot enough messages or too fast to report performance!\n");
        else
            printf("\nReceived %" NATS_PRINTF_D64 " requests in "\
                   "%" NATS_PRINTF_D64 " milliseconds (%d msgs/sec)\n",
                   count, elapsed, (int)((count * 1000) / elapsed));
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
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
