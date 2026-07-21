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
    kvWatcher           *w    = NULL;
    kvConfig            cfg;
    uint64_t            rev   = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // The INVENTORY bucket with a few keys, so the watch has a snapshot to
    // deliver before the boundary signal.
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
    {
        // NATS-DOC-START
        // Watch the whole bucket: the snapshot comes first, then live
        // changes.
        s = kvStore_Watch(&w, kv, ">", NULL);
        while (s == NATS_OK)
        {
            kvEntry *e = NULL;

            s = kvWatcher_Next(&e, w, 5000);
            if (s != NATS_OK)
                break;

            // A NULL entry is the end-of-initial-data signal: the snapshot
            // is complete and everything after it is a live change. Consume
            // it and keep reading — breaking here would miss every live
            // update, which is the reason to watch.
            if (e == NULL)
            {
                printf("snapshot complete, now live\n");
                continue;
            }

            printf("%s @ rev %" PRIu64 ": %s\n",
                   kvEntry_Key(e), kvEntry_Revision(e), kvEntry_ValueString(e));
            kvEntry_Destroy(e);
        }
        // NATS-DOC-END

        // For this demo, a 5-second lull with no live change ends the loop.
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
