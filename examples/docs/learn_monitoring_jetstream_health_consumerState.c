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
    jsConsumerInfo      *ci   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Read the live state of the shipping consumer on the ORDERS
        // stream. The state numbers tell you lag, in-flight, and
        // redelivery at a glance.
        s = js_GetConsumerInfo(&ci, js, "ORDERS", "shipping", NULL, &jerr);
        if (s == NATS_OK)
        {
            // NumPending is the lag: stored orders shipping has not
            // taken yet. NumAckPending is in-flight, not lag. A rising
            // NumPending with NumWaiting at 0 and Delivered.Stream
            // stalled means no worker is fetching.
            printf("Last Delivered: consumer=%" PRIu64 " stream=%" PRIu64 "\n",
                   ci->Delivered.Consumer, ci->Delivered.Stream);
            printf("Ack Floor:      consumer=%" PRIu64 " stream=%" PRIu64 "\n",
                   ci->AckFloor.Consumer, ci->AckFloor.Stream);
            printf("Outstanding Acks:     %" PRId64 "\n", ci->NumAckPending);
            printf("Redelivered Messages: %" PRId64 "\n", ci->NumRedelivered);
            printf("Waiting Pulls:        %" PRId64 "\n", ci->NumWaiting);
            printf("Unprocessed Messages: %" PRIu64 "\n", ci->NumPending);
        }
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
