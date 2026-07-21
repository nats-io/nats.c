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
        // Put the stock count for widget-blue. The key is the SKU, the
        // value is the count as bytes. A put is unconditional: it writes
        // whether or not the key already exists.
        uint64_t rev = 0;

        s = kvStore_PutString(&rev, kv, "widget-blue", "42");
        if (s == NATS_OK)
        {
            // The bucket assigns each write a revision from a single
            // counter. INVENTORY is empty, so this first write lands at 1.
            printf("Stored widget-blue at revision %llu\n",
                   (unsigned long long) rev);
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
