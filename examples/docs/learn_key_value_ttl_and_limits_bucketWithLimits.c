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
        // Create a throwaway CACHE bucket to show the three bucket-level
        // limits. TTL here is the bucket-wide max age — every value older
        // than 1 hour is removed, regardless of key — not a per-key TTL.
        kvStore     *cache = NULL;
        kvStatus    *sts   = NULL;
        kvConfig    cfg;

        kvConfig_Init(&cfg);
        cfg.Bucket       = "CACHE";
        cfg.History      = 1;
        cfg.TTL          = 60LL * 60 * 1000000000LL; // 1 hour, in nanoseconds
        cfg.MaxBytes     = 16 * 1024 * 1024;         // total size cap: 16 MiB
        cfg.MaxValueSize = 64 * 1024;                // largest single value: 64 KiB

        s = js_CreateKeyValue(&cache, js, &cfg);
        if (s == NATS_OK)
            s = kvStore_Status(&sts, cache);
        if (s == NATS_OK)
        {
            // A put larger than the value cap, or one that would push the
            // bucket past its size cap, is rejected; existing values stay.
            printf("Bucket: %s\n", kvStatus_Bucket(sts));
            printf("History kept: %" PRIi64 "\n", kvStatus_History(sts));
            printf("Maximum age: %" PRIi64 "s\n", kvStatus_TTL(sts) / 1000000000LL);
            kvStatus_Destroy(sts);
        }
        kvStore_Destroy(cache);
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
