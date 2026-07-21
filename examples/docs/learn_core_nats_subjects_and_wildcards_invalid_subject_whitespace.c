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

int main(void)
{
    natsConnection      *conn = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // A subject token can't contain whitespace: on the wire, a space
    // separates the subject from the reply subject and byte count. The C
    // client's publish skips the check, so this call does NOT fail: the
    // space goes straight into the PUB line and the server silently
    // misroutes: "orders.us" becomes the subject and "created" a reply
    // subject. Nobody subscribed to orders.us.created ever sees it.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

    s = natsConnection_PublishString(conn, "orders.us created", order);
    printf("publish with a space returned: %s\n", natsStatus_GetText(s));

    // Don't rely on a client-side check; keep spaces out of the subject.
    // The fix is one token per dot: orders.us.created.
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.us.created", order);
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
