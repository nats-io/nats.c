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

// Called for each async publish that failed: the server rejected it or no
// ack arrived in time. A failed publish is a lost order, so re-publish it
// (or report it) here.
static void
asyncPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    (void) js;
    (void) closure;
    printf("publish failed, re-publish it: %.*s (%s)\n",
           natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg),
           pae->ErrText);
}

int main(void)
{
    natsConnection  *conn = NULL;
    jsCtx           *js   = NULL;
    jsOptions       jsOpts;
    natsStatus      s;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
    {
        // Register the error callback for async publishes on the context.
        jsOptions_Init(&jsOpts);
        jsOpts.PublishAsync.ErrHandler = asyncPubErr;
        s = natsConnection_JetStream(&js, conn, &jsOpts);
    }

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Async publish: fire every order without waiting for its PubAck,
        // then wait for all acks at once. The round trips overlap, so this
        // is far faster than publishing one at a time -- but you still have
        // to confirm every ack, because a publish whose ack never arrives
        // is a lost order, not a stored one.
        const char *orders[] = {
            "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\",\"total_cents\":4200}",
            "{\"order_id\":\"ord_2zr9\",\"customer\":\"globex\",\"total_cents\":7800}",
            "{\"order_id\":\"ord_5t1m\",\"customer\":\"initech\",\"total_cents\":1500}",
            "{\"order_id\":\"ord_9p3x\",\"customer\":\"hooli\",\"total_cents\":9900}"};
        int         i;
        jsPubOptions wait;

        // js_PublishAsync returns immediately, before the server replies.
        // A failed ack is reported to the ErrHandler registered on the
        // JetStream context (see asyncPubErr above).
        for (i = 0; (s == NATS_OK) && (i < 4); i++)
            s = js_PublishAsync(js, "orders.created", orders[i],
                                (int) strlen(orders[i]), NULL);

        // Wait until every publish has been answered, or give up after
        // a timeout.
        jsPubOptions_Init(&wait);
        wait.MaxWait = 5000;
        if (s == NATS_OK)
            s = js_PublishAsyncComplete(js, &wait);
        if (s == NATS_TIMEOUT)
        {
            // Publishes still unconfirmed after the wait are lost writes:
            // take them back and re-publish or report each one.
            natsMsgList pending = {NULL, 0};

            if (js_PublishAsyncGetPendingList(&pending, js) == NATS_OK)
                printf("timed out with %d publishes unconfirmed\n", pending.Count);
            natsMsgList_Destroy(&pending);
        }
        else if (s == NATS_OK)
            printf("all 4 orders stored\n");
        // NATS-DOC-END
    }

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
