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
// The quote handler: read the order, answer with a shipping quote.
static microError *
onQuote(microRequest *req)
{
    const char *quote = "{\"carrier\":\"carrier-a\",\"quote_cents\":1500}";

    return microRequest_Respond(req, quote, strlen(quote));
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

    // NATS-DOC-START
    // A second service alongside OrderInventory. ShippingQuote promotes
    // the Core NATS shipping.quote providers into a named service: one
    // endpoint, "quote", answering on shipping.quote. Because the wire
    // subject differs from the endpoint name, set the subject explicitly.
    microServiceConfig svcConfig = {
        .Name        = "ShippingQuote",
        .Version     = "1.0.0",
        .Description = "Quotes shipping for an order",
    };
    microEndpointConfig quoteConfig = {
        .Name    = "quote",
        .Subject = "shipping.quote",
        .Handler = onQuote,
    };

    if (err == NULL)
        err = micro_AddService(&svc, conn, &svcConfig);
    if (err == NULL)
        err = microService_AddEndpoint(svc, &quoteConfig);
    // NATS-DOC-END

    if (err == NULL)
    {
        printf("ShippingQuote answering on shipping.quote\n");
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
