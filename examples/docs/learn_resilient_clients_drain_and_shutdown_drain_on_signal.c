// Copyright 2026 The NATS Authors
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

#include <nats.h>
#include <signal.h>

static volatile sig_atomic_t shuttingDown = 0;

static void
onSignal(int sig)
{
    shuttingDown = 1;
}

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("warehouse handled: %.*s\n",
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    natsMsg_Destroy(msg);
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "warehouse");

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onMsg, NULL);

    // The shutdown handler only sets a flag; the drain runs in main.
    signal(SIGTERM, onSignal);
    signal(SIGINT, onSignal);

    if (s == NATS_OK)
    {
        printf("subscribed to orders.>, send SIGTERM to drain\n");
        while (!shuttingDown && !natsConnection_IsClosed(conn))
            nats_Sleep(100);

        // NATS-DOC-START
        // SIGTERM arrived: drain instead of close. The connection stops
        // new deliveries, lets in-flight handlers finish, flushes
        // pending publishes, then closes.
        s = natsConnection_Drain(conn);

        // Drain returns immediately and runs in the background, so wait
        // for CLOSED before the process exits -- exiting on Drain()'s
        // return abandons the work drain was meant to save.
        while ((s == NATS_OK) && !natsConnection_IsClosed(conn))
            nats_Sleep(100);

        printf("drain complete, exiting\n");
        // NATS-DOC-END
    }

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
