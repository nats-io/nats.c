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

// Place the ORDERS stream on servers carrying specific tags.
//
// This assumes the 3-node "east" cluster is running with JetStream
// enabled, and that n1-east, n2-east, n3-east each advertise the tags
// region:us-east and disk:ssd via server_tags in their config.

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
        // Create ORDERS at R=3, constrained to servers carrying BOTH tags.
        // Placement.Tags is a list and the match is an intersection: every
        // listed tag must be present on a server for it to qualify. Tag
        // matching folds case (ssd == SSD) but spelling is exact.
        //
        // Placement.Cluster names the cluster every replica must live in.
        // In a single cluster like "east" it is a no-op (there is only one
        // cluster), but it is shown here so the syntax is familiar when you
        // place across clusters later. The match is an intersection of
        // cluster AND tags.
        jsStreamConfig  cfg;
        jsPlacement     placement;
        const char      *subjects[] = {"orders.>"};
        const char      *tags[]     = {"region:us-east", "disk:ssd"};

        jsPlacement_Init(&placement);
        placement.Cluster = "east";
        placement.Tags    = tags;
        placement.TagsLen = 2;

        jsStreamConfig_Init(&cfg);
        cfg.Name        = "ORDERS";
        cfg.Subjects    = subjects;
        cfg.SubjectsLen = 1;
        cfg.Replicas    = 3;
        cfg.Placement   = &placement;

        s = js_AddStream(&si, js, &cfg, NULL, &jerr);
        if (jerr == JSStreamNameExistErr)
        {
            // ORDERS already exists: change its placement instead of
            // recreating it. The same config applies on update; the meta
            // leader re-assigns the replicas to servers matching the new
            // constraint.
            s = js_UpdateStream(&si, js, &cfg, NULL, &jerr);
        }

        // Read the result. The Cluster block names the leader and the two
        // other peers — every one of them is a server you tagged.
        if ((s == NATS_OK) && (si->Cluster != NULL))
        {
            printf("Cluster: %s, leader: %s\n",
                   si->Cluster->Name, si->Cluster->Leader);
            for (i = 0; i < si->Cluster->ReplicasLen; i++)
                printf("Replica: %s\n", si->Cluster->Replicas[i]->Name);
        }

        // Publish the canonical order to confirm the placed stream accepts
        // writes exactly as before — placement changes where, not what.
        if (s == NATS_OK)
            s = natsConnection_PublishString(conn, "orders.created",
                    "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                    "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
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
