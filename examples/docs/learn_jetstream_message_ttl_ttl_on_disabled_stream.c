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
    jsStreamConfig      sc;
    jsPubOptions        po;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url        = getenv("NATS_URL");
    const char          *subjects[] = {"no-ttl.>"};
    const char          *payload    = "order 42 cancelled";

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: a stream that has NOT opted in to per-message TTLs
    // (AllowMsgTTL is left false).
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "NO-TTL";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }

    // NATS-DOC-START
    // Publishing with a TTL to a stream that hasn't enabled AllowMsgTTL
    // fails. The server returns "per-message TTL is disabled" (err_code
    // 10166) and stores nothing. The fix is to opt the stream in with
    // AllowMsgTTL: true.
    if (s == NATS_OK)
    {
        jsPubOptions_Init(&po);
        po.MsgTTL = 60000; // in milliseconds

        s = js_Publish(NULL, js, "no-ttl.msg", payload, (int) strlen(payload), &po, &jerr);
        if ((s != NATS_OK) && (jerr == JSMessageTTLDisabledErr))
        {
            printf("Publish rejected, message not stored: %s (err_code %d)\n",
                   nats_GetLastError(NULL), (int) jerr);
            s = NATS_OK;
        }
    }
    // NATS-DOC-END

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }
    return 0;
}
