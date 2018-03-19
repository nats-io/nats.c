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

#include "adapters/libuv.h"
#include "examples.h"

static const char *usage = ""\
"-gd            use global message delivery thread pool\n" \
"-count         number of expected messages\n";

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    natsMsg_Destroy(msg);

    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
    {
        elapsed = nats_Now() - start;

        natsConnection_Close(nc);
    }
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatus          s      = NATS_OK;
    uv_loop_t           *uvLoop= NULL;

    opts = parseArgs(argc, argv, usage);

    printf("Listening on '%s'.\n", subj);

    // One time initialization of things that we need.
    natsLibuv_Init();

    // Create a loop.
    uvLoop = uv_default_loop();
    if (uvLoop != NULL)
    {
        // Libuv is not thread-safe. Almost all calls to libuv need to
        // occur from the thread where the loop is running. NATS library
        // may have to call into the event loop from different threads.
        // This call allows natsLibuv APIs to know if they are executing
        // from the event loop thread or not.
        natsLibuv_SetThreadLocalLoop(uvLoop);
    }
    else
    {
        s = NATS_ERR;
    }

    // Indicate which loop and callbacks to use once connected.
    if (s == NATS_OK)
        s = natsOptions_SetEventLoop(opts, (void*) uvLoop,
                                     natsLibuv_Attach,
                                     natsLibuv_Read,
                                     natsLibuv_Write,
                                     natsLibuv_Detach);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);

    // For maximum performance, set no limit on the number of pending messages.
    if (s == NATS_OK)
        s = natsSubscription_SetPendingLimits(sub, -1, -1);

    // Run the event loop.
    // This call will return when the connection is closed (either after
    // receiving all messages, or disconnected and unable to reconnect).
    if (s == NATS_OK)
    {
        uv_run(uvLoop, UV_RUN_DEFAULT);
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
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (uvLoop != NULL)
        uv_loop_close(uvLoop);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
