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
// Print which subscription pattern (passed as the closure) got the message
static void
onOrderMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("%s%.*s  (%s)\n",
           (const char*) closure,
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg),
           natsMsg_GetSubject(msg));
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn    = NULL;
    natsSubscription    *shipped = NULL;
    natsSubscription    *placed  = NULL;
    natsSubscription    *retail  = NULL;
    natsStatus          s;
    const char          *url     = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Subscribe with single token wildcards
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&shipped, conn, "orders.*.shipped",
                                     onOrderMsg, (void*) "[orders.*.shipped]  ");
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&placed, conn, "orders.*.placed",
                                     onOrderMsg, (void*) "[orders.*.placed]   ");
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&retail, conn, "orders.retail.*",
                                     onOrderMsg, (void*) "[orders.retail.*]   ");

    // Publish to specific subjects
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.wholesale.placed", "Order W73737");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.retail.placed", "Order R65432");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.wholesale.shipped", "Order W73001");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.retail.shipped", "Order R65321");
    // NATS-DOC-END

    // Give the callbacks a moment to print the messages
    if (s == NATS_OK)
        nats_Sleep(500);

    // Anything that is created need to be destroyed
    natsSubscription_Destroy(shipped);
    natsSubscription_Destroy(placed);
    natsSubscription_Destroy(retail);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
