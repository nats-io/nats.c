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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Per-key TTL rides on the bucket's limit markers, so the bucket
        // needs them enabled. The C client turns them on when the bucket is
        // created: a LimitMarkerTTL > 0 enables per-key TTLs and is how long
        // the bucket keeps an expiry marker. Requires nats-server 2.11+.
        kvStore     *kv  = NULL;
        uint64_t    rev  = 0;
        kvConfig    cfg;

        kvConfig_Init(&cfg);
        cfg.Bucket         = "INVENTORY";
        cfg.History        = 10;
        cfg.LimitMarkerTTL = 60LL * 60 * 1000000000LL; // 1 hour, in nanoseconds

        s = js_CreateKeyValue(&kv, js, &cfg);

        // Create flash-sale with a 30-minute TTL (in milliseconds). The value
        // removes itself 30 minutes later; no service has to clean it up.
        // A TTL is accepted on create only: neither put nor update takes one.
        if (s == NATS_OK)
            s = kvStore_CreateStringWithTTL(&rev, kv, "flash-sale", "99",
                                            30 * 60 * 1000);
        if (s == NATS_OK)
            printf("created flash-sale at revision %" PRIu64 "\n", rev);

        // A per-key TTL is create-only, and writing the key again appends a
        // value with no TTL of its own. To change a TTL, delete the key and
        // create it again with the new one.
        if (s == NATS_OK)
            s = kvStore_Delete(kv, "flash-sale");
        if (s == NATS_OK)
            s = kvStore_CreateStringWithTTL(&rev, kv, "flash-sale", "99",
                                            10 * 60 * 1000);
        if (s == NATS_OK)
            printf("flash-sale now expires in 10 minutes\n");
        // NATS-DOC-END

        kvStore_Destroy(kv);
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
