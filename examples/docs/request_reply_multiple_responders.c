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
// Each responder replies with its own name (passed as the closure)
static void
onCalcRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    char result[64];

    snprintf(result, sizeof(result),
             "calculated result from %s", (const char*) closure);
    natsConnection_PublishString(nc, natsMsg_GetReply(msg), result);
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsSubscription    *subA  = NULL;
    natsSubscription    *subB  = NULL;
    natsMsg             *reply = NULL;
    natsStatus          s;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Multiple responders - only the first response is returned
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&subA, conn, "calc.add", onCalcRequest, (void*) "A");
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&subB, conn, "calc.add", onCalcRequest, (void*) "B");

    // Gets one response
    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn, "calc.add", "data", 1000);
    if (s == NATS_OK)
        printf("Got response: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
    // NATS-DOC-END

    // Anything that is created need to be destroyed
    natsMsg_Destroy(reply);
    natsSubscription_Destroy(subA);
    natsSubscription_Destroy(subB);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
