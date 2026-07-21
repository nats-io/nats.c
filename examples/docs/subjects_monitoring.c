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
// Print every message that flows through the server
static void
onMonitorMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("[MONITOR] %s: %.*s\n",
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg));
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Create a wire tap for monitoring
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, ">", onMonitorMsg, NULL);
    // NATS-DOC-END

    // Publish some sample traffic so the monitor has something to show
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.retail.placed", "Order R12345");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "inventory.check", "SKU-42");

    // Give the callback a moment to print the messages
    if (s == NATS_OK)
        nats_Sleep(500);

    // Anything that is created need to be destroyed
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
