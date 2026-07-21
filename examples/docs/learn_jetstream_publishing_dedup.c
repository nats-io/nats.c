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
#include <string.h>
#include <inttypes.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsStatus          s;
    jsErrCode           jerr  = 0;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Publish the same message twice with a message ID. The stream uses
        // the ID to detect duplicates within its dedupe window.
        jsPubAck        *first   = NULL;
        jsPubAck        *second  = NULL;
        jsPubOptions    po;
        const char      *payload = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\",\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

        jsPubOptions_Init(&po);
        po.MsgId = "ord_8w2k-created";

        s = js_Publish(&first, js, "orders.created",
                       payload, (int) strlen(payload),
                       &po, &jerr);
        if (s == NATS_OK)
        {
            printf("First:  sequence %" PRIu64 ", duplicate %s\n",
                   first->Sequence, first->Duplicate ? "true" : "false");
            jsPubAck_Destroy(first);

            // Re-publish with the same ID. The server recognizes it and
            // reports the original sequence instead of storing the message
            // again.
            s = js_Publish(&second, js, "orders.created",
                           payload, (int) strlen(payload),
                           &po, &jerr);
        }
        if (s == NATS_OK)
        {
            printf("Second: sequence %" PRIu64 ", duplicate %s\n",
                   second->Sequence, second->Duplicate ? "true" : "false");
            jsPubAck_Destroy(second);
        }
        // NATS-DOC-END
    }

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
