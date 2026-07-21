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
#include <stdlib.h>

// NATS-DOC-START
// Print each message's headers as Key: Value lines, then a blank line,
// then the body -- so you can see exactly what the publisher attached.
static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    const char* *keys = NULL;
    int         nkeys = 0;
    int         i;

    if (natsMsgHeader_Keys(msg, &keys, &nkeys) == NATS_OK)
    {
        for (i = 0; i < nkeys; i++)
        {
            const char *val = NULL;

            if (natsMsgHeader_Get(msg, keys[i], &val) == NATS_OK)
                printf("%s: %s\n", keys[i], val);
        }
        free((void*) keys);
    }
    printf("\n%.*s\n", natsMsg_GetDataLength(msg), natsMsg_GetData(msg));

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Subscribe to orders.created and watch the headers arrive. Run the
    // headers publish snippet in another terminal to produce output.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "orders.created", onMsg, NULL);
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        printf("listening on orders.created\n");
        while (natsConnection_Status(conn) != NATS_CONN_STATUS_CLOSED)
            nats_Sleep(100);
    }

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
