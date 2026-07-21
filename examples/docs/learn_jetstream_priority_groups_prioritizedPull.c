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
        // Bind to the existing prioritized consumer. Every pull names the
        // group and carries a priority from 0 to 9; the server serves the
        // lowest number present first.
        jsSubOptions so;

        jsSubOptions_Init(&so);
        so.Stream = "ORDERS";
        so.Consumer = "dispatch";

        s = js_PullSubscribe(&sub, js, NULL, "dispatch", NULL, &so, &jerr);

        // us-east pulls at priority 0, so it's served ahead of any
        // higher-numbered pull.
        if (s == NATS_OK)
        {
            jsFetchRequest  req;
            natsMsgList     list = {NULL, 0};

            jsFetchRequest_Init(&req);
            req.Batch = 5;
            req.Expires = 2 * 1000000000LL; // 2 seconds, in nanoseconds
            req.Group = "regions";
            req.Priority = 0;

            s = natsSubscription_FetchRequest(&list, sub, &req);
            if (s == NATS_OK)
            {
                printf("us-east got %d messages\n", list.Count);
                for (i = 0; i < list.Count; i++)
                    natsMsg_Ack(list.Msgs[i], NULL);
                natsMsgList_Destroy(&list);
            }
            else if (s == NATS_TIMEOUT)
                s = NATS_OK; // nothing waiting right now
        }

        // us-west pulls the same way at priority 1: it's served the moment no
        // priority-0 pull is waiting, with no threshold and no delay.
        if (s == NATS_OK)
        {
            jsFetchRequest  req;
            natsMsgList     list = {NULL, 0};

            jsFetchRequest_Init(&req);
            req.Batch = 5;
            req.Expires = 2 * 1000000000LL;
            req.Group = "regions";
            req.Priority = 1;

            s = natsSubscription_FetchRequest(&list, sub, &req);
            if (s == NATS_OK)
            {
                printf("us-west got %d messages\n", list.Count);
                for (i = 0; i < list.Count; i++)
                    natsMsg_Ack(list.Msgs[i], NULL);
                natsMsgList_Destroy(&list);
            }
            else if (s == NATS_TIMEOUT)
                s = NATS_OK;
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
