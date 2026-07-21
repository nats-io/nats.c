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
onDisconnected(natsConnection *nc, void *closure)
{
    printf("disconnected, will attempt reconnect\n");
}

static void
onReconnected(natsConnection *nc, void *closure)
{
    printf("reconnected\n");
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
    // Register the lifecycle callbacks before connecting: one for the
    // drop, one for the recovery.
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, onDisconnected, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, onReconnected, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Subscribe as the warehouse service. Now stop and restart the
    // nats-server: the client logs the drop, re-dials, and re-sends this
    // subscription on its own -- you never restart this program. Publish
    // an orders.created message once the server is back and it prints,
    // proving the subscription was restored on the new connection.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.created", onMsg, NULL);

    if (s == NATS_OK)
    {
        printf("subscribed to orders.created, restart the server underneath me\n");
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
