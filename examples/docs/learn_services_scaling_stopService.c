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
#include <string.h>

static microError *
handleCheck(microRequest *req)
{
    const char *answer = "{\"in_stock\":true,\"warehouse\":\"us-east\"}";

    return microRequest_Respond(req, answer, strlen(answer));
}

int main(void)
{
    natsConnection  *conn  = NULL;
    natsConnection  *conn2 = NULL;
    microService    *svc1  = NULL;
    microService    *svc2  = NULL;
    microError      *err   = NULL;
    natsStatus      s;
    char            errbuf[256];
    const char      *url   = getenv("NATS_URL");

    microEndpointConfig checkCfg = {
        .Name    = "check",
        .Subject = "orders.inventory.check",
        .Handler = handleCheck,
    };
    microServiceConfig svcCfg = {
        .Name        = "OrderInventory",
        .Version     = "1.0.0",
        .Description = "Checks whether an order's items are in stock",
        .Endpoint    = &checkCfg,
    };

    // Two running instances, one connection each.
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_ConnectTo(&conn2, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    err = micro_AddService(&svc1, conn, &svcCfg);
    if (err == NULL)
        err = micro_AddService(&svc2, conn2, &svcCfg);
    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n",
                microError_String(err, errbuf, sizeof(errbuf)));
        microError_Destroy(err);
        return 2;
    }

    // NATS-DOC-START
    // Stop the first instance gracefully. Stop drains the endpoint
    // subscriptions: the instance leaves queue group "q" so no new
    // requests route to it, requests it already accepted finish, and the
    // $SRV discovery verbs unsubscribe.
    err = microService_Stop(svc1);

    // The queue group rebalanced: every new order now lands on the
    // remaining instance.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    natsMsg    *reply = NULL;

    s = natsConnection_RequestString(&reply, conn, "orders.inventory.check",
                                     order, 2000);
    if (s == NATS_OK)
    {
        printf("survivor answered: %.*s\n",
               natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
        natsMsg_Destroy(reply);
    }
    // NATS-DOC-END

    microService_Destroy(svc1);
    microService_Destroy(svc2);
    natsConnection_Destroy(conn);
    natsConnection_Destroy(conn2);

    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n",
                microError_String(err, errbuf, sizeof(errbuf)));
        microError_Destroy(err);
        return 2;
    }
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        return 2;
    }
    return 0;
}
