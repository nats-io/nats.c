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
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: recreate the FULFILLMENT work-queue stream from earlier on
    // this page, with no consumers yet.
    if (s == NATS_OK)
    {
        jsStreamConfig  sc;
        const char      *subjects[] = {"fulfill.>"};

        js_DeleteStream(js, "FULFILLMENT", NULL, NULL);
        jsStreamConfig_Init(&sc);
        sc.Name        = "FULFILLMENT";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        sc.Retention   = js_WorkQueuePolicy;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // One unfiltered consumer covers every order in the queue.
        jsConsumerConfig cc;

        jsConsumerConfig_Init(&cc);
        cc.Durable   = "shippers";
        cc.AckPolicy = js_AckExplicit;
        s = js_AddConsumer(NULL, js, "FULFILLMENT", &cc, NULL, &jerr);
        if (s == NATS_OK)
            printf("created unfiltered consumer: shippers\n");

        // A WorkQueue stream delivers each message to exactly one consumer,
        // so it refuses a second consumer whose reach overlaps the first.
        // Two unfiltered consumers both cover fulfill.>, so this is rejected
        // (err 10099: multiple non-filtered consumers not allowed on
        // workqueue stream).
        if (s == NATS_OK)
        {
            jsConsumerConfig_Init(&cc);
            cc.Durable   = "eu-shippers";
            cc.AckPolicy = js_AckExplicit;
            if (js_AddConsumer(NULL, js, "FULFILLMENT", &cc, NULL, &jerr) != NATS_OK)
                printf("second unfiltered consumer rejected: %s\n", nats_GetLastError(NULL));
        }

        // Drop the catch-all consumer so we can split the queue by region
        // instead.
        if (s == NATS_OK)
            s = js_DeleteConsumer(js, "FULFILLMENT", "shippers", NULL, &jerr);

        // Filtered consumers are allowed as long as their subjects don't
        // overlap. us-shippers takes fulfill.us, eu-shippers takes
        // fulfill.eu, and each order still goes to exactly one worker.
        if (s == NATS_OK)
        {
            jsConsumerConfig_Init(&cc);
            cc.Durable       = "us-shippers";
            cc.FilterSubject = "fulfill.us";
            cc.AckPolicy     = js_AckExplicit;
            s = js_AddConsumer(NULL, js, "FULFILLMENT", &cc, NULL, &jerr);
        }
        if (s == NATS_OK)
        {
            jsConsumerConfig_Init(&cc);
            cc.Durable       = "eu-shippers";
            cc.FilterSubject = "fulfill.eu";
            cc.AckPolicy     = js_AckExplicit;
            s = js_AddConsumer(NULL, js, "FULFILLMENT", &cc, NULL, &jerr);
        }
        if (s == NATS_OK)
            printf("created filtered consumers: us-shippers (fulfill.us), eu-shippers (fulfill.eu)\n");
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
