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
#include <inttypes.h>

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

    // One connection per instance, as if each were its own process.
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_ConnectTo(&conn2, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // The first instance, exactly as on the first page.
    err = micro_AddService(&svc1, conn, &svcCfg);

    // NATS-DOC-START
    // A second instance is the same AddService call again: same Name,
    // Version, and endpoint. The framework hands it a fresh service ID,
    // and its check endpoint joins the same queue group "q".
    if (err == NULL)
        err = micro_AddService(&svc2, conn2, &svcCfg);

    // Send a burst of orders. The server delivers each request to exactly
    // one member of the queue group, so the six spread across the two
    // instances without any extra configuration.
    const char *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    natsMsg    *reply = NULL;
    natsStatus rs     = NATS_OK;
    int        i;

    for (i = 0; (err == NULL) && (rs == NATS_OK) && (i < 6); i++)
    {
        rs = natsConnection_RequestString(&reply, conn,
                                          "orders.inventory.check", order, 2000);
        if (rs == NATS_OK)
        {
            natsMsg_Destroy(reply);
            reply = NULL;
        }
    }

    // Each instance keeps its own counters; the two totals add up to six.
    microServiceStats *stats1 = NULL;
    microServiceStats *stats2 = NULL;

    if (err == NULL)
        err = microService_GetStats(&stats1, svc1);
    if (err == NULL)
        err = microService_GetStats(&stats2, svc2);
    if (err == NULL)
        printf("instance 1 handled %" PRId64 ", instance 2 handled %" PRId64 "\n",
               stats1->Endpoints[0].NumRequests,
               stats2->Endpoints[0].NumRequests);
    // NATS-DOC-END

    microServiceStats_Destroy(stats1);
    microServiceStats_Destroy(stats2);
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
    if (rs != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        return 2;
    }
    return 0;
}
