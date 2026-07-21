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
    jsStreamInfo        *si   = NULL;
    natsSubscription    *sub  = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Pitfall fix: advisories are transient, so capture them in a
        // stream that is always listening. This stream records every
        // advisory the cluster publishes and keeps a week of history.
        jsStreamConfig  cfg;
        const char      *subjects[] = {"$JS.EVENT.ADVISORY.>"};

        jsStreamConfig_Init(&cfg);
        cfg.Name        = "ADVISORIES";
        cfg.Subjects    = subjects;
        cfg.SubjectsLen = 1;
        cfg.Storage     = js_FileStorage;
        cfg.Retention   = js_LimitsPolicy;
        cfg.MaxAge      = 168LL * 60 * 60 * 1000000000LL; // 168h in ns

        s = js_AddStream(&si, js, &cfg, NULL, &jerr);

        // Now the max_deliver advisory for a poison order is durable.
        // Read back every advisory the stream has recorded, oldest
        // first, with an ephemeral no-ack consumer.
        if (s == NATS_OK)
        {
            jsSubOptions    so;
            natsMsgList     list = {NULL, 0};

            jsSubOptions_Init(&so);
            so.Stream           = "ADVISORIES";
            so.Config.AckPolicy = js_AckNone; // read-back only

            s = js_PullSubscribe(&sub, js, "$JS.EVENT.ADVISORY.>", NULL,
                                 NULL, &so, &jerr);
            if (s == NATS_OK)
                s = natsSubscription_Fetch(&list, sub, 100, 2000, &jerr);
            if ((s == NATS_OK) || (s == NATS_TIMEOUT))
            {
                int i;

                s = NATS_OK;
                for (i = 0; i < list.Count; i++)
                    printf("[%s] %s\n",
                           natsMsg_GetSubject(list.Msgs[i]),
                           natsMsg_GetData(list.Msgs[i]));
                natsMsgList_Destroy(&list);
            }
        }
        // NATS-DOC-END
    }

    natsSubscription_Destroy(sub);
    jsStreamInfo_Destroy(si);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
