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
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");
    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // The readiness probe answers from a status poll: ready on
        // CONNECTED, not ready while the reconnect loop works, dead on
        // CLOSED. Poll to report state; react through the events.
        switch (natsConnection_Status(conn))
        {
            case NATS_CONN_STATUS_CONNECTED:
            {
                // Connected: add the round-trip time as a latency read,
                // one PING/PONG to the server, timed.
                int64_t rtt = 0;

                if (natsConnection_GetRTT(conn, &rtt) == NATS_OK)
                    printf("ready, rtt %dus\n", (int) (rtt / 1000));
                break;
            }
            case NATS_CONN_STATUS_RECONNECTING:
                // Heals on its own; report not ready so the platform
                // routes new work elsewhere, and keep the process up.
                printf("not ready: reconnecting\n");
                break;
            case NATS_CONN_STATUS_CLOSED:
                // Terminal: the closed observer exits the process and
                // the supervisor replaces it.
                printf("dead: connection is closed\n");
                break;
            default:
                printf("not ready\n");
                break;
        }
        // NATS-DOC-END
    }

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
