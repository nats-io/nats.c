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
    natsConnection  *conn = NULL;
    natsStatus      s;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // Ask the OrderInventory service whether the order's item is in stock.
    // Underneath it is plain request-reply: a private reply subject, the
    // request out on the check endpoint's subject, and one answer back.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    natsMsg    *reply = NULL;

    s = natsConnection_RequestString(&reply, conn, "orders.inventory.check",
                                     order, 2000);
    if (s == NATS_OK)
    {
        printf("OrderInventory replied: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
    }
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
