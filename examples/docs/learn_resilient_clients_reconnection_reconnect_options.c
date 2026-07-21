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
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Tune reconnect for a long-lived service: unlimited retries (a
    // negative max), a two-second base wait between attempts on the same
    // server, and jitter left at its default (up to 100ms, 1s over TLS)
    // so a fleet of clients does not retry in lockstep.
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, -1);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 2000);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Publish one order against the pool. If the node the client landed
    // on goes away, it reconnects to another member on its own.
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
    // NATS-DOC-END

    if (s == NATS_OK)
        s = natsConnection_Flush(conn);

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
