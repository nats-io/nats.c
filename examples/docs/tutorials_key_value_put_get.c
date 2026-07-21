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
        s = js_KeyValue(&kv, js, "profiles");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Put a value: the key is sue.color, the value is blue. The bucket
        // assigns the write a revision; the first write lands at 1.
        uint64_t rev   = 0;
        kvEntry  *e    = NULL;

        s = kvStore_PutString(&rev, kv, "sue.color", "blue");
        if (s == NATS_OK)
            printf("stored sue.color at revision %llu\n",
                   (unsigned long long) rev);

        // Get the value back out by its key. A get returns the full entry:
        // the value together with its revision.
        if (s == NATS_OK)
            s = kvStore_Get(&e, kv, "sue.color");
        if (s == NATS_OK)
        {
            printf("sue.color revision: %llu value: %.*s\n",
                   (unsigned long long) kvEntry_Revision(e),
                   kvEntry_ValueLen(e), (const char*) kvEntry_Value(e));
            kvEntry_Destroy(e);
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
