// Copyright 2015-2018 The NATS Authors
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
"-queue         use a queue subscriber with this name\n" \
"-timeout <ms>  timeout in milliseconds (default is 10sec)\n" \
"-count         number of expected messages\n";

static volatile bool done = false;

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    // This callback will be invoked with a NULL message when the
    // subscription times out.
    if (print && (msg != NULL))
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if ((msg == NULL) || (++count == total))
    {
        printf("%s, destroying subscription\n",
               (msg == NULL ? "Subscription timed-out" : "All messages received"));

        natsSubscription_Destroy(sub);
        done = true;
    }

    // It is safe to call natsMsg_Destroy() with a NULL message.
    natsMsg_Destroy(msg);
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatus          s;

    opts = parseArgs(argc, argv, usage);

    printf("Listening asynchronously on '%s' with a timeout of %d ms.\n",
           subj, (int) timeout);

    s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
    {
        if (name != NULL)
            s = natsConnection_QueueSubscribeTimeout(&sub, conn, subj, name,
                                                     timeout, onMsg, NULL);
        else
            s = natsConnection_SubscribeTimeout(&sub, conn, subj,
                                                timeout, onMsg, NULL);
    }
    // Check every half a second for end of test.
    while ((s == NATS_OK) && !done)
    {
        nats_Sleep(500);
    }

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    // Do not destroy subscription since it is done in the callback
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
