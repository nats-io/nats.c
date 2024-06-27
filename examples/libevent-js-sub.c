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

#include "adapters/libevent.h"
#include "examples.h"

#ifndef WIN32
#include <pthread.h>
#define THREAD_T pthread_t
#define THREAD_FN void *
#define THREAD_RETURN() return (NULL)
#define THREAD_START(threadvar, fn, arg) \
    pthread_create(&(threadvar), NULL, fn, arg)
#define THREAD_JOIN(th) pthread_join(th, NULL)
#else
#include <process.h>
#define THREAD_T HANDLE
#define THREAD_FN unsigned __stdcall
#define THREAD_RETURN() return (0)
#define THREAD_START(threadvar, fn, arg)                                      \
    do                                                                        \
    {                                                                         \
        uintptr_t threadhandle = _beginthreadex(NULL, 0, fn, (arg), 0, NULL); \
        (threadvar) = (HANDLE)threadhandle;                                   \
    } while (0)
#define THREAD_JOIN(th) WaitForSingleObject(th, INFINITE)
#endif

static const char *usage = ""
                           "-gd            use global message delivery thread pool\n"
                           "-sync          receive synchronously (default is asynchronous)\n"
                           "-pull          use pull subscription\n"
                           "-fc            enable flow control\n"
                           "-count         number of expected messages\n";

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
        elapsed = nats_Now() - start;

    if (count % 1000 == 0)
        printf("Count = %llu\n", count);

    // Since this is auto-ack callback, we don't need to ack here.
    natsMsg_Destroy(msg);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    printf("Async error: %u - %s\n", err, natsStatus_GetText(err));

    natsSubscription_GetDropped(sub, (int64_t *)&dropped);
}

typedef struct
{
    natsConnection *conn;
    natsStatus status;

} threadInfo;

static void *workThread(void *arg)
{
    threadInfo *info = (threadInfo *)arg;
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    natsMsg *msg = NULL;
    jsCtx *js = NULL;
    jsErrCode jerr = 0;
    jsOptions jsOpts;
    jsSubOptions so;
    bool delStream = false;
    natsStatistics *stats = NULL;

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
        s = natsConnection_JetStream(&js, info->conn, &jsOpts);

    if (s == NATS_OK)
    {
        jsStreamInfo *si = NULL;

        // First check if the stream already exists.
        s = js_GetStreamInfo(&si, js, stream, NULL, &jerr);
        if (s == NATS_NOT_FOUND)
        {
            jsStreamConfig cfg;

            // Since we are the one creating this stream, we can delete at the end.
            delStream = true;

            // Initialize the configuration structure.
            jsStreamConfig_Init(&cfg);
            cfg.Name = stream;
            // Set the subject
            cfg.Subjects = (const char *[1]){subj};
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
        if (pull)
            s = js_PullSubscribe(&sub, js, subj, durable, &jsOpts, &so, &jerr);
        else if (async)
            s = js_Subscribe(&sub, js, subj, onMsg, NULL, &jsOpts, &so, &jerr);
        else
            s = js_SubscribeSync(&sub, js, subj, &jsOpts, &so, &jerr);
    }
    if (s == NATS_OK)
        s = natsSubscription_SetPendingLimits(sub, -1, -1);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if ((s == NATS_OK) && pull)
    {
        natsMsgList list;
        int i;

        for (count = 0; (s == NATS_OK) && (count < total);)
        {
            int64_t start = nats_Now();
            s = natsSubscription_Fetch(&list, sub, 100, 60*1000 /* 1 minute */, &jerr);
            if (s == NATS_OK)
                printf("Received %d messages\n", list.Count);
            else
                printf("Fetch error: %u - %s - jerr=%u, after %lld\n", s, natsStatus_GetText(s), jerr, nats_Now() - start);

            if (s != NATS_OK)
                break;

            if (start == 0)
                start = nats_Now();

            count += (int64_t)list.Count;
            for (i = 0; (s == NATS_OK) && (i < list.Count); i++)
                s = natsMsg_Ack(list.Msgs[i], &jsOpts);

            printf("Count = %llu\n", count);

            natsMsgList_Destroy(&list);
        }
    }
    else if ((s == NATS_OK) && async)
    {
        while (s == NATS_OK)
        {
            if (count + dropped > total)
                break;

            nats_Sleep(1000);
        }
    }
    else if (s == NATS_OK)
    {
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
        printStats(STATS_IN | STATS_COUNT, info->conn, sub, stats);
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

    // Since this is a user-thread, call this function to release
    // possible thread-local memory allocated by the library.
    nats_ReleaseThreadMemory();
    return NULL;
}

int main(int argc, char **argv)
{
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    natsStatus s;
    struct event_base *evLoop = NULL;
    THREAD_T pub;
    threadInfo info;

    opts = parseArgs(argc, argv, usage);

    printf("Creating %s subscription on '%s'.\n",
           (pull ? "pull" : (async ? "asynchronous" : "synchronous")), subj);

    s = natsOptions_SetErrorHandler(opts, asyncCb, NULL);

    // One time initialization of things that we need.
    natsLibevent_Init();

    // Create a loop.
    evLoop = event_base_new();
    if (evLoop == NULL)
        s = NATS_ERR;

    // Indicate which loop and callbacks to use once connected.
    if (s == NATS_OK)
        s = natsOptions_SetEventLoop(opts, (void *)evLoop,
                                     natsLibevent_Attach,
                                     natsLibevent_Read,
                                     natsLibevent_Write,
                                     natsLibevent_Detach);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        info.conn = conn;
        info.status = NATS_OK;
        THREAD_START(pub, workThread, (void *)&info);
    }

    if (s == NATS_OK)
    {
        event_base_dispatch(evLoop);

        THREAD_JOIN(pub);
        s = info.status;
    }

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}