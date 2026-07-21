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

int main(void)
{
    natsConnection      *conn = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // The third discovery level adds the service id, addressing exactly
    // one instance instead of all of them. Pick an id from an earlier
    // INFO or PING reply; replace the one below with your own.
    const char *instanceID = "JJTBR2MN3JSAYBI5ST7E32";
    natsMsg    *reply      = NULL;
    char       subject[256];

    // $SRV.STATS.OrderInventory.<id> reaches only the instance with that
    // id, so a single reply is expected -- no deadline loop needed.
    snprintf(subject, sizeof(subject),
             "$SRV.STATS.OrderInventory.%s", instanceID);
    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn, subject, "", 1000);
    if (s == NATS_OK)
    {
        printf("stats: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
        reply = NULL;
    }

    // PING at the instance level is the lightweight check: it confirms
    // that one specific instance is alive and reports its name, id, and
    // version.
    snprintf(subject, sizeof(subject),
             "$SRV.PING.OrderInventory.%s", instanceID);
    if (s == NATS_OK)
        s = natsConnection_RequestString(&reply, conn, subject, "", 1000);
    if (s == NATS_OK)
    {
        printf("ping: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
    }
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
