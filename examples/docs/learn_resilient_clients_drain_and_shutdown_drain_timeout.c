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

// A deliberately slow handler: two seconds per order, standing in for a
// database write.
static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    nats_Sleep(2000);
    printf("warehouse handled: %.*s\n",
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
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "warehouse");

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onMsg, NULL);

    // Give the slow handler in-flight work to finish during the drain.
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
    if (s == NATS_OK)
        s = natsConnection_Flush(conn);

    // NATS-DOC-START
    // Size the drain timeout to the slowest handler, with margin: a
    // two-second handler gets ten seconds, not a round one second. If
    // the timeout fires, the messages left are discarded and the drain
    // surfaces NATS_TIMEOUT instead of completing. The default, with
    // natsConnection_Drain(), is 30 seconds.
    if (s == NATS_OK)
        s = natsConnection_DrainTimeout(conn, 10000);

    // Wait for CLOSED: only then has the drain finished (or been cut
    // short by the timeout).
    while ((s == NATS_OK) && !natsConnection_IsClosed(conn))
        nats_Sleep(100);

    if (s == NATS_OK)
        printf("drained within the timeout, nothing dropped\n");
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
