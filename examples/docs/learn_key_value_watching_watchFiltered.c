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
    kvStore             *kv   = NULL;
    kvWatcher           *w    = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);
    if (s == NATS_OK)
        s = js_KeyValue(&kv, js, "INVENTORY");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Watch a subset of the bucket. The filter is matched like a
        // subject: '*' stands for a whole dot-separated token, so
        // "widget-*" is not a wildcard and matches nothing. With flat
        // SKU keys, name an exact key instead.
        kvEntry *e = NULL;

        s = kvStore_Watch(&w, kv, "widget-blue", NULL);
        while (s == NATS_OK)
        {
            s = kvWatcher_Next(&e, w, 60000);
            if (s != NATS_OK)
                break;
            // NULL marks the end of the snapshot; keep reading.
            if (e == NULL)
                continue;
            // Only widget-blue arrives here: the filter applies to both
            // the snapshot and the live changes.
            printf("%s: %.*s\n", kvEntry_Key(e),
                   kvEntry_ValueLen(e), (const char*) kvEntry_Value(e));
            kvEntry_Destroy(e);
            e = NULL;
        }
        // NATS-DOC-END

        // For this demo, stop cleanly after 60s without a change.
        if (s == NATS_TIMEOUT)
            s = NATS_OK;
    }

    kvWatcher_Destroy(w);
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
