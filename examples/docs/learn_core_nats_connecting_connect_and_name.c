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

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Name the connection "warehouse" so server monitoring identifies it,
    // instead of showing a connection with no name at all.
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "warehouse");

    // Open the connection. This runs the connect handshake: the server
    // sends INFO, the client answers with CONNECT carrying the name.
    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Measure the round trip: one PING sent to the server, timed until
    // its PONG comes back. A printed time means the handshake succeeded
    // and the server is answering.
    if (s == NATS_OK)
    {
        int64_t rtt = 0;

        s = natsConnection_GetRTT(conn, &rtt);
        if (s == NATS_OK)
            printf("round trip: %dus\n", (int) (rtt / 1000));
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
