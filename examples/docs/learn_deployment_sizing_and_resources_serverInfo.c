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
    natsConnection      *conn  = NULL;
    natsMsg             *reply = NULL;
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    // Server info is a system-account request: connect with the
    // system account's credentials (sys.creds), not the ORDERS-account
    // user creds.
    s = natsConnection_ConnectTo(&conn,
            (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Read the LIVE server limits for one node of the east cluster.
    // There is no dedicated client API for this: it is a plain
    // request on the system account's $SYS.REQ.SERVER.PING.VARZ
    // subject, filtered to the n1-east node by name. The VARZ reply
    // carries the per-server ceilings: max_payload (default 1 MiB),
    // max_connections (default 64K), and the JetStream memory/store
    // limits the node was started with.
    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn,
                "$SYS.REQ.SERVER.PING.VARZ",
                "{\"server_name\":\"n1-east\"}", 2000);
    if (s == NATS_OK)
        printf("%.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
    // NATS-DOC-END

    natsMsg_Destroy(reply);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
