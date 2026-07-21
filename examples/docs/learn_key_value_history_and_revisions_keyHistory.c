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

    // The INVENTORY bucket with history raised to 10. The puts to widget-red
    // and gadget-pro take revision numbers between widget-blue's writes, so
    // its revisions are not consecutive.
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
        s = kvStore_PutString(&rev, kv, "widget-red", "17");
    if (s == NATS_OK)
        s = kvStore_PutString(&rev, kv, "gadget-pro", "5");
    if (s == NATS_OK)
        s = kvStore_PutString(&rev, kv, "widget-blue", "40");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Read the kept history of a key, oldest first. Each entry is one
        // revision: the key, its revision number, the operation, and the
        // value. The revision numbers aren't consecutive — the counter is
        // bucket-wide, not per key.
        kvEntryList list;
        int         i;

        s = kvStore_History(&list, kv, "widget-blue", NULL);
        if (s == NATS_OK)
        {
            printf("History for INVENTORY > widget-blue\n");
            for (i = 0; i < list.Count; i++)
            {
                kvEntry *e = list.Entries[i];

                printf("  %s rev %" PRIu64 " %s %s\n",
                       kvEntry_Key(e), kvEntry_Revision(e),
                       (kvEntry_Operation(e) == kvOp_Put) ? "PUT" : "MARKER",
                       (kvEntry_Value(e) != NULL) ? kvEntry_ValueString(e) : "");
            }
            kvEntryList_Destroy(&list);
        }
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
