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
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    natsMsg             *msg  = NULL;
    jsStreamConfig      sc;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url        = getenv("NATS_URL");
    const char          *subjects[] = {"orders.>"};

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: the ORDERS stream with three stored messages.
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }
    if (s == NATS_OK)
        s = publishOrder(js, "orders.created", "{\"order_id\":\"ord_1yhz\",\"customer\":\"acme-co\",\"total_cents\":1250}");
    if (s == NATS_OK)
        s = publishOrder(js, "orders.created", "{\"order_id\":\"ord_2zr9\",\"customer\":\"globex\",\"total_cents\":7800}");
    if (s == NATS_OK)
        s = publishOrder(js, "orders.shipped", "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\",\"total_cents\":4200}");

    // NATS-DOC-START
    // Read the message stored at stream sequence 2. This is the regular get,
    // served by the stream leader.
    if (s == NATS_OK)
        s = js_GetMsg(&msg, js, "ORDERS", 2, NULL, &jerr);

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
