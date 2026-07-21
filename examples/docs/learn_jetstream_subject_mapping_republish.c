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
    natsConnection      *conn    = NULL;
    jsCtx               *js      = NULL;
    jsStreamInfo        *si      = NULL;
    jsStreamInfo        *updated = NULL;
    jsErrCode           jerr     = 0;
    natsStatus          s;
    const char          *url     = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: make sure the ORDERS stream from earlier pages exists.
    if (s == NATS_OK)
    {
        jsStreamConfig  sc;
        jsStreamInfo    *tmp = NULL;
        const char      *subjects[] = {"orders.>"};

        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        if (js_GetStreamInfo(&tmp, js, "ORDERS", NULL, NULL) != NATS_OK)
            s = js_AddStream(NULL, js, &sc, NULL, &jerr);
        jsStreamInfo_Destroy(tmp);
    }

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Read the current config, add a republish rule, and push the
        // update. Every message stored on "orders.>" is also echoed live to
        // "dash.orders.>" so a dashboard can subscribe without touching the
        // stream.
        jsRePublish rp;

        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
        {
            jsRePublish_Init(&rp);
            rp.Source      = "orders.>";
            rp.Destination = "dash.orders.>";
            // rp.HeadersOnly = true; // republish only the headers, not the body
            si->Config->RePublish = &rp;
            s = js_UpdateStream(&updated, js, si->Config, NULL, &jerr);
        }
        if (s == NATS_OK)
            printf("Republish destination: %s\n", updated->Config->RePublish->Destination);
        // NATS-DOC-END
    }

    jsStreamInfo_Destroy(si);
    jsStreamInfo_Destroy(updated);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
