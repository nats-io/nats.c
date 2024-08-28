// Copyright 2021 The NATS Authors
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

#include "examples.h"

static const char *usage = ""\
"-gd            use global message delivery thread pool\n" \
"-sync          receive synchronously (default is asynchronous)\n" \
"-pull          use pull subscription\n" \
"-pull-async    use an async pull subscription\n" \
"-fc            enable flow control\n" \
"-count         number of expected messages\n";

static bool fetchCompleteCalled = false;
static bool subCompleteCalled = false;

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
    {
        printf("Received msg: %s - '%.*s'\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));
    }
    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
        elapsed = nats_Now() - start;

    // Since this is auto-ack callback, we don't need to ack here.
    natsMsg_Destroy(msg);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    printf("Async error: %u - %s\n", err, natsStatus_GetText(err));

    natsSubscription_GetDropped(sub, (int64_t*) &dropped);
}

static void
_completeFetchCb(natsConnection *nc, natsSubscription *sub, natsStatus s, void *closure)
{
    fetchCompleteCalled = true;

    if (print)
        printf("Fetch completed with status: %u - %s\n", s, natsStatus_GetText(s));
}

static void
_completeSubCb(void *closure)
{
    subCompleteCalled = true;
    if (print)
        printf("Subscription completed\n");
}

