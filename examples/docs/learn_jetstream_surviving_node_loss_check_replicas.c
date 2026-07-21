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
    jsStreamInfo        *si   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

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
        // Before trusting ORDERS in production, confirm how many copies
        // exist. Stream info reports the replica count; on a single-node
        // laptop this is 1, which means no fault tolerance.
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
        {
            // A value of 1 is R=1, a single point of failure. Treat
            // anything below 3 as a warning sign for a stream that holds
            // real orders.
            printf("ORDERS replicas: %d\n", (int) si->Config->Replicas);
            if (si->Config->Replicas < 3)
                printf("warning: fewer than 3 replicas, no fault tolerance\n");
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
