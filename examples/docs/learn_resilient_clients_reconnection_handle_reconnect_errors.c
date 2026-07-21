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
    fprintf(stderr, "order-svc disconnected, retrying without limit\n");
}

static void
onReconnected(natsConnection *nc, void *closure)
{
    char url[256];

    natsConnection_GetConnectedUrl(nc, url, sizeof(url));
    fprintf(stderr, "order-svc reconnected to %s\n", url);
}

static void
onClosed(natsConnection *nc, void *closure)
{
    fprintf(stderr, "order-svc connection closed for good\n");
}

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("received: %.*s\n",
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    natsMsg_Destroy(msg);
}

int main(void)
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
        s = natsOptions_SetName(opts, "order-svc");

    // NATS-DOC-START
    // Retry without limit: a negative max means the client keeps cycling
    // the pool through a long outage instead of moving to CLOSED.
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, -1);

    // Make every drop and recovery visible. The C client reports the
    // outage through the disconnected callback and the recovery through
    // the reconnected one; the closed callback fires only if the
    // connection ends for good, which unlimited retries prevent here.
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, onDisconnected, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, onReconnected, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, onClosed, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Subscribe, then stop and restart the server underneath: the
    // callbacks log the disconnect and the reconnect, and the
    // subscription resumes on its own.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onMsg, NULL);
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        printf("subscribed to orders.>, kill and restart the server to watch\n");
        while (natsConnection_Status(conn) != NATS_CONN_STATUS_CLOSED)
            nats_Sleep(100);
    }

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
