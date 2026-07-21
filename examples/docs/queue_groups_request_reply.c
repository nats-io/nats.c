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
#include <stdio.h>
#include <string.h>

// NATS-DOC-START
// Service instance: parse the request, compute, reply with the result
// and the instance that handled it.
static void
onCalculate(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    const char  *instanceID = (const char *) closure;
    int         a = 0;
    int         b = 0;
    char        response[128];

    // Parse request
    sscanf(natsMsg_GetData(msg), "{\"a\":%d,\"b\":%d}", &a, &b);

    // Process request and send response
    snprintf(response, sizeof(response),
             "{\"result\":%d,\"processedBy\":\"%s\"}", a + b, instanceID);
    if (natsMsg_GetReply(msg) != NULL)
        natsConnection_PublishString(nc, natsMsg_GetReply(msg), response);

    printf("Instance %s processed request\n", instanceID);

    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    natsSubscription    *subs[3];
    const char          *ids[3] = {"instance-1", "instance-2", "instance-3"};
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    for (int i = 0; i < 3; i++)
        subs[i] = NULL;

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Start multiple service instances in the same queue group
    for (int i = 0; (s == NATS_OK) && (i < 3); i++)
        s = natsConnection_QueueSubscribe(&subs[i], conn, "api.calculate",
                                          "api-workers", onCalculate,
                                          (void *) ids[i]);

    // Make requests - automatically load balanced
    for (int i = 0; (s == NATS_OK) && (i < 10); i++)
    {
        natsMsg *reply = NULL;
        char    request[64];

        snprintf(request, sizeof(request), "{\"a\":%d,\"b\":%d}", i, i * 2);
        s = natsConnection_RequestString(&reply, conn, "api.calculate",
                                         request, 1000);
        if (s == NATS_OK)
        {
            printf("Reply: %.*s\n",
                   natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
            natsMsg_Destroy(reply);
        }
    }
    // NATS-DOC-END

    for (int i = 0; i < 3; i++)
        natsSubscription_Destroy(subs[i]);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
