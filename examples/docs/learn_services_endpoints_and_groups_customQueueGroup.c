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

// The check handler: answer the in-stock question for an order.
static microError *
onCheck(microRequest *req)
{
    const char *answer = "{\"in_stock\":true,\"warehouse\":\"us-east\"}";

    return microRequest_Respond(req, answer, strlen(answer));
}

int main(void)
{
    natsConnection  *conn = NULL;
    microService    *svc  = NULL;
    microError      *err  = NULL;
    natsStatus      s;
    char            errbuf[256];
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
        err = micro_ErrorFromStatus(s);

    microServiceConfig svcConfig = {
        .Name        = "OrderInventory",
        .Version     = "1.0.0",
        .Description = "Checks order items against warehouse inventory",
    };

    if (err == NULL)
        err = micro_AddService(&svc, conn, &svcConfig);

    // NATS-DOC-START
    // Override the queue group on one endpoint. Instead of the service
    // default "q", check load-balances in its own group "q-inventory":
    // instances sharing that group split check requests among themselves,
    // separately from any other endpoint.
    microEndpointConfig checkConfig = {
        .Name       = "check",
        .Subject    = "orders.inventory.check",
        .QueueGroup = "q-inventory",
        .Handler    = onCheck,
    };

    if (err == NULL)
        err = microService_AddEndpoint(svc, &checkConfig);
    // NATS-DOC-END

    if (err == NULL)
    {
        printf("check answering on orders.inventory.check"
               " in queue group q-inventory\n");
        // Serve requests until the service stops; stop with Ctrl+C.
        err = microService_Run(svc);
    }

    microService_Destroy(svc);
    natsConnection_Destroy(conn);

    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n",
                microError_String(err, errbuf, sizeof(errbuf)));
        microError_Destroy(err);
        exit(2);
    }

    return 0;
}
