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

int main(void)
{
    natsConnection      *conn  = NULL;
    natsSubscription    *sub   = NULL;
    natsInbox           *inbox = NULL;
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Gather more than one reply to a single request. A plain Request
    // returns only the first reply, so when several services may answer,
    // subscribe to your own inbox, publish the request with that inbox as
    // the reply subject, and collect replies until they stop arriving.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    int        replies = 0;

    if (s == NATS_OK)
        s = natsInbox_Create(&inbox);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, conn, (const char *) inbox);
    if (s == NATS_OK)
        s = natsConnection_PublishRequestString(conn, "orders.inventory.check",
                                                (const char *) inbox, order);

    while (s == NATS_OK)
    {
        natsMsg *reply = NULL;

        // Stop once no further reply arrives within the gap deadline.
        s = natsSubscription_NextMsg(&reply, sub, 300);
        if (s != NATS_OK)
            break;

        replies++;
        natsMsg_Destroy(reply);
    }
    printf("gathered %d replies\n", replies);
    // NATS-DOC-END

    // Running out of replies ends the gather; it is not an error.
    if ((s == NATS_TIMEOUT) || (s == NATS_NO_RESPONDERS))
        s = NATS_OK;

    natsSubscription_Destroy(sub);
    natsInbox_Destroy(inbox);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
