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
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // Ask the inventory service whether an order's item is in stock. The
    // client creates a private inbox, sends the request, and waits up to
    // the timeout for one reply. A missing service surfaces immediately as
    // NATS_NO_RESPONDERS; a slow one as NATS_TIMEOUT.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    natsMsg    *reply = NULL;

    s = natsConnection_RequestString(&reply, conn, "orders.inventory.check",
                                     order, 2000);
    switch (s)
    {
        case NATS_NO_RESPONDERS:
            printf("no inventory service is running\n");
            break;
        case NATS_TIMEOUT:
            printf("inventory service did not answer in time\n");
            break;
        case NATS_OK:
            printf("inventory replied: %.*s\n",
                   natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
            natsMsg_Destroy(reply);
            break;
        default:
            printf("request failed: %s\n", natsStatus_GetText(s));
            break;
    }
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    return 0;
}
