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
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Max-deliveries advisories are plain core NATS messages, so
        // subscribe with the core client instead of a JetStream consumer.
        // The server publishes one each time a message on the "shipping"
        // consumer hits its delivery limit.
        const char *subject = "$JS.EVENT.ADVISORY.CONSUMER.MAX_DELIVERIES.ORDERS.shipping";

        s = natsConnection_SubscribeSync(&sub, conn, subject);

        // Print each advisory as it arrives.
        while (s == NATS_OK)
        {
            natsMsg *msg = NULL;

            s = natsSubscription_NextMsg(&msg, sub, 60000);
            if (s == NATS_OK)
            {
                printf("Max-deliveries advisory: %.*s\n",
                       natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
                natsMsg_Destroy(msg);
            }
        }
        // NATS-DOC-END
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
