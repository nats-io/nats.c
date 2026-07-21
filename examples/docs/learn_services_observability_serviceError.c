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
#include <stdlib.h>

// NATS-DOC-START
// The check handler rejects an order whose total is not a positive
// amount. Returning a microError makes the framework attach the
// Nats-Service-Error and Nats-Service-Error-Code headers, send the
// reply, and bump num_errors and last_error for this endpoint.
static microError *
onCheck(microRequest *req)
{
    const char *answer = "{\"in_stock\":true,\"warehouse\":\"us-east\"}";
    char       body[512];
    int        len     = microRequest_GetDataLength(req);
    const char *field;
    long       total   = 0;

    if (len >= (int) sizeof(body))
        len = (int) sizeof(body) - 1;
    memcpy(body, microRequest_GetData(req), len);
    body[len] = '\0';

    field = strstr(body, "\"total_cents\":");
    if (field != NULL)
        total = strtol(field + strlen("\"total_cents\":"), NULL, 10);

    if (total <= 0)
        return micro_ErrorfCode(400, "order total must be positive");

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
        err = micro_ErrorFromStatus(s);

    microServiceConfig svcConfig = {
        .Name        = "OrderInventory",
        .Version     = "1.0.0",
        .Description = "Checks order items against warehouse inventory",
    };
    microEndpointConfig checkConfig = {
        .Name    = "check",
        .Subject = "orders.inventory.check",
        .Handler = onCheck,
    };

    if (err == NULL)
        err = micro_AddService(&svc, conn, &svcConfig);
    if (err == NULL)
        err = microService_AddEndpoint(svc, &checkConfig);

    if (err == NULL)
    {
        printf("check answering on orders.inventory.check\n");
        // Serve requests until the service stops; stop with Ctrl+C.
        // Send an order with "total_cents":0 and read the stats to see
        // num_errors move.
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
