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
#include <inttypes.h>

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    jsErrCode           jerr  = 0;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Ask the consumer how many messages are still waiting, then read
        // exactly that many. This drains everything stored without assuming
        // a count.
        jsConsumerInfo  *ci     = NULL;
        uint64_t        pending = 0;

        s = js_GetConsumerInfo(&ci, js, "ORDERS", "billing", NULL, &jerr);
        if (s == NATS_OK)
        {
            pending = ci->NumPending;
            jsConsumerInfo_Destroy(ci);
        }
        if ((s == NATS_OK) && (pending == 0))
            printf("nothing to read\n");

        if ((s == NATS_OK) && (pending > 0))
        {
            // Bind to the durable consumer created earlier and fetch the
            // pending messages.
            jsSubOptions so;
            natsMsgList  list = {NULL, 0};

            jsSubOptions_Init(&so);
            so.Stream   = "ORDERS";
            so.Consumer = "billing";

            s = js_PullSubscribe(&sub, js, NULL, NULL, NULL, &so, &jerr);
            if (s == NATS_OK)
                s = natsSubscription_Fetch(&list, sub, (int) pending, 5000, &jerr);
            if (s == NATS_OK)
            {
                for (int i = 0; i < list.Count; i++)
                {
                    // Metadata carries the position of this message in the
                    // stream and in the consumer's own delivery sequence.
                    jsMsgMetaData *meta = NULL;

                    s = natsMsg_GetMetaData(&meta, list.Msgs[i]);
                    if (s != NATS_OK)
                        break;

                    printf("stream seq=%" PRIu64 " consumer seq=%" PRIu64 " payload=%s\n",
                           meta->Sequence.Stream, meta->Sequence.Consumer,
                           natsMsg_GetData(list.Msgs[i]));
                    jsMsgMetaData_Destroy(meta);

                    // Acknowledge so the server advances the consumer past
                    // this message.
                    natsMsg_Ack(list.Msgs[i], NULL);
                }
                natsMsgList_Destroy(&list);
            }
        }
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
