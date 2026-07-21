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
#include <string.h>

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsMsg             *req   = NULL;
    natsMsg             *reply = NULL;
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Carry a trace id through a request. Run a responder in another
    // terminal first, e.g.: nats reply orders.inventory.check --echo
    // (--echo reflects the request back and copies its headers onto the
    // reply, so the trace id you send returns to you).
    const char *order =
        "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

    if (s == NATS_OK)
        s = natsMsg_Create(&req, "orders.inventory.check", NULL,
                           order, (int) strlen(order));
    if (s == NATS_OK)
        s = natsMsgHeader_Set(req, "Acme-Trace-Id", "trace_5e21");
    if (s == NATS_OK)
        s = natsMsgHeader_Set(req, "Acme-Request-Id", "req_7f3c9a");

    // Send the check and wait up to 2 seconds for the reply.
    if (s == NATS_OK)
        s = natsConnection_RequestMsg(&reply, conn, req, 2000);

    // The trace id comes back on the reply's headers, so you can match
    // this answer to the operation that asked the question.
    if (s == NATS_OK)
    {
        const char *traceID = NULL;

        if (natsMsgHeader_Get(reply, "Acme-Trace-Id", &traceID) == NATS_OK)
            printf("Acme-Trace-Id: %s\n", traceID);
        printf("%.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
    }
    // NATS-DOC-END

    natsMsg_Destroy(req);
    natsMsg_Destroy(reply);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
