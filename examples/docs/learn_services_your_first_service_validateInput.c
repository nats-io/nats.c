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
// Validate the request body before using it. On bad input the handler
// returns a service error: the framework attaches the Nats-Service-Error
// and Nats-Service-Error-Code headers to the response and the service
// stays up for the next caller.
static microError *
handleCheck(microRequest *req)
{
    const char *data   = microRequest_GetData(req);
    int         len    = microRequest_GetDataLength(req);
    const char *answer = "{\"in_stock\":true,\"warehouse\":\"us-east\"}";

    // A real handler parses the JSON; the point is to reject garbage
    // instead of letting it crash the parse.
    if ((len == 0) || (data[0] != '{'))
        return micro_ErrorfCode(400, "invalid order: body is not JSON");

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

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    err = micro_AddService(&svc, conn, &svcCfg);
    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n",
                microError_String(err, errbuf, sizeof(errbuf)));
        microError_Destroy(err);
        natsConnection_Destroy(conn);
        return 2;
    }

    // NATS-DOC-START
    // Send a deliberately malformed body. The service answers with the
    // error headers rather than falling over; the caller reads them on
    // the reply.
    natsMsg *reply = NULL;

    s = natsConnection_RequestString(&reply, conn, "orders.inventory.check",
                                     "not-json", 2000);
    if (s == NATS_OK)
    {
        const char *code = NULL;
        const char *desc = NULL;

        natsMsgHeader_Get(reply, "Nats-Service-Error-Code", &code);
        natsMsgHeader_Get(reply, "Nats-Service-Error", &desc);
        if (code != NULL)
            printf("service error %s: %s\n", code, desc);
        natsMsg_Destroy(reply);
    }
    // NATS-DOC-END

    microService_Destroy(svc);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
