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
    jsStreamInfo    *si   = NULL;
    natsStatus      s;
    jsErrCode       jerr  = 0;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Create ALL-ORDERS as an aggregate that sources the three regional
        // streams into one. Unlike a mirror, a stream can list several sources.
        jsStreamConfig  sc;
        jsStreamSource  us, eu, apac;
        jsStreamSource  *sources[] = {&us, &eu, &apac};

        jsStreamSource_Init(&us);
        us.Name = "ORDERS-US";
        jsStreamSource_Init(&eu);
        eu.Name = "ORDERS-EU";
        jsStreamSource_Init(&apac);
        apac.Name = "ORDERS-APAC";

        jsStreamConfig_Init(&sc);
        sc.Name = "ALL-ORDERS";
        sc.Sources = sources;
        sc.SourcesLen = 3;

        s = js_AddStream(&si, js, &sc, NULL, &jerr);
        if (s == NATS_OK)
            printf("Created %s sourcing %d streams\n",
                   si->Config->Name, si->Config->SourcesLen);
        // NATS-DOC-END
    }

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
