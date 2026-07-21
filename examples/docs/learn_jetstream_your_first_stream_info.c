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
    jsStreamInfo        *si   = NULL;
    jsErrCode           jerr  = 0;
    natsStatus          s;

    const char *url = getenv("NATS_URL");
    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Look up the ORDERS stream and fetch its latest info from the server.
        s = js_GetStreamInfo(&si, js, "ORDERS", NULL, &jerr);
        if (s == NATS_OK)
        {
            int i;

            // Print key fields: name, captured subjects, and current message count.
            printf("Name:     %s\n", si->Config->Name);
            printf("Subjects:");
            for (i = 0; i < si->Config->SubjectsLen; i++)
                printf(" %s", si->Config->Subjects[i]);
            printf("\n");
            printf("Messages: %" PRIu64 "\n", si->State.Msgs);
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
