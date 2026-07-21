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
    kvStore             *kv   = NULL;
    kvConfig            cfg;
    uint64_t            rev   = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // The INVENTORY bucket from the earlier pages, with widget-blue at 41.
    if (s == NATS_OK)
    {
        kvConfig_Init(&cfg);
        cfg.Bucket  = "INVENTORY";
        cfg.History = 10;
        s = js_CreateKeyValue(&kv, js, &cfg);
    }
    if (s == NATS_OK)
        s = kvStore_PutString(&rev, kv, "widget-blue", "41");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Decrement widget-blue from 41 to 40 safely with compare-and-swap.
        kvEntry     *entry    = NULL;
        uint64_t    revision  = 0;
        uint64_t    newRev    = 0;

        // Read the current entry; it carries the value and its revision.
        s = kvStore_Get(&entry, kv, "widget-blue");
        if (s == NATS_OK)
        {
            revision = kvEntry_Revision(entry);
            printf("widget-blue is %s at revision %" PRIu64 "\n",
                   kvEntry_ValueString(entry), revision);
            kvEntry_Destroy(entry);

            // Update succeeds only if widget-blue is still at that revision.
            // If another writer got there first, the update is rejected and
            // nothing is overwritten.
            s = kvStore_UpdateString(&newRev, kv, "widget-blue", "40", revision);
        }
        if (s == NATS_OK)
            printf("decremented widget-blue to 40 at revision %" PRIu64 "\n", newRev);
        // NATS-DOC-END
    }

    kvStore_Destroy(kv);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
