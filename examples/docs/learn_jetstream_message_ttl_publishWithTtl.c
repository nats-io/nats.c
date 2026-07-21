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
#include <string.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsPubAck            *ack  = NULL;
    jsStreamConfig      sc;
    jsPubOptions        po;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url        = getenv("NATS_URL");
    const char          *subjects[] = {"orders.>"};
    const char          *payload    = "order 42 cancelled";

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: the ORDERS stream with per-message TTLs allowed.
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        sc.AllowMsgTTL = true;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }

    // NATS-DOC-START
    // Publish with a 60-second TTL. MsgTTL sets the Nats-TTL header, so the
    // server deletes this single message 60s after it is stored, ahead of
    // the stream's MaxAge.
    if (s == NATS_OK)
    {
        jsPubOptions_Init(&po);
        po.MsgTTL = 60000; // in milliseconds

        s = js_Publish(&ack, js, "orders.cancelled", payload, (int) strlen(payload), &po, &jerr);
    }

    if (s == NATS_OK)
        printf("Stored in %s at sequence %" PRIu64 "\n", ack->Stream, ack->Sequence);
    // NATS-DOC-END

    jsPubAck_Destroy(ack);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }
    return 0;
}
