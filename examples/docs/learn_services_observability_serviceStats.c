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
    // Send a few real requests so the counters have something to show.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    natsMsg    *reply = NULL;
    int        i;

    for (i = 0; (s == NATS_OK) && (i < 3); i++)
    {
        s = natsConnection_RequestString(&reply, conn,
                                         "orders.inventory.check", order, 2000);
        if (s == NATS_OK)
        {
            natsMsg_Destroy(reply);
            reply = NULL;
        }
    }

    // Read the accumulated stats: the STATS discovery verb on the $SRV
    // prefix, scoped to OrderInventory. Each instance answers with one
    // stats_response listing every endpoint and its counters:
    // num_requests, num_errors, last_error, processing_time,
    // average_processing_time.
    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn,
                                         "$SRV.STATS.OrderInventory", "", 2000);
    if (s == NATS_OK)
    {
        printf("%.*s\n", natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
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
