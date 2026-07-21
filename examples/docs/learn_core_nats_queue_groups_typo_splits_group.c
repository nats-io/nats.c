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
// A typo in the queue group name silently creates a SECOND group. The
// server matches members by the exact name string, so "packers" and
// "packer" are two different groups on the same subject. Both
// subscriptions succeed with no warning.
static void
onOrder(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("group %s handling: %.*s\n", (const char *) closure,
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub1 = NULL;
    natsSubscription    *sub2 = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&sub1, conn, "orders.created",
                                          "packers", onOrder, (void *) "packers");
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&sub2, conn, "orders.created",
                                          "packer", onOrder, (void *) "packer"); // typo!

    // Publish an order and BOTH callbacks print it: one member of EACH
    // group receives it, so the work is double-handled instead of
    // load-balanced. Fix: give every member the byte-for-byte identical
    // group name.
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        // Keep the subscribers running; stop with Ctrl+C.
        for (;;)
            nats_Sleep(100);
    }

    natsSubscription_Destroy(sub1);
    natsSubscription_Destroy(sub2);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
