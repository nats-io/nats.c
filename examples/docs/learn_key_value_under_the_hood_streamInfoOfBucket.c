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

    // The INVENTORY bucket from the earlier pages, with a key in it.
    if (s == NATS_OK)
    {
        kvConfig_Init(&cfg);
        cfg.Bucket  = "INVENTORY";
        cfg.History = 10;
        s = js_CreateKeyValue(&kv, js, &cfg);
    }
    if (s == NATS_OK)
        s = kvStore_PutString(&rev, kv, "widget-blue", "42");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // The INVENTORY bucket is a JetStream stream named KV_INVENTORY. The
        // KV calls hide that name, but the stream API sees straight through
        // to it. Ask for the stream, including its per-subject counts.
        jsStreamInfo    *si  = NULL;
        jsErrCode       jerr = 0;
        jsOptions       o;

        jsOptions_Init(&o);
        o.Stream.Info.SubjectsFilter = "$KV.INVENTORY.>";

        s = js_GetStreamInfo(&si, js, "KV_INVENTORY", &o, &jerr);
        if (s == NATS_OK)
        {
            // The config is the bucket's settings written in stream terms.
            printf("Subjects: %s\n", si->Config->Subjects[0]);
            printf("Maximum Per Subject: %" PRIi64 "\n",       // history depth
                   si->Config->MaxMsgsPerSubject);
            printf("Discard Policy: %s\n",                     // limits reject
                   (si->Config->Discard == js_DiscardNew) ? "New" : "Old");
            printf("Direct Get: %s\n",                         // get without a consumer
                   si->Config->AllowDirect ? "true" : "false");
            printf("Allows Rollups: %s\n",                     // purge = one marker
                   si->Config->AllowRollup ? "true" : "false");
            printf("Allows Msg Delete: %s\n",                  // deny_delete on
                   si->Config->DenyDelete ? "false" : "true");

            // One subject per key: widget-blue is the message on
            // $KV.INVENTORY.widget-blue.
            if (si->State.Subjects != NULL)
            {
                int i;

                for (i = 0; i < si->State.Subjects->Count; i++)
                    printf("  %s: %" PRIu64 " messages\n",
                           si->State.Subjects->List[i].Subject,
                           si->State.Subjects->List[i].Msgs);
            }
            jsStreamInfo_Destroy(si);
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
