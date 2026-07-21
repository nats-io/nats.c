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
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfo        *si   = NULL;
    natsStatus          s;
    jsErrCode           jerr  = 0;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Load FULFILLMENT and take its current config.
        s = js_GetStreamInfo(&si, js, "FULFILLMENT", NULL, &jerr);
        if (s == NATS_OK)
        {
            // Try to turn the WorkQueue stream into a Limits stream by
            // flipping retention and pushing the update back.
            jsStreamInfo *updated = NULL;

            si->Config->Retention = js_LimitsPolicy;

            s = js_UpdateStream(&updated, js, si->Config, NULL, &jerr);
            if (s != NATS_OK)
            {
                // The server refuses: a stream's retention policy is fixed
                // for its lifetime (err 10052: stream configuration update
                // can not change retention policy to/from workqueue). To
                // change it, recreate the stream.
                printf("retention switch rejected: %s\n", nats_GetLastError(NULL));
                s = NATS_OK;
            }
            else
            {
                printf("update unexpectedly succeeded\n");
                jsStreamInfo_Destroy(updated);
            }
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
