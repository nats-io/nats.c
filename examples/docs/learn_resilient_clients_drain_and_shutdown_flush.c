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
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");
    int                 i;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // NATS-DOC-START
    // A burst of fire-and-forget publishes: the orders may still sit in
    // the client's write buffer.
    for (i = 0; (s == NATS_OK) && (i < 3); i++)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");

    // Flush sends a PING and waits for the PONG. The server processes a
    // connection's traffic in order, so when this returns the server
    // has received every publish above. natsConnection_Flush() is the
    // same call with a default 10-second bound.
    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 5000);

    if (s == NATS_OK)
        printf("flushed: the server has the batch\n");
    // NATS-DOC-END

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
