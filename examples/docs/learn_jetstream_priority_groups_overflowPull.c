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
    natsStatus          s;
    jsErrCode           jerr  = 0;
    int                 i;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Bind to the existing overflow consumer. Every pull on a
        // priority-group consumer must name its group.
        jsSubOptions so;

        jsSubOptions_Init(&so);
        so.Stream = "ORDERS";
        so.Consumer = "dispatch";

        s = js_PullSubscribe(&sub, js, NULL, "dispatch", NULL, &so, &jerr);

        // A near-region worker pulls with no threshold, so it always gets
        // messages.
        if (s == NATS_OK)
        {
            jsFetchRequest  req;
            natsMsgList     list = {NULL, 0};

            jsFetchRequest_Init(&req);
            req.Batch = 5;
            req.Expires = 2 * 1000000000LL; // 2 seconds, in nanoseconds
            req.Group = "regions";

            s = natsSubscription_FetchRequest(&list, sub, &req);
            if (s == NATS_OK)
            {
                printf("near region got %d messages\n", list.Count);
                for (i = 0; i < list.Count; i++)
                    natsMsg_Ack(list.Msgs[i], NULL);
                natsMsgList_Destroy(&list);
            }
            else if (s == NATS_TIMEOUT)
                s = NATS_OK; // nothing waiting right now
        }

        // A far-region worker adds MinPending: the server answers its pull
        // only when the consumer has backed up past that many waiting
        // messages. Below the threshold it gets nothing, the same as if the
        // stream were empty.
        if (s == NATS_OK)
        {
            jsFetchRequest  req;
            natsMsgList     list = {NULL, 0};

            jsFetchRequest_Init(&req);
            req.Batch = 5;
            req.Expires = 2 * 1000000000LL;
            req.Group = "regions";
            req.MinPending = 100;

            s = natsSubscription_FetchRequest(&list, sub, &req);
            if (s == NATS_OK)
            {
                printf("far region got %d overflow messages\n", list.Count);
                for (i = 0; i < list.Count; i++)
                    natsMsg_Ack(list.Msgs[i], NULL);
                natsMsgList_Destroy(&list);
            }
            else if (s == NATS_TIMEOUT)
            {
                // Backlog below 100: the far region stays idle.
                printf("far region got nothing\n");
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
