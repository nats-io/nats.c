// Copyright 2016-2018 The NATS Authors
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
#define THREAD_START(threadvar, fn, arg) do {       \
    uintptr_t threadhandle = _beginthreadex(NULL,0,fn,(arg),0,NULL); \
    (threadvar) = (HANDLE) threadhandle; \
    } while (0)
#define THREAD_JOIN(th) WaitForSingleObject(th, INFINITE)
#endif

static const char *usage = ""\
"-txt           text to send (default is 'hello')\n" \
"-count         number of messages to send\n";

typedef struct
{
    natsConnection  *conn;
    natsStatus      status;

} threadInfo;

static THREAD_FN
pubThread(void *arg)
{
    threadInfo  *info = (threadInfo*) arg;
    natsStatus  s     = NATS_OK;

    for (count = 0; (s == NATS_OK) && (count < total); count++)
        s = natsConnection_PublishString(info->conn, subj, txt);

    if (s == NATS_OK)
        s = natsConnection_Flush(info->conn);

    natsConnection_Close(info->conn);

    info->status = s;

    if (s != NATS_OK)
        nats_PrintLastErrorStack(stderr);

    THREAD_RETURN();
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatus          s      = NATS_OK;
    struct event_base   *evLoop= NULL;
    THREAD_T            pub;
    threadInfo          info;

    nats_Open(-1);

    opts = parseArgs(argc, argv, usage);

    printf("Sending %" PRId64 " messages to subject '%s'\n", total, subj);

    // One time initialization of things that we need.
    natsLibevent_Init();

    // Create a loop.
    evLoop = event_base_new();
    if (evLoop == NULL)
        s = NATS_ERR;

    // Indicate which loop and callbacks to use once connected.
    if (s == NATS_OK)
        s = natsOptions_SetEventLoop(opts, (void*) evLoop,
                                     natsLibevent_Attach,
                                     natsLibevent_Read,
                                     natsLibevent_Write,
                                     natsLibevent_Detach);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        start = nats_Now();

    if (s == NATS_OK)
    {
        info.conn   = conn;
        info.status = NATS_OK;

        THREAD_START(pub, pubThread, (void*) &info);
    }

    if (s == NATS_OK)
    {
        event_base_dispatch(evLoop);

        THREAD_JOIN(pub);
        s = info.status;
    }

    if (s == NATS_OK)
    {
        printPerf("Sent", count, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (evLoop != NULL)
        event_base_free(evLoop);

    // To silence reports of memory still in used with valgrind
    nats_Close();
    libevent_global_shutdown();

    return 0;
}
