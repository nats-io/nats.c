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

// NATS-DOC-START
// The closed observer: the last event the connection delivers. After
// this fires nothing inside the client recovers, so the reaction is to
// exit and let the supervisor restart the process, or raise an alert.
static void
onClosed(natsConnection *nc, void *closure)
{
    const char *err = NULL;

    natsConnection_GetLastError(nc, &err);
    printf("connection closed: %s -- exiting for the supervisor\n",
           (err == NULL) ? "no error" : err);
}
// NATS-DOC-END

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
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "warehouse");

    // NATS-DOC-START
    // Wire it even with unlimited reconnects: a Close() on an error
    // path or a repeated authentication failure still reaches CLOSED.
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, onClosed, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.>", onMsg, NULL);

    // Block until the terminal transition, then exit non-zero.
    if (s == NATS_OK)
    {
        while (!natsConnection_IsClosed(conn))
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

    // The connection is CLOSED: report failure so the supervisor
    // restarts the process.
    return 1;
}
