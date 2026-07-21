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
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    if (s == NATS_OK)
    {
        // NATS-DOC-START
        // Publish six messages to the "tasks" subject. With two workers in
        // the "workers" queue group, the server hands each message to
        // exactly one of them.
        char task[32];
        int  i;

        for (i = 1; (s == NATS_OK) && (i <= 6); i++)
        {
            snprintf(task, sizeof(task), "task %d", i);
            s = natsConnection_PublishString(conn, "tasks", task);
            if (s == NATS_OK)
                printf("published \"%s\" to \"tasks\"\n", task);
        }
        // NATS-DOC-END

        // Make sure the messages are on the wire before we exit.
        if (s == NATS_OK)
            s = natsConnection_Flush(conn);
    }

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
