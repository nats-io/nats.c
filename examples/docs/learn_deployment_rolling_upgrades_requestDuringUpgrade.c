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

static void
onReconnected(natsConnection *nc, void *closure)
{
    printf("reconnected to another node, subscription restored\n");
}

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("warehouse received: %.*s\n",
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    natsMsg_Destroy(msg);
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Prove a client keeps working THROUGH a rolling upgrade. The
    // reconnect callback logs the moment the client's node enters
    // lame-duck mode and the connection moves to another node.
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, onReconnected, NULL);
    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // warehouse subscribes to new orders and stays connected.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.created",
                                     onMsg, NULL);

    // order-svc publishes one order. It is captured by ORDERS and
    // delivered to the warehouse subscriber even if a node is
    // mid-upgrade, because the R3 stream still has a quorum on the
    // two nodes that are up.
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");

    // Leave the subscriber running and roll a node: it logs a
    // reconnect and keeps printing messages. No order is lost.
    if (s == NATS_OK)
    {
        printf("subscribed to orders.created, roll a node while I run\n");
        while (natsConnection_Status(conn) != NATS_CONN_STATUS_CLOSED)
            nats_Sleep(100);
    }
    // NATS-DOC-END

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
