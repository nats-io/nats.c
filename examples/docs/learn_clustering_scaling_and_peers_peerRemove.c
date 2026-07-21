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

// Move a replica off one server, safely, then verify the new peer set
// before trusting the change.
//
// This assumes the east cluster (n1-east, n2-east, n3-east, plus a
// fourth server n4-east) is running and ORDERS holds a replica on
// n4-east. peer-remove does NOT shrink the stream: it evicts the replica
// from n4-east and the meta leader re-places it on another qualifying
// server, so the replica count stays the same. Make ONE change at a time
// and wait for a leader and a caught-up replacement before the next —
// stacking changes can drop the peers holding the data below a majority
// and the stream stops committing.

static void printCluster(jsStreamInfo *si)
{
    int i;

    if (si->Cluster == NULL)
        return;
    printf("Leader: %s\n",
           (si->Cluster->Leader != NULL) ? si->Cluster->Leader : "(no leader)");
    for (i = 0; i < si->Cluster->ReplicasLen; i++)
    {
        jsPeerInfo *peer = si->Cluster->Replicas[i];

        printf("Replica: %s, %s, lag %" PRIu64 "\n",
               peer->Name,
               peer->Current ? "current" : "outdated",
               peer->Lag);
    }
}

int main(void)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfo        *si   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // First, read the current peer set. Confirm there is a leader and
        // that every replica's lag is 0 before you change anything — a
        // peer mid catchup is not safe to lean on.
        natsMsg *reply = NULL;

        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
        {
            printCluster(si);
            jsStreamInfo_Destroy(si);
            si = NULL;
        }

        // Evict the replica from one server by name. nats.c has no
        // peer-remove helper, so send the JetStream API request directly —
        // the same request the CLI's `stream cluster peer-remove` sends.
        // The meta leader picks a replacement server and updates the
        // stream assignment; n4-east drops its RAFT subscriptions. If no
        // other server qualifies, an R>1 stream is still left a peer
        // short: the reply carries "peer remap failed" (only a
        // single-replica stream is refused outright).
        // (To change the replica COUNT, update the stream's Replicas
        // field with js_UpdateStream instead.)
        if (s == NATS_OK)
            s = natsConnection_RequestString(&reply, conn,
                    "$JS.API.STREAM.PEER.REMOVE.ORDERS",
                    "{\"peer\":\"n4-east\"}", 5000);
        if (s == NATS_OK)
        {
            printf("%.*s\n",
                   natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
            natsMsg_Destroy(reply);
        }

        // Verify. Re-read the cluster info and confirm three things:
        //   - n4-east is gone from the replicas list,
        //   - there is still a named leader,
        //   - the replacement replica is catching up (and reaches lag 0).
        //
        // Only when a leader is back and the replacement is current is it
        // safe to make the next change. If the stream shows no leader,
        // stop — you have lost quorum and must restore a peer, not make
        // another change.
        if (s == NATS_OK)
            s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
            printCluster(si);
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
