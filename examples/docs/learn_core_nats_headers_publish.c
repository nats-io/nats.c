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
#include <string.h>

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsMsg             *msg  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Build an orders.created message with two headers attached. The
    // headers travel alongside the JSON body, they are not part of it:
    //   Content-Type     tells receivers how to read the body
    //   Acme-Request-Id  a value your own code sets, here the upstream
    //                    request that produced this order
    const char *order =
        "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

    if (s == NATS_OK)
        s = natsMsg_Create(&msg, "orders.created", NULL,
                           order, (int) strlen(order));
    if (s == NATS_OK)
        s = natsMsgHeader_Set(msg, "Content-Type", "application/json");
    if (s == NATS_OK)
        s = natsMsgHeader_Set(msg, "Acme-Request-Id", "req_7f3c9a");

    if (s == NATS_OK)
        s = natsConnection_PublishMsg(conn, msg);
    // NATS-DOC-END

    // Make sure the buffered publish reaches the server before exiting.
    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    natsMsg_Destroy(msg);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
