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
    natsConnection  *conn = NULL;
    jsCtx           *js   = NULL;
    jsConsumerInfo  *ci   = NULL;
    natsStatus      s;
    jsErrCode       jerr  = 0;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // A consumer acts on exactly one priority group, but PriorityGroups is
        // a list, so it's easy to pass two by accident.
        jsConsumerConfig    cc;
        const char          *twoGroups[] = {"regions", "backup"};
        const char          *oneGroup[]  = {"regions"};

        // NOT THIS: two group names. The server accepts the create without
        // error, but it uses only the first group (regions) and silently
        // ignores the rest. Multiple groups per consumer is reserved for a
        // future release.
        jsConsumerConfig_Init(&cc);
        cc.Durable = "dispatch";
        cc.AckPolicy = js_AckExplicit;
        cc.PriorityPolicy = "overflow";
        cc.PriorityGroups = twoGroups;
        cc.PriorityGroupsLen = 2;

        s = js_AddConsumer(&ci, js, "ORDERS", &cc, NULL, &jerr);
        if (s == NATS_OK)
        {
            jsConsumerInfo_Destroy(ci);
            ci = NULL;
            // The policy and groups are fixed at creation, so delete the
            // consumer before recreating it correctly.
            s = js_DeleteConsumer(js, "ORDERS", "dispatch", NULL, &jerr);
        }

        // DO THIS: name a single group. To split work by region or tier, run
        // separate consumers, each with its own group, on the same stream.
        if (s == NATS_OK)
        {
            cc.PriorityGroups = oneGroup;
            cc.PriorityGroupsLen = 1;
            s = js_AddConsumer(&ci, js, "ORDERS", &cc, NULL, &jerr);
        }
        if (s == NATS_OK)
            printf("dispatch uses group %s\n", ci->Config->PriorityGroups[0]);
        // NATS-DOC-END
    }

    jsConsumerInfo_Destroy(ci);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
