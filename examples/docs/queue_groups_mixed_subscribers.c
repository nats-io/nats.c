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
#include <stdio.h>

// NATS-DOC-START
// Regular subscribers receive every message; the closure names them.
static void
onAll(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("[%s] %s: %.*s\n", (const char *) closure,
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));

    natsMsg_Destroy(msg);
}

// Queue group members share the load: one worker per message.
static void
onWork(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("[WORKER %s] Processing: %.*s\n", (const char *) closure,
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn    = NULL;
    natsSubscription    *audit   = NULL;
    natsSubscription    *metrics = NULL;
    natsSubscription    *workA   = NULL;
    natsSubscription    *workB   = NULL;
    natsStatus          s;
    const char          *url     = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Audit logger - receives all messages
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&audit, conn, "orders.>",
                                     onAll, (void *) "AUDIT");

    // Metrics collector - receives all messages
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&metrics, conn, "orders.>",
                                     onAll, (void *) "METRICS");

    // Workers in queue group - load balanced
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&workA, conn, "orders.new",
                                          "workers", onWork, (void *) "A");
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&workB, conn, "orders.new",
                                          "workers", onWork, (void *) "B");

    // Publish orders
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.new", "Order 123");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.new", "Order 124");
    // Audit and metrics see them, one worker processes each
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        // Give the subscribers a moment to receive and print the orders.
        natsConnection_Flush(conn);
        nats_Sleep(500);
    }

    natsSubscription_Destroy(audit);
    natsSubscription_Destroy(metrics);
    natsSubscription_Destroy(workA);
    natsSubscription_Destroy(workB);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
