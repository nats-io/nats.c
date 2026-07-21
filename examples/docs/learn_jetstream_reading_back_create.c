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
        // Create a durable consumer on the ORDERS stream. The durable name
        // lets a reader bind to the same consumer later and pick up where it
        // left off.
        jsConsumerInfo      *ci = NULL;
        jsConsumerConfig    cc;

        jsConsumerConfig_Init(&cc);
        cc.Durable       = "billing";
        cc.DeliverPolicy = js_DeliverAll;
        cc.AckPolicy     = js_AckExplicit;

        s = js_AddConsumer(&ci, js, "ORDERS", &cc, NULL, &jerr);
        if (s == NATS_OK)
        {
            // Confirm the consumer is ready.
            printf("Created consumer: %s\n", ci->Name);
            jsConsumerInfo_Destroy(ci);
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
