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
// Audit service: catch every order message at any depth. The multi-token
// wildcard > matches one or more tokens and must be the last token, so
// orders.> matches orders.created, orders.us.created, and
// orders.us.west.created alike.
static void
onOrder(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("audit: %s\n", natsMsg_GetSubject(msg));

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onOrder, NULL);
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        // Keep the audit service running; stop with Ctrl+C.
        for (;;)
            nats_Sleep(100);
    }

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
