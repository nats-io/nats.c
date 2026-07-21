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
// One callback per event. Disconnect, reconnect, discovered servers,
// and lame duck are logging; closed is the one that demands an action.
static void
onDisconnected(natsConnection *nc, void *closure)
{
    printf("disconnected, reconnect loop running\n");
}

static void
onReconnected(natsConnection *nc, void *closure)
{
    char url[256];

    if (natsConnection_GetConnectedUrl(nc, url, sizeof(url)) == NATS_OK)
        printf("reconnected to %s\n", url);
}

static void
onDiscoveredServers(natsConnection *nc, void *closure)
{
    printf("server pool grew from cluster gossip\n");
}

static void
onLameDuck(natsConnection *nc, void *closure)
{
    printf("server is shutting down soon (lame duck)\n");
}

static void
onClosed(natsConnection *nc, void *closure)
{
    printf("connection closed, no more events after this\n");
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;

    s = natsOptions_Create(&opts);

    // NATS-DOC-START
    const char *servers[] = {"nats://n1:4222", "nats://n2:4222",
                             "nats://n3:4222"};

    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, servers, 3);
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");

    // Wire the full event surface before connecting. The C client has
    // no per-attempt reconnect-error callback; a long outage shows as
    // the gap between the disconnect and reconnect events.
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, onDisconnected, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, onReconnected, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetDiscoveredServersCB(opts, onDiscoveredServers, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetLameDuckModeCB(opts, onLameDuck, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, onClosed, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // Restart a server in the pool and watch the events print.
    if (s == NATS_OK)
    {
        printf("connected, restart a server to see the events\n");
        while (!natsConnection_IsClosed(conn))
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
