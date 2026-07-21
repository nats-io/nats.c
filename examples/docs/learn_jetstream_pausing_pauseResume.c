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
        // Pause the shipping consumer for one hour. The server stores a fixed
        // deadline: now plus one hour, in nanoseconds since the epoch.
        jsConsumerPauseResponse *resp     = NULL;
        uint64_t                deadline  = (uint64_t) nats_NowInNanoSeconds()
                                            + 3600ULL * 1000000000ULL;

        s = js_PauseConsumer(&resp, js, "ORDERS", "shipping", deadline, NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("Paused: %s, remaining: %.0fs\n",
                   resp->Paused ? "true" : "false",
                   (double) resp->PauseRemaining / 1E9);
            jsConsumerPauseResponse_Destroy(resp);
            resp = NULL;
        }

        // Resume early, before the deadline: a pause request with no deadline
        // lifts the pause. Delivery picks up at the same cursor position the
        // consumer held when it was paused.
        if (s == NATS_OK)
            s = js_PauseConsumer(&resp, js, "ORDERS", "shipping", 0, NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("Paused: %s\n", resp->Paused ? "true" : "false");
            jsConsumerPauseResponse_Destroy(resp);
        }
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
