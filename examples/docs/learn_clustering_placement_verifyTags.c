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

// Verify server tags BEFORE placing a stream, then place against exactly
// what the servers advertise — so a typo fails loudly instead of silently
// placing nowhere.
//
// This assumes the 3-node "east" cluster is running with JetStream
// enabled. Reading server info goes through the system account ($SYS),
// so that request needs a connection with a system user's credentials:
// set NATS_SYS_URL to a URL that carries them (it falls back to NATS_URL).

int main(void)
{
    natsConnection      *conn    = NULL;
    natsConnection      *sysConn = NULL;
    jsCtx               *js      = NULL;
    jsStreamInfo        *si      = NULL;
    jsErrCode           jerr     = 0;
    natsStatus          s;
    int                 i;
    const char          *url     = getenv("NATS_URL");
    const char          *sysUrl  = getenv("NATS_SYS_URL");

    if (url == NULL)
        url = NATS_DEFAULT_URL;
    if (sysUrl == NULL)
        sysUrl = url;

    // One connection for the application account (JetStream), one for
    // the system account ($SYS requests).
    s = natsConnection_ConnectTo(&conn, url);
    if (s == NATS_OK)
        s = natsConnection_ConnectTo(&sysConn, sysUrl);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Read the tags each server actually advertises. Do not assume the
        // config took — a typo in server_tags is silent until a placement
        // asks for a tag no server carries. There is no dedicated client
        // API for this: it is a plain request on the system account's
        // $SYS.REQ.SERVER.PING.VARZ subject, filtered by server name. The
        // "tags" field of the reply is the source of truth.
        const char *names[] = {"n1-east", "n2-east", "n3-east"};

        for (i = 0; (s == NATS_OK) && (i < 3); i++)
        {
            natsMsg     *reply = NULL;
            char        req[64];

            snprintf(req, sizeof(req), "{\"server_name\":\"%s\"}", names[i]);
            s = natsConnection_RequestString(&reply, sysConn,
                    "$SYS.REQ.SERVER.PING.VARZ", req, 2000);
            if (s == NATS_OK)
            {
                // Print just the "tags" list out of the VARZ reply.
                const char *tags = strstr(natsMsg_GetData(reply), "\"tags\":");
                const char *end  = (tags != NULL) ? strchr(tags, ']') : NULL;

                if (end != NULL)
                    printf("%s %.*s]\n", names[i], (int)(end - tags), tags);
                natsMsg_Destroy(reply);
            }
        }

        // Now place ORDERS against exactly the tags you just read back.
        // If a requested tag is misspelled or missing on every server, the
        // intersection is empty and the create fails — it does NOT fall
        // back to any server. The error names the tag no server carried:
        //
        //   no suitable peers for placement, tags not matched ['disk:sdd']
        //   (error code 10005)
        if (s == NATS_OK)
        {
            jsStreamConfig  cfg;
            jsPlacement     placement;
            const char      *subjects[] = {"orders.>"};
            const char      *tags[]     = {"region:us-east", "disk:ssd"};

            jsPlacement_Init(&placement);
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
                s = js_UpdateStream(&si, js, &cfg, NULL, &jerr);
        }

        // Confirm the placement landed where you intended: the leader and
        // both replicas are servers that advertised the tags.
        if ((s == NATS_OK) && (si->Cluster != NULL))
        {
            printf("Leader: %s\n", si->Cluster->Leader);
            for (i = 0; i < si->Cluster->ReplicasLen; i++)
                printf("Replica: %s\n", si->Cluster->Replicas[i]->Name);
        }
        // NATS-DOC-END
    }

    jsStreamInfo_Destroy(si);
    jsCtx_Destroy(js);
    natsConnection_Destroy(sysConn);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
