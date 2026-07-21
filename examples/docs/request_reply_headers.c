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
// Header aware service: echo the data and the request headers back
static void
onServiceRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsMsg     *responseMsg = NULL;
    const char  *requestID   = NULL;
    const char  *priority    = NULL;

    natsMsgHeader_Get(msg, "X-Request-ID", &requestID);
    natsMsgHeader_Get(msg, "X-Priority", &priority);

    natsMsg_Create(&responseMsg, natsMsg_GetReply(msg), NULL,
                   natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    natsMsgHeader_Add(responseMsg, "X-Response-ID", "123");
    if (requestID != NULL)
        natsMsgHeader_Add(responseMsg, "X-Request-ID", requestID);
    if (priority != NULL)
        natsMsgHeader_Add(responseMsg, "X-Priority", priority);

    natsConnection_PublishMsg(nc, responseMsg);

    natsMsg_Destroy(responseMsg);
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn     = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsMsg             *response = NULL;
    natsStatus          s;
    const char          *url      = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "service", onServiceRequest, NULL);

    // Create a message with headers
    if (s == NATS_OK)
        s = natsMsg_Create(&msg, "service", NULL, "data", 4);
    if (s == NATS_OK)
        s = natsMsgHeader_Add(msg, "X-Request-ID", "123");
    if (s == NATS_OK)
        s = natsMsgHeader_Add(msg, "X-Priority", "high");

    // Send the request with headers
    if (s == NATS_OK)
        s = natsConnection_RequestMsg(&response, conn, msg, 1000);
    if (s == NATS_OK)
    {
        const char *responseID = NULL;

        printf("Response: %.*s\n",
               natsMsg_GetDataLength(response), natsMsg_GetData(response));
        if (natsMsgHeader_Get(response, "X-Response-ID", &responseID) == NATS_OK)
            printf("Response ID: %s\n", responseID);
    }
    else
        printf("Error: %s\n", natsStatus_GetText(s));
    // NATS-DOC-END

    // Anything that is created need to be destroyed
    natsMsg_Destroy(msg);
    natsMsg_Destroy(response);
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
