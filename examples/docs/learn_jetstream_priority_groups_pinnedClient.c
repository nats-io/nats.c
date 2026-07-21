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

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("processed %.*s\n", natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    natsMsg_Ack(msg, NULL);
    natsMsg_Destroy(msg);
}

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
        // Pull from the pinned consumer with the async helper. The library
        // runs the pin handshake for you: it stores the Nats-Pin-Id header
        // from the first delivered message, sends it back on every later pull,
        // and clears it to rejoin the standby pool if the server unpins this
        // client.
        jsOptions       jsOpts;
        jsSubOptions    so;

        jsOptions_Init(&jsOpts);
        jsOpts.PullSubscribeAsync.Group = "ordered"; // the consumer's priority group
        jsOpts.PullSubscribeAsync.Timeout = 30000;   // stop after 30 seconds

        jsSubOptions_Init(&so);
        so.Stream = "ORDERS";
        so.Consumer = "sequencer";
        so.ManualAck = true; // pinned_client requires explicit acks

        s = js_PullSubscribeAsync(&sub, js, NULL, "sequencer",
                                  onMsg, NULL, &jsOpts, &so, &jerr);

        // If the server pins this client, every message flows here; if a
        // standby client runs the same code, its pulls wait until the pin
        // moves to it.
        while ((s == NATS_OK) && natsSubscription_IsValid(sub))
            nats_Sleep(100);
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
