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

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Subscribe to orders.created and stop automatically after three
    // messages. AutoUnsubscribe tells the client to remove the
    // subscription once that many have been delivered, giving a
    // take-exactly-N read instead of an open-ended subscription.
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, conn, "orders.created");
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 3);

    // Read the three orders, waiting up to 10 seconds for each.
    if (s == NATS_OK)
    {
        int i;

        for (i = 0; (s == NATS_OK) && (i < 3); i++)
        {
            natsMsg *msg = NULL;

            s = natsSubscription_NextMsg(&msg, sub, 10000);
            if (s == NATS_OK)
            {
                printf("warehouse received: %.*s\n",
                       natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
                natsMsg_Destroy(msg);
            }
        }
    }
    // NATS-DOC-END

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
