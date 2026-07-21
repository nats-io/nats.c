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

// NATS-DOC-START
// The async error callback is where the client reports a slow consumer.
// Without one, the overflow is dropped silently and you never learn the
// application lost data. GetDropped gives the running count of messages
// already lost on this subscription.
static void
onAsyncError(natsConnection *nc, natsSubscription *sub, natsStatus err,
             void *closure)
{
    if (err == NATS_SLOW_CONSUMER)
    {
        int64_t dropped = 0;

        natsSubscription_GetDropped(sub, &dropped);
        fprintf(stderr, "slow consumer on warehouse: %lld messages "
                "dropped so far\n", (long long) dropped);
    }
    else
        fprintf(stderr, "async error: %s\n", natsStatus_GetText(err));
}
// NATS-DOC-END

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    // A deliberately slow handler: flood orders.> faster than this
    // drains and the pending buffer overflows, firing the callback.
    nats_Sleep(500);
    natsMsg_Destroy(msg);
}

int main(void)
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

    // NATS-DOC-START
    // Wire the callback on the connection, then bound the buffer: limits
    // and the callback together make the overflow visible instead of a
    // silent drop.
    if (s == NATS_OK)
        s = natsOptions_SetErrorHandler(opts, onAsyncError, NULL);
    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onMsg, NULL);
    if (s == NATS_OK)
        s = natsSubscription_SetPendingLimits(sub, 10000, 8 * 1024 * 1024);
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        printf("subscribed to orders.>, flood the subject to see the signal\n");
        nats_Sleep(10000);
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
