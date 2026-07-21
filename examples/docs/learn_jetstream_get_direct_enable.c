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
    natsConnection      *conn    = NULL;
    jsCtx               *js      = NULL;
    jsStreamInfo        *si      = NULL;
    jsStreamInfo        *updated = NULL;
    jsStreamConfig      sc;
    jsErrCode           jerr = 0;
    natsStatus          s;
    const char          *url        = getenv("NATS_URL");
    const char          *subjects[] = {"orders.>"};

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    // Setup: the ORDERS stream, created without direct access.
    if (s == NATS_OK)
    {
        jsStreamConfig_Init(&sc);
        sc.Name        = "ORDERS";
        sc.Subjects    = subjects;
        sc.SubjectsLen = 1;
        s = js_AddStream(NULL, js, &sc, NULL, &jerr);
    }

    // NATS-DOC-START
    // Read the current config, turn on direct access, and push the update.
    // AllowDirect lets any replica serve single-message gets.
    if (s == NATS_OK)
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);

    if (s == NATS_OK)
    {
        si->Config->AllowDirect = true;
        s = js_UpdateStream(&updated, js, si->Config, NULL, &jerr);
    }

    if (s == NATS_OK)
        printf("AllowDirect: %s\n", updated->Config->AllowDirect ? "true" : "false");
    // NATS-DOC-END

    jsStreamInfo_Destroy(si);
    jsStreamInfo_Destroy(updated);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }
    return 0;
}
