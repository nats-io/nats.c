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
    // Point at site2, where the ORDERS_DR mirror lives,
    // e.g. NATS_URL=nats://site2:4222
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Before you promote ORDERS_DR, read how far it trails the
        // upstream. Lag is that gap, in messages: promote a mirror with
        // non-zero lag and you publish on top of a stream that is still
        // missing tail messages.
        s = js_GetStreamInfo(&si, js, "ORDERS_DR", NULL, &jerr);
        if ((s == NATS_OK) && (si->Mirror != NULL))
        {
            printf("Mirror of : %s\n", si->Mirror->Name);
            printf("Lag       : %" PRIu64 " messages\n", si->Mirror->Lag);
            // Active is how long ago the mirror last heard from the
            // upstream. If the upstream site is already gone, this
            // climbs and Lag freezes at whatever it was when contact
            // dropped — that frozen Lag is your data loss.
            printf("Last seen : %.1fs ago\n",
                   (double) si->Mirror->Active / 1E9);

            // Lag 0 means the mirror has caught up to every message the
            // upstream had at last contact: it is safe to promote.
            if (si->Mirror->Lag == 0)
                printf("Safe to promote\n");
            else
                printf("Messages still in flight — wait and re-check\n");
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
