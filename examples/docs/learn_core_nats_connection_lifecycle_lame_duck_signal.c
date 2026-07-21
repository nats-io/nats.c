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
onLameDuck(natsConnection *nc, void *closure)
{
    // The server announced it is going away. Across a cluster this is
    // where a client would move early, e.g. by forcing a reconnect to
    // another server, before its socket is cut.
    printf("server entered lame-duck mode, it will close this connection soon\n");
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Register a callback for the lame-duck notice. When an operator takes
    // the server down gracefully (nats-server --signal ldm), the server
    // stops accepting new connections and announces to every connected
    // client that it is about to go away; this callback is that notice.
    if (s == NATS_OK)
        s = natsOptions_SetLameDuckModeCB(opts, onLameDuck, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Keep the client running. Signal the server from another terminal
    // with: nats-server --signal ldm
    if (s == NATS_OK)
    {
        printf("connected, waiting for the lame-duck notice...\n");
        while (natsConnection_Status(conn) != NATS_CONN_STATUS_CLOSED)
            nats_Sleep(100);
    }
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
