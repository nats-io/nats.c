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
    kvStore             *kv   = NULL;
    kvConfig            cfg;
    uint64_t            rev   = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // The INVENTORY bucket from the earlier pages, with widget-blue stocked.
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
        // CAS retry loop. A rejected update is dropped, not queued, so on a
        // revision mismatch re-get the key and retry with the fresh revision.
        // Here the inventory service decrements widget-blue by one, safely.
        kvEntry     *entry    = NULL;
        uint64_t    revision  = 0;
        uint64_t    newRev    = 0;
        char        newValue[32];

        // Read the value AND its revision from a single get, so the pair is
        // consistent. Two separate gets could pair a stale value with a fresh
        // revision if a concurrent write landed in between.
        s = kvStore_Get(&entry, kv, "widget-blue");
        if (s == NATS_OK)
        {
            revision = kvEntry_Revision(entry);
            snprintf(newValue, sizeof(newValue), "%d",
                     atoi(kvEntry_ValueString(entry)) - 1);
            kvEntry_Destroy(entry);

            // Try the update; if a concurrent writer bumped the revision,
            // re-get and retry once.
            s = kvStore_UpdateString(&newRev, kv, "widget-blue", newValue, revision);
            if (s != NATS_OK)
            {
                printf("revision conflict, re-getting and retrying\n");
                s = kvStore_Get(&entry, kv, "widget-blue");
                if (s == NATS_OK)
                {
                    revision = kvEntry_Revision(entry);
                    snprintf(newValue, sizeof(newValue), "%d",
                             atoi(kvEntry_ValueString(entry)) - 1);
                    kvEntry_Destroy(entry);

                    s = kvStore_UpdateString(&newRev, kv, "widget-blue",
                                             newValue, revision);
                }
            }
        }
        if (s == NATS_OK)
            printf("decremented widget-blue to %s\n", newValue);
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
