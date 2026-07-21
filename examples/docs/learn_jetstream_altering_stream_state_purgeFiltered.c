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
    natsStatus      s;
    jsErrCode       jerr  = 0;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // A bare purge removes everything. Three optional purge options
        // narrow it.
        jsOptions o;

        // Remove only the shipped events, leave everything else.
        jsOptions_Init(&o);
        o.Stream.Purge.Subject = "orders.shipped";
        s = js_PurgeStream(js, "ORDERS", &o, &jerr);

        // Remove everything up to but not including sequence 100
        // (keep 100 onward).
        jsOptions_Init(&o);
        o.Stream.Purge.Sequence = 100;
        if (s == NATS_OK)
            s = js_PurgeStream(js, "ORDERS", &o, &jerr);

        // Keep only the most recent 50 messages.
        jsOptions_Init(&o);
        o.Stream.Purge.Keep = 50;
        if (s == NATS_OK)
            s = js_PurgeStream(js, "ORDERS", &o, &jerr);
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
