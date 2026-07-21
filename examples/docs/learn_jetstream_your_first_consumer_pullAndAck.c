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
    natsSubscription    *sub  = NULL;
    natsMsgList         list  = {NULL, 0};
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Bind to the durable "shipping" consumer created earlier.
        jsSubOptions so;

        jsSubOptions_Init(&so);
        so.Stream   = "ORDERS";
        so.Consumer = "shipping";

        s = js_PullSubscribe(&sub, js, NULL, NULL, NULL, &so, &jerr);

        // Fetch a single message from the consumer.
        if (s == NATS_OK)
            s = natsSubscription_Fetch(&list, sub, 1, 5000, &jerr);
        if (s == NATS_OK)
        {
            natsMsg *msg = list.Msgs[0];

            printf("Received on %s: %.*s\n",
                   natsMsg_GetSubject(msg),
                   natsMsg_GetDataLength(msg),
                   natsMsg_GetData(msg));

            // Acknowledge so the server advances the consumer past this message.
            s = natsMsg_Ack(msg, NULL);
        }
        // NATS-DOC-END
    }

    natsMsgList_Destroy(&list);
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
