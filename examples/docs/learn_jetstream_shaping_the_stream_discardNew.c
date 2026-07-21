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
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: make sure the ORDERS stream from earlier pages exists and
    // holds at least one order.
    if (s == NATS_OK)
    {
        jsStreamConfig  sc;
        jsStreamInfo    *tmp = NULL;
        const char      *subjects[] = {"orders.>"};
        const char      *seed = "{\"order_id\":\"ord_0001\"}";

        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        if (js_GetStreamInfo(&tmp, js, "ORDERS", NULL, NULL) != NATS_OK)
            s = js_AddStream(NULL, js, &sc, NULL, &jerr);
        jsStreamInfo_Destroy(tmp);
        if (s == NATS_OK)
            s = js_Publish(NULL, js, "orders.created", seed, (int) strlen(seed), NULL, &jerr);
    }

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Switch ORDERS to Discard New. Discard New never drops messages
        // already stored, so capping it at one message leaves the existing
        // orders in place and puts the stream instantly over its limit. The
        // next publish is rejected rather than evicting an older order.
        const char *order = "{\"order_id\":\"ord_8w2k\"}";

        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
        {
            si->Config->Discard = js_DiscardNew;
            si->Config->MaxMsgs = 1;
            s = js_UpdateStream(NULL, js, si->Config, NULL, &jerr);
        }

        // This publish hits the full stream and fails with "maximum messages
        // exceeded" instead of succeeding silently. Handle it in the
        // publisher.
        if (s == NATS_OK)
        {
            if (js_Publish(NULL, js, "orders.created", order, (int) strlen(order), NULL, &jerr) != NATS_OK)
                printf("publish rejected: %s\n", nats_GetLastError(NULL));
        }

        // Put ORDERS back: Discard Old, no message cap (age and byte limits
        // stay).
        if (s == NATS_OK)
        {
            si->Config->Discard = js_DiscardOld;
            si->Config->MaxMsgs = -1;
            s = js_UpdateStream(NULL, js, si->Config, NULL, &jerr);
        }
        // NATS-DOC-END
    }

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
