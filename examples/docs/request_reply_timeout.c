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

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Request with a custom timeout of 2 seconds
        s = natsConnection_RequestString(&reply, conn, "service", "data", 2000);
        if (s == NATS_TIMEOUT)
            printf("Request timed out\n");
        else if (s != NATS_OK)
            printf("Request failed: %s\n", natsStatus_GetText(s));
        else
            printf("Response: %.*s\n",
                   natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        // NATS-DOC-END
    }

    // A timeout (or no responders when no service is running) is the
    // expected outcome of this example.
    if ((s == NATS_TIMEOUT) || (s == NATS_NO_RESPONDERS))
        s = NATS_OK;

    // Anything that is created need to be destroyed
    natsMsg_Destroy(reply);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
