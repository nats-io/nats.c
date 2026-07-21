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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsStatus          s;
    jsErrCode           jerr  = 0;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Publish three order messages into the ORDERS stream. Each js_Publish
        // blocks until the server replies with an ack confirming where it was
        // stored.
        const char *subjects[] = {"orders.created", "orders.created", "orders.shipped"};
        const char *payloads[] = {
            "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\",\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}",
            "{\"order_id\":\"ord_2zr9\",\"customer\":\"globex\",\"total_cents\":7800,\"ts\":\"2026-05-22T10:14:25Z\"}",
            "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\",\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:31Z\"}"};

        for (int i = 0; (i < 3) && (s == NATS_OK); i++)
        {
            jsPubAck *ack = NULL;

            s = js_Publish(&ack, js, subjects[i],
                           payloads[i], (int) strlen(payloads[i]),
                           NULL, &jerr);
            if (s == NATS_OK)
            {
                printf("Stored in %s, sequence %" PRIu64 "\n", ack->Stream, ack->Sequence);
                jsPubAck_Destroy(ack);
            }
        }
        // NATS-DOC-END
    }

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
