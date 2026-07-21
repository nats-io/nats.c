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

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfo        *si   = NULL;
    jsConsumerInfo      *ci   = NULL;
    natsSubscription    *sub  = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Create a stream that captures any subject under `orders.`
        jsStreamConfig  cfg;
        const char      *subjects[] = {"orders.>"};

        jsStreamConfig_Init(&cfg);
        cfg.Name        = "ORDERS";
        cfg.Subjects    = subjects;
        cfg.SubjectsLen = 1;
        cfg.Storage     = js_FileStorage;

        s = js_AddStream(&si, js, &cfg, NULL, &jerr);

        // Publish a few orders
        if (s == NATS_OK)
            s = js_Publish(NULL, js, "orders.new", "Order #1001",
                           (int) strlen("Order #1001"), NULL, &jerr);
        if (s == NATS_OK)
            s = js_Publish(NULL, js, "orders.new", "Order #1002",
                           (int) strlen("Order #1002"), NULL, &jerr);
        if (s == NATS_OK)
            s = js_Publish(NULL, js, "orders.shipped", "Order #1001 shipped",
                           (int) strlen("Order #1001 shipped"), NULL, &jerr);

        // Create a durable pull consumer that delivers from the beginning
        if (s == NATS_OK)
        {
            jsConsumerConfig    cc;

            jsConsumerConfig_Init(&cc);
            cc.Durable   = "order-processor";
            cc.AckPolicy = js_AckExplicit;

            s = js_AddConsumer(&ci, js, "ORDERS", &cc, NULL, &jerr);
        }

        // Bind a pull subscription to the consumer
        if (s == NATS_OK)
        {
            jsSubOptions    so;

            jsSubOptions_Init(&so);
            so.Stream   = "ORDERS";
            so.Consumer = "order-processor";

            s = js_PullSubscribe(&sub, js, NULL, NULL, NULL, &so, &jerr);
        }

        // Fetch a batch and acknowledge each message
        if (s == NATS_OK)
        {
            natsMsgList list = {NULL, 0};

            s = natsSubscription_Fetch(&list, sub, 3, 2000, &jerr);
            if (s == NATS_OK)
            {
                for (int i = 0; i < list.Count; i++)
                {
                    printf("Received on %s: %s\n",
                           natsMsg_GetSubject(list.Msgs[i]),
                           natsMsg_GetData(list.Msgs[i]));
                    natsMsg_Ack(list.Msgs[i], NULL);
                }
                natsMsgList_Destroy(&list);
            }
        }
        // NATS-DOC-END
    }

    natsSubscription_Destroy(sub);
    jsConsumerInfo_Destroy(ci);
    jsStreamInfo_Destroy(si);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
