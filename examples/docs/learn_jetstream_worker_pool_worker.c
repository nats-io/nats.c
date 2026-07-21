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

static volatile sig_atomic_t done = 0;

static void
onInterrupt(int sig)
{
    (void) sig;
    done = 1;
}

// NATS-DOC-START
// The handler runs for every order the server hands this worker.
static void
onOrder(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("shipping %.*s\n", natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    natsMsg_Ack(msg, NULL);
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsSubscription    *sub  = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    signal(SIGINT, onInterrupt);

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        natsConnection_Destroy(conn);
        exit(2);
    }

    // NATS-DOC-START
    // Bind to the durable "shipping" consumer created earlier and deliver
    // messages to the handler. Start this same program in several processes:
    // they all share the one "shipping" consumer, and the server splits the
    // stored orders across them, one order to one worker.
    jsSubOptions so;

    jsSubOptions_Init(&so);
    so.Stream    = "ORDERS";
    so.Consumer  = "shipping";
    so.ManualAck = true;

    s = js_PullSubscribeAsync(&sub, js, NULL, NULL, onOrder, NULL, NULL, &so, &jerr);

    // Keep this worker running until interrupted (Ctrl-C).
    while ((s == NATS_OK) && !done)
        nats_Sleep(100);
    // NATS-DOC-END

    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
