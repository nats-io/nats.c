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
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("warehouse-2 handled: %.*s\n",
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
        s = natsOptions_SetName(opts, "warehouse-2");

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // NATS-DOC-START
    // One member of the warehouse queue group, sharing the orders.>
    // load with the other members.
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&sub, conn, "orders.>",
                                          "warehouse", onMsg, NULL);

    // Rotate this member out without dropping its in-flight orders:
    // drain the subscription, not the connection. It unsubscribes, so
    // the server routes new orders to the remaining members, and the
    // handler finishes what is already buffered here.
    if (s == NATS_OK)
        s = natsSubscription_Drain(sub);

    // The drain runs in the background; block until it completes (zero
    // means no timeout), then check how it ended.
    if (s == NATS_OK)
        s = natsSubscription_WaitForDrainCompletion(sub, 0);
    if (s == NATS_OK)
        s = natsSubscription_DrainCompletionStatus(sub);

    // The connection and every other subscription are still alive.
    if (s == NATS_OK)
        printf("member rotated out, connection still %s\n",
               natsConnection_IsClosed(conn) ? "closed" : "open");
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
