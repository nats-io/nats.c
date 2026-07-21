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

// Create the ORDERS stream as an R=3 stream on the `east` cluster, then
// publish one order and read back which server leads the stream and which
// servers hold the copies.
//
// This assumes the three `east` servers from the Topologies chapter are
// running, each with JetStream enabled. Point NATS_URL at any server in
// the cluster (a comma-separated list works too, e.g.
// nats://127.0.0.1:4222,nats://127.0.0.1:4223,nats://127.0.0.1:4224);
// the cluster routes each request to the stream leader wherever it
// currently lives.

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfo        *si   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    int                 i;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Create ORDERS with three replicas. Replicas=3 is the only line
        // that differs from the single-server create in the JetStream
        // chapter. If ORDERS already exists at R=1, raise it in place with
        // js_UpdateStream instead.
        jsStreamConfig  cfg;
        const char      *subjects[] = {"orders.>"};
        jsPubAck        *pa         = NULL;

        jsStreamConfig_Init(&cfg);
        cfg.Name        = "ORDERS";
        cfg.Subjects    = subjects;
        cfg.SubjectsLen = 1;
        cfg.Replicas    = 3;

        s = js_AddStream(NULL, js, &cfg, NULL, &jerr);
        if (jerr == JSStreamNameExistErr)
            s = js_UpdateStream(NULL, js, &cfg, NULL, &jerr);

        // Publish one order as order-svc would. A JetStream publish waits
        // for a PubAck; the PubAck returns only after the leader has the
        // write committed to a quorum (itself plus one follower). A core
        // publish (natsConnection_Publish) would not wait for one.
        const char *order =
            "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
            "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";

        if (s == NATS_OK)
            s = js_Publish(&pa, js, "orders.created",
                           order, (int) strlen(order), NULL, &jerr);
        if (s == NATS_OK)
        {
            printf("Stored in %s, sequence %" PRIu64 "\n",
                   pa->Stream, pa->Sequence);
            jsPubAck_Destroy(pa);
        }

        // Confirm the replica count and the cluster layout that R=3
        // produced: one leader and two follower replicas, each on its own
        // server of the east cluster.
        if (s == NATS_OK)
            s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if ((s == NATS_OK) && (si->Cluster != NULL))
        {
            printf("Cluster: %s, leader: %s\n",
                   si->Cluster->Name, si->Cluster->Leader);
            for (i = 0; i < si->Cluster->ReplicasLen; i++)
            {
                jsPeerInfo *peer = si->Cluster->Replicas[i];

                printf("Replica: %s, %s, seen %.2fs ago\n",
                       peer->Name,
                       peer->Current ? "current" : "outdated",
                       (double) peer->Active / 1E9);
            }
        }
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
