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
    natsSubscription    *sub  = NULL;
    jsStreamInfo        *si   = NULL;
    jsStreamInfo        *info = NULL;
    natsMsgList         list  = {NULL, 0};
    jsErrCode           jerr  = 0;
    natsStatus          s;
    int                 i;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Start from a clean slate so the fetch below sees only this run's order.
    if (s == NATS_OK)
        js_DeleteStream(js, "FULFILLMENT", NULL, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // FULFILLMENT is the queue of paid orders awaiting shipment. With
        // WorkQueue retention the server keeps each message only until a
        // consumer acks it, then deletes it from the stream.
        jsStreamConfig  sc;
        const char      *subjects[] = {"fulfill.>"};
        const char      *order = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\"}";

        jsStreamConfig_Init(&sc);
        sc.Name        = "FULFILLMENT";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        sc.Retention   = js_WorkQueuePolicy;
        s = js_AddStream(&si, js, &sc, NULL, &jerr);
        if (s == NATS_OK)
            printf("FULFILLMENT retention: %s\n",
                   (si->Config->Retention == js_WorkQueuePolicy) ? "workqueue" : "other");

        // Queue one order to ship.
        if (s == NATS_OK)
            s = js_Publish(NULL, js, "fulfill.us", order, (int) strlen(order), NULL, &jerr);

        // A shipping worker binds to a durable pull consumer with explicit ack.
        if (s == NATS_OK)
        {
            jsConsumerConfig cc;

            jsConsumerConfig_Init(&cc);
            cc.Durable   = "shippers";
            cc.AckPolicy = js_AckExplicit;
            s = js_AddConsumer(NULL, js, "FULFILLMENT", &cc, NULL, &jerr);
        }
        if (s == NATS_OK)
        {
            jsSubOptions so;

            jsSubOptions_Init(&so);
            so.Stream   = "FULFILLMENT";
            so.Consumer = "shippers";
            s = js_PullSubscribe(&sub, js, NULL, "shippers", NULL, &so, &jerr);
        }

        // Fetch the order and ack it once the worker has shipped it.
        if (s == NATS_OK)
            s = natsSubscription_Fetch(&list, sub, 1, 5000, &jerr);
        for (i = 0; (s == NATS_OK) && (i < list.Count); i++)
        {
            printf("shipping order: %s\n", natsMsg_GetData(list.Msgs[i]));
            s = natsMsg_AckSync(list.Msgs[i], NULL, &jerr);
        }
        natsMsgList_Destroy(&list);

        // The ack removed the task from the queue, so the stream is now
        // empty. A Limits stream like ORDERS would still hold the message; a
        // WorkQueue stream drains to zero as workers finish.
        if (s == NATS_OK)
            s = js_GetStreamInfo(&info, js, "FULFILLMENT", NULL, &jerr);
        if (s == NATS_OK)
            printf("messages left in FULFILLMENT: %d\n", (int) info->State.Msgs);
        // NATS-DOC-END
    }

    jsStreamInfo_Destroy(si);
    jsStreamInfo_Destroy(info);
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
