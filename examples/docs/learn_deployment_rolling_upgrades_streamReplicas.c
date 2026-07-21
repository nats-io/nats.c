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

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    jsCtx               *js   = NULL;
    jsStreamInfo        *si   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    int                 i;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn,
            (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // NATS-DOC-START
    // Read the R3 ORDERS stream's replicas and current leader. Run
    // this BEFORE you start the upgrade, and again AFTER each node
    // rejoins, to confirm the stream stayed at 3 replicas and the
    // leader moved as expected.
    if (s == NATS_OK)
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
    if (s == NATS_OK)
    {
        printf("replicas: %" PRId64 "\n", si->Config->Replicas);
        if (si->Cluster != NULL)
        {
            printf("leader:   %s\n", si->Cluster->Leader);
            for (i = 0; i < si->Cluster->ReplicasLen; i++)
            {
                jsPeerInfo *peer = si->Cluster->Replicas[i];

                // Upgrade rule: every replica must read "current"
                // before you take the next node down. A lagging
                // replica is still catching up, so pausing on it
                // keeps the stream at full R3 the whole way through.
                printf("replica:  %s, %s, lag %" PRIu64 "\n",
                       peer->Name,
                       peer->Current ? "current" : "outdated",
                       peer->Lag);
            }
        }
    }
    // NATS-DOC-END

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
