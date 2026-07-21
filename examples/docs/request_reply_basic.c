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
#include <time.h>

// NATS-DOC-START
// Set up a service: reply to each request with the current time
static void
onTimeRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    char    timeStr[64];
    time_t  now = time(NULL);

    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S%z", localtime(&now));
    natsConnection_PublishString(nc, natsMsg_GetReply(msg), timeStr);
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
        s = natsConnection_Subscribe(&sub, conn, "time", onTimeRequest, NULL);

    // Make a request and wait up to 1 second for the reply
    if (s == NATS_OK)
        s = natsConnection_Request(&reply, conn, "time", NULL, 0, 1000);
    if (s == NATS_OK)
        printf("Response: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
    else
        printf("Request failed: %s\n", natsStatus_GetText(s));
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
