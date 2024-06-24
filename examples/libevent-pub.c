// Copyright 2016-2018 The NATS Authors
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

#include <string.h>

#include "nats/adapters/libevent.h"
#include "nats/adapters/malloc_heap.h"
#include "examples.h"

static const char *usage = ""
                           "-txt           text to send (default is 'hello')\n"
                           "-count         number of messages to send\n";

static void reportAndFreeData(natsConnection *nc, natsMessage *m, void *closure)
{
    printf("Message published, freeing the data: %.*s\n", (int)nats_GetMessageDataLen(m), nats_GetMessageData(m));
    free(closure);
}

static void reportAndDisconnect(natsConnection *nc, natsMessage *m, void *closure)
{
    printf("Last message published, now disconnecting: %.*s\n", (int)nats_GetMessageDataLen(m), nats_GetMessageData(m));
    nats_DestroyConnection(nc);
}

static void connected(natsConnection *nc, void *closure)
{
    natsStatus s;
    natsString payload;

    // Send a simple message with static data. Publish-and-forget.
    payload = nats_ToString("Hello, NATS!");
    natsMessage *msg1 = NULL;
    s = nats_CreateMessage(&msg1, nc, subj);
    if (s == NATS_OK)
        s = nats_SetMessagePayload(msg1, payload.data, payload.len);
    if (s == NATS_OK)
        s = nats_AsyncPublishNoCopy(nc, msg1);
    if (s == NATS_OK)
        printf("Message enqueued, text: %.*s\n", (int)payload.len, payload.data);
    nats_ReleaseMessage(msg1);

    payload = nats_ToString(strdup("Hello, NATS! "
                                   "I was allocated on the heap, "
                                   "my bytes were copied into another buffer, "
                                   "and I was freed immediately by the app."));
    natsMessage *msg2 = NULL;
    if (s == NATS_OK)
        s = nats_CreateMessage(&msg2, nc, subj);
    if (s == NATS_OK)
        s = nats_SetMessagePayload(msg2, payload.data, payload.len);
    if (s == NATS_OK)
        s = nats_AsyncPublish(nc, msg2);
    if (s == NATS_OK)
        printf("Message enqueued as a copy, freeing the data now: %.*s\n",
               (int)nats_GetMessageDataLen(msg2), nats_GetMessageData(msg2));
    free(payload.data);
    nats_ReleaseMessage(msg2);

    // Send a message using nats_AsyncPublishNoCopy, will NOT make a copy of the
    // message so we need to free our memory once the message has been published.
    payload = nats_ToString(strdup("Hello, NATS! "
                                   "I was allocated on the heap, "
                                   "my bytes were NOT copied, "
                                   "and I was freed by a cusom 'message published' callback"));
    natsMessage *msg3 = NULL;
    if (s == NATS_OK)
        s = nats_CreateMessage(&msg3, nc, subj);
    if (s == NATS_OK)
        s = nats_SetMessagePayload(msg3, payload.data, payload.len);
    if (s == NATS_OK)
        s = nats_SetOnMessagePublished(msg3, reportAndFreeData, payload.data);
    if (s == NATS_OK)
        s = nats_AsyncPublishNoCopy(nc, msg3);
    if (s == NATS_OK)
        printf("Message enqueued, text: %.*s\n", (int)payload.len, payload.data);
    nats_ReleaseMessage(msg3);

    // Same, but there is a shortcut if we don't need a full "done" callback,
    // just need to free the buffer.
    payload = nats_ToString(strdup("Hello, NATS! "
                                   "I was allocated on the heap, "
                                   "my bytes were NOT copied, "
                                   "and I was 'free'-ed by the cleanup, no custom callback needed."));
    natsMessage *msg4 = NULL;
    if (s == NATS_OK)
        s = nats_CreateMessage(&msg4, nc, subj);
    if (s == NATS_OK)
        s = nats_SetMessagePayload(msg4, payload.data, payload.len);
    if (s == NATS_OK)
        s = nats_SetOnMessageCleanup(msg4, free, payload.data);
    if (s == NATS_OK)
        s = nats_AsyncPublishNoCopy(nc, msg4);
    if (s == NATS_OK)
        printf("Message enqueued, text: %.*s\n", (int)payload.len, payload.data);
    nats_ReleaseMessage(msg4);

    // Send the last message and quit once done. This demonstrates how to set a
    // "written" callback on a message.
    natsMessage *msgFinal = NULL;
    if (s == NATS_OK)
        s = nats_CreateMessage(&msgFinal, nc, subj);
    if (s == NATS_OK)
        s = nats_SetOnMessagePublished(msgFinal, reportAndDisconnect, NULL);
    if (s == NATS_OK)
        s = nats_AsyncPublish(nc, msgFinal);
    if (s == NATS_OK)
        printf("FINAL message enqueued, text: %.*s\n", (int)payload.len, payload.data);
}

void closedConnection(natsConnection *nc, void *closure)
{
    printf("Connection closed, last error: '%s'\n", nats_GetConnectionError(nc));
}

// The event loop and the connection callbacks are passed to the NATS API as
// pointers, they never change once initialized. Allocate them as global data.
static natsEventLoop ev;

int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    natsConnection *conn = NULL;

    natsOptions *opts = parseArgs(argc, argv, usage);
    nats_SetOnConnected(opts, connected, NULL);
    nats_SetOnConnectionClosed(opts, closedConnection, NULL);
    printf("Hello, mini-nats! Will try to send a message to '%s'\n", subj);

    // Create an event loop and initia
    s = natsLibevent_Init(&ev);
    if (s == NATS_OK)
        s = nats_AsyncConnectWithOptions(&conn, &ev, opts);
    if (s == NATS_OK)
    {
        event_base_dispatch(ev.loop);
    }

    // Cleanup.
    nats_DestroyConnection(conn);
    nats_Shutdown();
    event_base_free(ev.loop);
    libevent_global_shutdown();

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        return 1;
    }
    return 0;
}
