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
"-txt           text to send (default is 'hello')\n" \
"-count         number of messages to send\n";

typedef struct
{
    natsConnection  *conn;
    natsStatus      status;

} threadInfo;

static void
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

    // Since this is a user-thread, call this function to release
    // possible thread-local memory allocated by the library.
    nats_ReleaseThreadMemory();
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatus          s      = NATS_OK;
    uv_loop_t           *uvLoop= NULL;
    uv_thread_t         pub;
    threadInfo          info;

    opts = parseArgs(argc, argv, usage);

    printf("Sending %" PRId64 " messages to subject '%s'\n", total, subj);

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
        start = nats_Now();

    if (s == NATS_OK)
    {
        info.conn   = conn;
        info.status = NATS_OK;

        if (uv_thread_create(&pub, pubThread, (void*) &info) != 0)
            s = NATS_ERR;
    }

    if (s == NATS_OK)
    {
        uv_run(uvLoop, UV_RUN_DEFAULT);

        uv_thread_join(&pub);
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

    if (uvLoop != NULL)
        uv_loop_close(uvLoop);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
