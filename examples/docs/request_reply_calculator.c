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
// Calculator service: parse two numbers and reply with their sum
static void
onCalcAdd(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    int     a = 0;
    int     b = 0;
    char    result[32];

    if (sscanf(natsMsg_GetData(msg), "%d %d", &a, &b) == 2)
    {
        snprintf(result, sizeof(result), "%d", a + b);
        natsConnection_PublishString(nc, natsMsg_GetReply(msg), result);
    }
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsSubscription    *sub   = NULL;
    natsMsg             *reply = NULL;
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "calc.add", onCalcAdd, NULL);

    // Give the service a moment to be ready, then make calculations
    nats_Sleep(100);

    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn, "calc.add", "5 3", 1000);
    if (s == NATS_OK)
    {
        printf("5 + 3 = %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
        reply = NULL;

        s = natsConnection_RequestString(&reply, conn, "calc.add", "10 7", 1000);
    }
    if (s == NATS_OK)
        printf("10 + 7 = %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
    // NATS-DOC-END

    // Anything that is created need to be destroyed
    natsMsg_Destroy(reply);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
