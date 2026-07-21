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

// Read the Cluster block of the ORDERS stream info to check that every
// copy is current before trusting the stream. This is the handling step
// for the "a replica may lag" pitfall: do not assume all three copies
// hold the same data — read the lag and confirm it.
//
// Assumes the `east` cluster is running and ORDERS is an R=3 stream (see
// the createR3 example). Point NATS_URL at any server in the cluster.

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
        // Ask the cluster who leads ORDERS and how its followers are
        // doing. The Cluster info names the leader (every write lands
        // there) and lists each replica with its status.
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if ((s == NATS_OK) && (si->Cluster != NULL))
        {
            bool allCurrent = true;

            printf("Cluster: %s, leader: %s\n",
                   si->Cluster->Name, si->Cluster->Leader);

            for (i = 0; i < si->Cluster->ReplicasLen; i++)
            {
                jsPeerInfo *peer = si->Cluster->Replicas[i];

                // Current means the follower has applied every committed
                // entry — it is up to date with the leader. A follower
                // that is catching up shows Current false and a non-zero
                // Lag (in operations) instead. Active is how long ago the
                // leader last heard from it.
                printf("Replica: %s, %s, seen %.2fs ago",
                       peer->Name,
                       peer->Current ? "current" : "outdated",
                       (double) peer->Active / 1E9);
                if (peer->Lag > 0)
                    printf(", %" PRIu64 " operations behind", peer->Lag);
                printf("\n");

                if (!peer->Current)
                    allCurrent = false;
            }

            // A healthy R=3 stream shows every replica current with a
            // small seen age. If a replica is outdated, do not treat it as
            // a current copy: read from the leader for read-after-write,
            // and wait for current before trusting that copy to survive a
            // node loss. A persistent lag points at a slow disk or a
            // saturated route between peers.
            if (allCurrent)
                printf("All replicas current\n");
            else
                printf("A replica is catching up — wait before leaning on it\n");
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
