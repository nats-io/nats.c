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
    natsSubscription    *sub  = NULL;
    jsConsumerInfo      *ci   = NULL;
    natsStatus          s;
    jsErrCode           jerr  = 0;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Create a durable pull consumer named "analytics" on the ORDERS
        // stream. FilterSubject narrows delivery to "orders.shipped", so
        // the consumer never sees "orders.created" messages even though
        // the stream stores both.
        jsConsumerConfig cc;
        jsSubOptions     so;
        natsMsgList      list = {NULL, 0};
        int              i;

        jsConsumerConfig_Init(&cc);
        cc.Durable       = "analytics";
        cc.FilterSubject = "orders.shipped";
        cc.DeliverPolicy = js_DeliverAll;
        cc.AckPolicy     = js_AckExplicit;
        s = js_AddConsumer(&ci, js, "ORDERS", &cc, NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("Created consumer: %s\n", ci->Name);
            printf("Filter: %s\n", ci->Config->FilterSubject);
        }

        // Bind to the consumer and fetch up to 5 messages with a short
        // expiry. Only "orders.shipped" messages come back; the filter
        // excludes everything else.
        jsSubOptions_Init(&so);
        so.Stream   = "ORDERS";
        so.Consumer = "analytics";
        if (s == NATS_OK)
            s = js_PullSubscribe(&sub, js, NULL, NULL, NULL, &so, &jerr);
        if (s == NATS_OK)
            s = natsSubscription_Fetch(&list, sub, 5, 2000, &jerr);
        for (i = 0; (s == NATS_OK) && (i < list.Count); i++)
        {
            printf("Received on %s\n", natsMsg_GetSubject(list.Msgs[i]));
            natsMsg_Ack(list.Msgs[i], NULL);
        }
        natsMsgList_Destroy(&list);
        // NATS-DOC-END
    }

    jsConsumerInfo_Destroy(ci);
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
