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
    natsConnection  *conn = NULL;
    jsCtx           *js   = NULL;
    natsStatus      s;
    jsErrCode       jerr  = 0;
    const char      *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Remove the message stored at sequence 2 from ORDERS. This marks
        // it erased but leaves the stored bytes in place until they are
        // later overwritten, which is cheap.
        s = js_DeleteMsg(js, "ORDERS", 2, NULL, &jerr);
        if (s == NATS_OK)
            printf("Deleted message 2 from ORDERS\n");

        // For a message that held data it shouldn't have, use js_EraseMsg
        // instead: the server overwrites the stored bytes right away, the
        // way `nats stream rmm` does, so the old contents can't be read
        // back. It is slower for it.
        //
        //     s = js_EraseMsg(js, "ORDERS", 2, NULL, &jerr);
        // NATS-DOC-END
    }

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
