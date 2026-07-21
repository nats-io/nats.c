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

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Subscribe to every JetStream advisory the cluster publishes.
        // Advisories are transient: you only see events that fire while
        // you are attached, so subscribe before the shipping consumer
        // goes wrong.
        s = natsConnection_SubscribeSync(&sub, conn, "$JS.EVENT.ADVISORY.>");

        // When a poison order exhausts its deliveries on the shipping
        // consumer, one max_deliver advisory lands on:
        //   $JS.EVENT.ADVISORY.CONSUMER.MAX_DELIVERIES.ORDERS.shipping
        // Its JSON body names the stream, the consumer, the failed
        // sequence, and how many times delivery was attempted.
        while (s == NATS_OK)
        {
            natsMsg *msg = NULL;

            s = natsSubscription_NextMsg(&msg, sub, 30000);
            if (s == NATS_OK)
            {
                printf("[%s] %s\n", natsMsg_GetSubject(msg), natsMsg_GetData(msg));
                natsMsg_Destroy(msg);
            }
        }
        // NATS-DOC-END

        // Stopping after a quiet 30 seconds is fine for the example.
        if (s == NATS_TIMEOUT)
            s = NATS_OK;
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
