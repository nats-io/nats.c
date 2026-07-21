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

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsSubscription    *sub  = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Replay every message stored in the stream, oldest first. An
        // ordered consumer starts at the first stored message; the metadata
        // on each message tells us how many are still pending, so we stop
        // once we've read everything. Nothing is removed: this is a read.
        jsSubOptions so;

        jsSubOptions_Init(&so);
        so.Stream  = "EVENTS";
        so.Ordered = true;

        s = js_SubscribeSync(&sub, js, "events.>", NULL, &so, &jerr);

        while (s == NATS_OK)
        {
            natsMsg         *msg    = NULL;
            jsMsgMetaData   *meta   = NULL;
            uint64_t        pending = 0;

            s = natsSubscription_NextMsg(&msg, sub, 5000);
            if (s != NATS_OK)
                break;

            s = natsMsg_GetMetaData(&meta, msg);
            if (s == NATS_OK)
            {
                pending = meta->NumPending;
                printf("seq: %" PRIu64 " / subject: %s\n%.*s\n\n",
                       meta->Sequence.Stream, natsMsg_GetSubject(msg),
                       natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
                jsMsgMetaData_Destroy(meta);
            }
            natsMsg_Destroy(msg);

            // Exit on our own once everything stored has been read.
            if ((s == NATS_OK) && (pending == 0))
                break;
        }
        // An empty stream simply times out waiting for the first message.
        if (s == NATS_TIMEOUT)
            s = NATS_OK;
        // NATS-DOC-END
    }

    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
