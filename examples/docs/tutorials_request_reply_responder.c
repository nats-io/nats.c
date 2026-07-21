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
// Answer every request on "time" with the current date and time, sent
// back on the request's private reply subject.
static void
onRequest(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    char   now[64];
    time_t t = time(NULL);

    strftime(now, sizeof(now), "%a %b %e %H:%M:%S %Z %Y", localtime(&t));
    if (natsMsg_GetReply(msg) != NULL)
        natsConnection_PublishString(nc, natsMsg_GetReply(msg), now);

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Subscribe on "time" and leave the responder running; Ctrl-C to stop.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, "time", onRequest, NULL);
    // NATS-DOC-END

    if (s == NATS_OK)
    {
        printf("listening on \"time\"\n");
        while (natsConnection_Status(conn) != NATS_CONN_STATUS_CLOSED)
            nats_Sleep(100);
    }

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
