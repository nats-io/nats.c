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
// Each worker prints its own name; the closure carries it.
static void
onOrder(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("Worker %s processed: %.*s\n", (const char *) closure,
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    natsSubscription    *subA = NULL;
    natsSubscription    *subB = NULL;
    natsSubscription    *subC = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Create three workers in the same queue group
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&subA, conn, "orders.new",
                                          "workers", onOrder, (void *) "A");
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&subB, conn, "orders.new",
                                          "workers", onOrder, (void *) "B");
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&subC, conn, "orders.new",
                                          "workers", onOrder, (void *) "C");

    // Publish messages - automatically load balanced
    for (int i = 1; (s == NATS_OK) && (i <= 10); i++)
    {
        char order[32];

        snprintf(order, sizeof(order), "Order %d", i);
        s = natsConnection_PublishString(conn, "orders.new", order);
    }
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        // Give the workers a moment to receive and print the orders.
        natsConnection_Flush(conn);
        nats_Sleep(500);
    }

    natsSubscription_Destroy(subA);
    natsSubscription_Destroy(subB);
    natsSubscription_Destroy(subC);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
