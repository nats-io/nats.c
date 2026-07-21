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
    natsMsgList         list;
    jsStreamConfig      sc;
    jsSubOptions        so;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url        = getenv("NATS_URL");
    const char          *subjects[] = {"orders.>"};
    const char          *payload    = "order ord_2zr9 created";

    memset(&list, 0, sizeof(list));

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: the ORDERS stream with a few stored messages, none of them
    // on the misspelled subject used below.
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }
    if (s == NATS_OK)
        s = js_Publish(NULL, js, "orders.created", payload, (int) strlen(payload), NULL, &jerr);
    if (s == NATS_OK)
        s = js_Publish(NULL, js, "orders.shipped", "order ord_2zr9 shipped", 22, NULL, &jerr);

    // NATS-DOC-START
    // Create a durable pull consumer named "analytics-typo" on the ORDERS
    // stream. The filter "orders.shiped" has a typo (one "p"), so it matches
    // no subject the stream actually stores.
    if (s == NATS_OK)
    {
        jsSubOptions_Init(&so);
        so.Stream = "ORDERS";
        s = js_PullSubscribe(&sub, js, "orders.shiped", "analytics-typo", NULL, &so, &jerr);
    }

    // Fetch with a short wait. The fetch times out with zero messages
    // because the filter matched no stored subject; that is not an error.
    if (s == NATS_OK)
    {
        s = natsSubscription_Fetch(&list, sub, 5, 2000, &jerr);
        if (s == NATS_TIMEOUT)
            s = NATS_OK;
    }

    // A wrong filter fails silently: the pull returned nothing, and no error
    // was raised. The consumer is healthy, it just never matches a message.
    if (s == NATS_OK)
        printf("Received %d messages: the filter matched no stored subject, so the pull returned nothing (no error).\n",
               list.Count);
    // NATS-DOC-END

    natsMsgList_Destroy(&list);
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
