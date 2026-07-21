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

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfoList    *list = NULL;
    jsStreamInfo        *si   = NULL;
    jsPubAck            *ack  = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    int                 i;
    const char          *url  = getenv("NATS_URL");
    const char          *order =
        "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
        "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

    s = natsConnection_ConnectTo(&conn,
            (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // NATS-DOC-START
    // List every stream the cluster holds. After the Stream CRD
    // reconciles, ORDERS appears here even though no human ran
    // `nats stream add` -- the NACK controller created it from the
    // declarative resource.
    if (s == NATS_OK)
        s = js_Streams(&list, js, NULL, &jerr);
    if (s == NATS_OK)
    {
        for (i = 0; i < list->Count; i++)
            printf("stream: %s\n", list->List[i]->Config->Name);
    }

    // Read ORDERS back in full. The number that matters for a
    // CRD-created stream is Replicas: 3 -- proof the controller
    // honoured `replicas: 3` from the CRD spec and placed a copy on
    // each of nats-0, nats-1, nats-2.
    if (s == NATS_OK)
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
    if (s == NATS_OK)
    {
        printf("ORDERS replicas: %" PRId64 "\n", si->Config->Replicas);
        if (si->Cluster != NULL)
            printf("ORDERS leader:   %s\n", si->Cluster->Leader);
    }

    // Publish one order to prove the CRD-created stream actually
    // accepts writes across all three replicas.
    if (s == NATS_OK)
        s = js_Publish(&ack, js, "orders.created",
                       order, (int) strlen(order), NULL, &jerr);
    if (s == NATS_OK)
        printf("stored in %s, sequence %" PRIu64 "\n",
               ack->Stream, ack->Sequence);
    // NATS-DOC-END

    jsPubAck_Destroy(ack);
    jsStreamInfo_Destroy(si);
    jsStreamInfoList_Destroy(list);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