static bool
nextFetchCb(jsFetchRequest *req, natsSubscription *sub, void *closure)
{
    if (print)
        printf("NextFetch: always ask for 1 message, 0 MaxBytes\n");

    req->Batch = 1;
    req->MaxBytes = 0;
    return true;
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsStatistics      *stats = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsMsg             *msg   = NULL;
    jsCtx               *js    = NULL;
    jsErrCode           jerr   = 0;
    jsOptions           jsOpts;
    jsSubOptions        so;
    natsStatus          s;
    bool                delStream = false;

    opts = parseArgs(argc, argv, usage);

    printf("Creating %s%s subscription on '%s'\n",
            async ? "an asynchronous" : "a synchronous",
            pull ? " pull" : "",
            subj);

    s = natsOptions_SetErrorHandler(opts, asyncCb, NULL);

    // Uncomment to use the global thread pool for message delivery.
    // if (s == NATS_OK)
    //     s = natsOptions_UseGlobalMessageDelivery(opts, true);
    // if (s == NATS_OK)
    //     s = nats_SetMessageDeliveryPoolSize(1); // 1 thread for all subscriptions.

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = jsOptions_Init(&jsOpts);

    if (s == NATS_OK)
        s = jsSubOptions_Init(&so);
    if (s == NATS_OK)
    {
        so.Stream = stream;
        so.Consumer = durable;
        if (flowctrl)
        {
            so.Config.FlowControl = true;
            so.Config.Heartbeat = (int64_t)1E9;
        }
    }

    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, &jsOpts);

    if (s == NATS_OK)
    {
        jsStreamInfo    *si = NULL;

        // First check if the stream already exists.
        s = js_GetStreamInfo(&si, js, stream, NULL, &jerr);
        if (s == NATS_NOT_FOUND)
        {
            jsStreamConfig  cfg;

            // Since we are the one creating this stream, we can delete at the end.
            delStream = true;

            // Initialize the configuration structure.
            jsStreamConfig_Init(&cfg);
            cfg.Name = stream;
            // Set the subject
            cfg.Subjects = (const char*[1]){subj};
            // Set the subject count
            cfg.SubjectsLen = 1;
            // Make it a memory stream.
            cfg.Storage = js_MemoryStorage;
            // Add the stream,
            s = js_AddStream(&si, js, &cfg, NULL, &jerr);
        }
        if (s == NATS_OK)
        {
            printf("Stream %s has %" PRIu64 " messages (%" PRIu64 " bytes)\n",
                si->Config->Name, si->State.Msgs, si->State.Bytes);

            // Need to destroy the returned stream object.
            jsStreamInfo_Destroy(si);
        }
    }

    if (s == NATS_OK)
    {
        if (pull && async)
        {
            jsOpts.PullSubscribeAsync.MaxMessages = (int) total;

            // Defalut values, change as needed.
            jsOpts.PullSubscribeAsync.FetchSize = 128;  // ask for 128 messages at a time
            jsOpts.PullSubscribeAsync.NoWait = false;
            jsOpts.PullSubscribeAsync.Timeout = 0;      // for the entire subscription, in milliseconds
            jsOpts.PullSubscribeAsync.KeepAhead = 0;
            jsOpts.PullSubscribeAsync.Heartbeat = 0;    // in milliseconds

            jsOpts.PullSubscribeAsync.CompleteHandler = _completeFetchCb;
            jsOpts.PullSubscribeAsync.CompleteHandlerClosure = NULL;

            // Uncomment to provide custom control over next fetch size.
            // jsOpts.PullSubscribeAsync.NextHandler = nextFetchCb;

            // Uncomment to turn off AutoACK on delivered messages.            
            // so.ManualAck = true;

            s = js_PullSubscribeAsync(&sub, js, subj, durable, onMsg, NULL, &jsOpts, &so, &jerr);
        }
        else if (pull)
            s = js_PullSubscribe(&sub, js, subj, durable, &jsOpts, &so, &jerr);
        else if (async)
            s = js_Subscribe(&sub, js, subj, onMsg, NULL, &jsOpts, &so, &jerr);
        else
            s = js_SubscribeSync(&sub, js, subj, &jsOpts, &so, &jerr);
    }

    if ((s == NATS_OK) && async)
        s = natsSubscription_SetOnCompleteCB(sub, _completeSubCb, NULL);
    if ((s == NATS_OK) && async)
        s = natsSubscription_AutoUnsubscribe(sub, (int) total); // to get the sub closed callback
    if (s == NATS_OK)
        s = natsSubscription_SetPendingLimits(sub, -1, -1);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if ((s == NATS_OK) && pull && !async)
    {
        // Pull mode, simple "Fetch" loop
        natsMsgList list;
        int         i;

        for (count = 0; (s == NATS_OK) && (count < total); )
        {
            s = natsSubscription_Fetch(&list, sub, 1024, 5000, &jerr);
            if (s != NATS_OK)
                break;

            if (start == 0)
                start = nats_Now();

            count += (int64_t) list.Count;
            for (i=0; (s == NATS_OK) && (i<list.Count); i++)
                s = natsMsg_Ack(list.Msgs[i], &jsOpts);

            natsMsgList_Destroy(&list);
        }
    }
    else if ((s == NATS_OK) && async)
    {
        // All async modes (push and pull)
        while (s == NATS_OK)
        {
            bool end = (count + dropped >= total);

            if (end && subCompleteCalled)
            {
                if (!pull)
                    break;
                else if (fetchCompleteCalled)
                    break;
            }

            nats_Sleep(500);
        }
    }
    else if (s == NATS_OK)
    {
        // Sync mode
        for (count = 0; (s == NATS_OK) && (count < total); count++)
        {
            s = natsSubscription_NextMsg(&msg, sub, 5000);
            if (s != NATS_OK)
                break;

            if (start == 0)
                start = nats_Now();

            s = natsMsg_Ack(msg, &jsOpts);
            natsMsg_Destroy(msg);
        }
    }

    if (s == NATS_OK)
    {
        printStats(STATS_IN|STATS_COUNT, conn, sub, stats);
        printPerf("Received");
    }
    if (s == NATS_OK)
    {
        jsStreamInfo *si = NULL;

        // Let's report some stats after the run
        s = js_GetStreamInfo(&si, js, stream, NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("\nStream %s has %" PRIu64 " messages (%" PRIu64 " bytes)\n",
                si->Config->Name, si->State.Msgs, si->State.Bytes);

            jsStreamInfo_Destroy(si);
        }
        if (delStream)
        {
            printf("\nDeleting stream %s: ", stream);
            s = js_DeleteStream(js, stream, NULL, &jerr);
            if (s == NATS_OK)
                printf("OK!");
            printf("\n");
        }
    }
    else
    {
        printf("Error: %u - %s - jerr=%u\n", s, natsStatus_GetText(s), jerr);
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    jsCtx_Destroy(js);
    natsStatistics_Destroy(stats);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
