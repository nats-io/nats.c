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

static natsStatus
publishOrder(jsCtx *js, const char *subject, const char *payload)
{
    return js_Publish(NULL, js, subject, payload, (int) strlen(payload), NULL, NULL);
}

int main(void)
{
    natsConnection          *conn = NULL;
    jsCtx                   *js   = NULL;
    natsMsg                 *msg  = NULL;
    jsStreamConfig          sc;
    jsDirectGetMsgOptions   dgo;
    jsErrCode               jerr  = 0;
    natsStatus              s;
    const char              *url        = getenv("NATS_URL");
    const char              *subjects[] = {"orders.>"};

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: the ORDERS stream with AllowDirect on and a stored message.
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        sc.AllowDirect = true;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }
    if (s == NATS_OK)
        s = publishOrder(js, "orders.created", "{\"order_id\":\"ord_1yhz\",\"customer\":\"acme-co\",\"total_cents\":1250}");

    // NATS-DOC-START
    // Read the message at sequence 1. Because the stream has AllowDirect
    // enabled, the Direct Get API serves the read, so any replica can
    // answer it, not only the leader.
    if (s == NATS_OK)
    {
        jsDirectGetMsgOptions_Init(&dgo);
        dgo.Sequence = 1;
        s = js_DirectGetMsg(&msg, js, "ORDERS", NULL, &dgo);
    }

    if (s == NATS_OK)
    {
        printf("Subject: %s\n", natsMsg_GetSubject(msg));
        printf("Payload: %.*s\n", natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    }
    // NATS-DOC-END

    natsMsg_Destroy(msg);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }
    return 0;
}
