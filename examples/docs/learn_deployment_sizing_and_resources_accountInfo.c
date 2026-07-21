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
    jsAccountInfo       *ai   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn,
            (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // NATS-DOC-START
    // Read the LIVE account limits for the ORDERS account before you
    // size anything. This is the source of truth: the ceilings the
    // server will actually enforce, not what you hope the config says.
    if (s == NATS_OK)
        s = js_GetAccountInfo(&ai, js, NULL, &jerr);
    if (s == NATS_OK)
    {
        // Current usage, then the ceilings. A value of -1 means
        // unlimited.
        printf("memory used:    %" PRIu64 " bytes\n", ai->Memory);
        printf("storage used:   %" PRIu64 " bytes\n", ai->Store);
        printf("streams:        %" PRId64 "\n", ai->Streams);
        printf("consumers:      %" PRId64 "\n", ai->Consumers);
        printf("max memory:     %" PRId64 "\n", ai->Limits.MaxMemory);
        printf("max storage:    %" PRId64 "\n", ai->Limits.MaxStore);
        printf("max streams:    %" PRId64 "\n", ai->Limits.MaxStreams);
        printf("max consumers:  %" PRId64 "\n", ai->Limits.MaxConsumers);

        // Sizing rule: on an UN-tiered account an R3 stream counts as
        // replicas x bytes against max storage, so a 10 GiB ORDERS
        // stream at R3 spends 30 GiB of the account's limit. A tiered
        // account bakes replication in, so its number is usable bytes.
        if (ai->TiersLen > 0)
            printf("tiered account: %d tier(s), limits are per tier\n",
                   ai->TiersLen);
    }
    // NATS-DOC-END

    jsAccountInfo_Destroy(ai);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
