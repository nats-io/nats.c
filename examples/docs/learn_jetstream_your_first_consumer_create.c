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

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsConsumerInfo      *ci   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Create a durable pull consumer named "shipping" on the ORDERS stream.
        // js_DeliverAll starts from the first message in the stream, and
        // js_AckExplicit means every message must be acknowledged by hand.
        jsConsumerConfig cfg;

        jsConsumerConfig_Init(&cfg);
        cfg.Durable       = "shipping";
        cfg.DeliverPolicy = js_DeliverAll;
        cfg.AckPolicy     = js_AckExplicit;

        s = js_AddConsumer(&ci, js, "ORDERS", &cfg, NULL, &jerr);
        if (s == NATS_OK)
        {
            // Confirm the consumer is ready.
            printf("Created consumer: %s\n", ci->Name);
        }
        // NATS-DOC-END
    }

    jsConsumerInfo_Destroy(ci);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
