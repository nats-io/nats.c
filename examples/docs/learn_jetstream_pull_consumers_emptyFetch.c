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
        // Bind to the durable "shipping" consumer.
        jsSubOptions so;

        jsSubOptions_Init(&so);
        so.Stream   = "ORDERS";
        so.Consumer = "shipping";

        s = js_PullSubscribe(&sub, js, NULL, NULL, NULL, &so, &jerr);
        if (s == NATS_OK)
        {
            // A fetch on a drained consumer comes back empty with
            // NATS_TIMEOUT once the wait elapses. Treat "nothing right now"
            // as normal: if no orders came back, wait and fetch again
            // instead of failing.
            natsMsgList list = {NULL, 0};

            s = natsSubscription_Fetch(&list, sub, 10, 2000, &jerr);
            if ((s == NATS_OK) || (s == NATS_TIMEOUT))
            {
                for (int i = 0; i < list.Count; i++)
                {
                    printf("shipping %s\n", natsMsg_GetData(list.Msgs[i]));
                    natsMsg_Ack(list.Msgs[i], NULL);
                }
                if (list.Count == 0)
                {
                    printf("no orders waiting, will retry\n");
                    nats_Sleep(1000);
                }
                natsMsgList_Destroy(&list);
                s = NATS_OK;
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
