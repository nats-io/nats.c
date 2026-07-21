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

    // The INVENTORY bucket with history kept: widget-blue has two prior
    // revisions, widget-red has one value.
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
        s = kvStore_PutString(&rev, kv, "widget-blue", "40");
    if (s == NATS_OK)
        s = kvStore_PutString(&rev, kv, "widget-red", "17");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Delete and purge both make a key read empty, but they keep
        // different amounts of history.
        kvEntryList list;
        const char  *ops[] = { "UNKNOWN", "PUT", "DELETE", "PURGE" };
        int         i;

        // Delete leaves a non-destructive marker. The key reads empty, but
        // every prior revision the bucket keeps stays readable through
        // history: the kept PUTs plus a final DELETE marker.
        s = kvStore_Delete(kv, "widget-blue");
        if (s == NATS_OK)
            s = kvStore_History(&list, kv, "widget-blue", NULL);
        if (s == NATS_OK)
        {
            printf("widget-blue after delete:\n");
            for (i = 0; i < list.Count; i++)
            {
                kvEntry *e = list.Entries[i];

                printf("  rev %" PRIu64 " %s %s\n",
                       kvEntry_Revision(e), ops[kvEntry_Operation(e)],
                       (kvEntry_Value(e) != NULL) ? kvEntry_ValueString(e) : "");
            }
            kvEntryList_Destroy(&list);
        }

        // Purge is destructive. It drops every earlier message on the key's
        // subject and keeps only a rollup marker, so history collapses to a
        // single PURGE entry; the prior values are gone from disk.
        if (s == NATS_OK)
            s = kvStore_Purge(kv, "widget-red", NULL);
        if (s == NATS_OK)
            s = kvStore_History(&list, kv, "widget-red", NULL);
        if (s == NATS_OK)
        {
            printf("widget-red after purge:\n");
            for (i = 0; i < list.Count; i++)
            {
                kvEntry *e = list.Entries[i];

                printf("  rev %" PRIu64 " %s %s\n",
                       kvEntry_Revision(e), ops[kvEntry_Operation(e)],
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
