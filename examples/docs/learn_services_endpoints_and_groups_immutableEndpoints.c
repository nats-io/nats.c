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

// The reserve handler for the replacement layout.
static microError *
onReserve(microRequest *req)
{
    const char *answer = "{\"reserved\":true}";

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

    // The running service: OrderInventory with a single check endpoint.
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

    // NATS-DOC-START
    // Endpoints are immutable once added: there is no remove, rename, or
    // re-subject on a running service. Read the shape back to see exactly
    // which endpoints, subjects, and queue groups it registered.
    microServiceInfo *info = NULL;

    if (err == NULL)
        err = microService_GetInfo(&info, svc);
    if (err == NULL)
    {
        for (int i = 0; i < info->EndpointsLen; i++)
            printf("endpoint %s on %s (queue group %s)\n",
                   info->Endpoints[i].Name,
                   info->Endpoints[i].Subject,
                   info->Endpoints[i].QueueGroup);
        microServiceInfo_Destroy(info);
    }

    // To change the layout, stop the service and start a replacement
    // with the new shape -- here check plus a new reserve endpoint.
    microEndpointConfig reserveConfig = {
        .Name    = "reserve",
        .Subject = "orders.inventory.reserve",
        .Handler = onReserve,
    };
    microService *replacement = NULL;

    if (err == NULL)
        err = microService_Stop(svc);
    if (err == NULL)
        err = micro_AddService(&replacement, conn, &svcConfig);
    if (err == NULL)
        err = microService_AddEndpoint(replacement, &checkConfig);
    if (err == NULL)
        err = microService_AddEndpoint(replacement, &reserveConfig);
    // NATS-DOC-END

    if (err == NULL)
    {
        printf("replacement running with check and reserve\n");
        // Serve requests until the service stops; stop with Ctrl+C.
        err = microService_Run(replacement);
    }

    microService_Destroy(replacement);
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
