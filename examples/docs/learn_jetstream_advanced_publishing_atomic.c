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
#include <inttypes.h>

static natsStatus
newItem(natsMsg **msg, const char *json)
{
    return natsMsg_Create(msg, "orders.created", NULL, json, (int) strlen(json));
}

int main(void)
{
    natsConnection      *conn  = NULL;
    jsCtx               *js    = NULL;
    jsAtomicBatchCtx    *batch = NULL;
    natsMsg             *msg1  = NULL;
    natsMsg             *msg2  = NULL;
    natsMsg             *msg3  = NULL;
    natsStatus          s;
    jsErrCode           jerr   = 0;
    const char          *url   = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // The three line items of order ORD-42, each as its own message.
    if (s == NATS_OK)
        s = newItem(&msg1, "{\"order\":\"ORD-42\",\"sku\":\"COFFEE-1KG\",\"qty\":2}");
    if (s == NATS_OK)
        s = newItem(&msg2, "{\"order\":\"ORD-42\",\"sku\":\"FILTER-100\",\"qty\":1}");
    if (s == NATS_OK)
        s = newItem(&msg3, "{\"order\":\"ORD-42\",\"sku\":\"MUG-WHITE\",\"qty\":4}");

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Publish the three line items of order ORD-42 as one atomic batch:
        // all three land in the ORDERS stream, or none do.
        jsPubAck *ack = NULL;

        // Start the batch with the first line item.
        s = js_BatchPublishStart(&batch, NULL, js, msg1, NULL, &jerr);

        // Add the second line item to the batch.
        if (s == NATS_OK)
            s = js_BatchPublishAdd(NULL, batch, msg2, NULL, &jerr);

        // Commit sends the final line item and atomically commits the
        // batch. The ack carries the batch ID and how many messages the
        // batch stored.
        if (s == NATS_OK)
            s = js_BatchPublishCommit(&ack, batch, msg3, NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("batch \"%s\" committed: %" PRIu64 " line items stored\n",
                   ack->Batch, ack->Count);
            jsPubAck_Destroy(ack);
        }
        jsAtomicBatchCtx_Destroy(batch);
        // NATS-DOC-END
    }

    natsMsg_Destroy(msg1);
    natsMsg_Destroy(msg2);
    natsMsg_Destroy(msg3);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
