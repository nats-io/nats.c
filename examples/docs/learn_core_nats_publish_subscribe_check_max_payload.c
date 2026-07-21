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
#include <string.h>

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Ask the connection for its limit before sizing a message. The value
    // comes from the INFO the server sent at connect (1 MB by default).
    if (s == NATS_OK)
        printf("maximum payload: %d bytes\n",
               (int) natsConnection_GetMaxPayload(conn));

    // A safe order publish stays far under the ceiling.
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");

    // Publishing over the ceiling fails immediately, client-side, before
    // the message reaches the server. Try a 2 MB payload:
    if (s == NATS_OK)
    {
        int   bigLen = 2 * 1024 * 1024;
        char  *big   = malloc(bigLen);

        memset(big, 'x', bigLen);
        s = natsConnection_Publish(conn, "orders.created", big, bigLen);
        if (s == NATS_MAX_PAYLOAD)
        {
            printf("oversized publish rejected: %s\n", natsStatus_GetText(s));
            s = NATS_OK;
        }
        free(big);
    }
    // NATS-DOC-END

    // Make sure the buffered publish reaches the server before exiting.
    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
