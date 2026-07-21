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
    jsConsumerInfo      *ci   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Check the shipping consumer against explicit thresholds, the
        // same verdict `nats server check consumer` computes: turn the
        // pending number into a CRITICAL when more than 100 orders are
        // waiting, or a poison order has been redelivered more than 10
        // times.
        s = js_GetConsumerInfo(&ci, js, "ORDERS", "shipping", NULL, &jerr);
        if (s == NATS_OK)
        {
            bool critical = (ci->NumPending > 100) || (ci->NumRedelivered > 10);

            // With the pinned snapshot (20 pending, 3 redelivered) the
            // check stays OK; raise the load until pending crosses 100
            // and it returns CRITICAL.
            printf("%s: unprocessed=%" PRIu64 " redelivered=%" PRId64 "\n",
                   (critical ? "CRITICAL" : "OK"),
                   ci->NumPending, ci->NumRedelivered);
        }
        // NATS-DOC-END
    }

    jsConsumerInfo_Destroy(ci);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
