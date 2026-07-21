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
        // A get returns an entry, not a bare value: the value together
        // with its revision and the time it was written.
        kvEntry *e = NULL;

        s = kvStore_Get(&e, kv, "widget-blue");
        if (s == NATS_OK)
        {
            printf("value:    %.*s\n",
                   kvEntry_ValueLen(e), (const char*) kvEntry_Value(e));
            printf("revision: %llu\n",
                   (unsigned long long) kvEntry_Revision(e));
            printf("created:  %lld (unix seconds)\n",
                   (long long) (kvEntry_Created(e) / 1000000000LL));
            kvEntry_Destroy(e);
            e = NULL;
        }

        // A key that was never put fails with NATS_NOT_FOUND, which is
        // distinct from a key whose value is empty. Check for absence
        // instead of treating a missing SKU as "the count is zero".
        s = kvStore_Get(&e, kv, "widget-green");
        if (s == NATS_NOT_FOUND)
        {
            printf("widget-green not in stock yet\n");
            s = NATS_OK;
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
