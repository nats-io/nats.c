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

// NATS-DOC-START
// The check endpoint handler: the same answering behavior as the raw
// responder. The framework already read the reply subject off the
// incoming request; Respond sends the answer back to it.
static microError *
handleCheck(microRequest *req)
{
    const char *answer = "{\"in_stock\":true,\"warehouse\":\"us-east\"}";

    return microRequest_Respond(req, answer, strlen(answer));
}
// NATS-DOC-END

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
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // Promote the responder into a service: a Name, a SemVer Version, and
    // a Description. The "check" endpoint binds the handler to
    // orders.inventory.check and joins the default queue group "q".
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

    err = micro_AddService(&svc, conn, &svcCfg);
    if (err == NULL)
    {
        printf("OrderInventory 1.0.0 answering on orders.inventory.check\n");
        // Block until the service stops.
        err = microService_Run(svc);
    }
    // NATS-DOC-END

    microService_Destroy(svc);
    natsConnection_Destroy(conn);

    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n",
                microError_String(err, errbuf, sizeof(errbuf)));
        microError_Destroy(err);
        return 2;
    }
    return 0;
}
