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
#include <stdio.h>
#include <string.h>

// NATS-DOC-START
// A worker is a queue subscription plus an ID for logging.
typedef struct
{
    char                id[16];
    natsSubscription    *sub;
} Worker;

static void
onTask(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    Worker *w = (Worker *) closure;

    printf("Worker %s processing: %.*s\n", w->id,
           natsMsg_GetDataLength(msg), natsMsg_GetData(msg));
    // Simulate work
    nats_Sleep(100);

    natsMsg_Destroy(msg);
}

static natsStatus
startWorker(Worker *w, natsConnection *nc, int id)
{
    snprintf(w->id, sizeof(w->id), "%d", id);
    return natsConnection_QueueSubscribe(&(w->sub), nc, "tasks",
                                         "workers", onTask, (void *) w);
}

static void
stopWorker(Worker *w)
{
    // Leaving the queue group: remaining members keep sharing the load.
    natsSubscription_Unsubscribe(w->sub);
}
// NATS-DOC-END

int main(void)
{
    natsConnection      *conn = NULL;
    Worker              workers[5];
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    memset(workers, 0, sizeof(workers));

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Scale up: add five workers to the group
    for (int i = 0; (s == NATS_OK) && (i < 5); i++)
        s = startWorker(&workers[i], conn, i + 1);

    // Scale down: remove them again
    if (s == NATS_OK)
    {
        for (int i = 0; i < 5; i++)
            stopWorker(&workers[i]);
    }
    // NATS-DOC-END

    for (int i = 0; i < 5; i++)
        natsSubscription_Destroy(workers[i].sub);
    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
