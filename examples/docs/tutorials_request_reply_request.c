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
    // Call the responder once. The client creates a private inbox,
    // publishes an empty request on "time" with the inbox attached, and
    // waits up to two seconds for the first reply.
    natsMsg *reply = NULL;

    s = natsConnection_RequestString(&reply, conn, "time", "", 2000);
    if (s == NATS_OK)
    {
        printf("received: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
    }
    else if (s == NATS_NO_RESPONDERS)
    {
        // Nobody is subscribed to "time": the server says so right away
        // instead of letting the request wait out the timeout.
        printf("no responders are available\n");
    }
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    if ((s != NATS_OK) && (s != NATS_NO_RESPONDERS))
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
