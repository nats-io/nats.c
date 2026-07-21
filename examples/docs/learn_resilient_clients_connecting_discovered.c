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
static void
printPool(natsConnection *nc)
{
    char    **servers = NULL;
    int     count     = 0;
    int     i;

    if (natsConnection_GetServers(nc, &servers, &count) != NATS_OK)
        return;

    for (i = 0; i < count; i++)
    {
        printf("  %s\n", servers[i]);
        free(servers[i]);
    }
    free(servers);
}

// Fires when gossip adds a server the client has not seen before.
static void
onDiscoveredServers(natsConnection *nc, void *closure)
{
    printf("discovered new servers, known servers are now:\n");
    printPool(nc);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);

    // Configure a single URL: n1 only.
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");

    // Register the discovered-servers callback before connecting.
    if (s == NATS_OK)
        s = natsOptions_SetDiscoveredServersCB(opts, onDiscoveredServers, NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    // The first INFO already merged the rest of the cluster into the
    // pool, silently: one configured URL, three known servers.
    if (s == NATS_OK)
    {
        printf("configured one server, pool after connect:\n");
        printPool(conn);
    }
    // NATS-DOC-END

    // Stay connected for a while: add a server to the cluster and the
    // callback prints the grown pool.
    if (s == NATS_OK)
        nats_Sleep(30000);

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
