// Copyright 2016 Apcera Inc. All rights reserved.

#include "../adapters/libevent.h"
#include "examples.h"

static const char *usage = ""\
"-count         number of expected messages\n";

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    natsMsg_Destroy(msg);

    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (++count == total)
    {
        elapsed = nats_Now() - start;

        natsConnection_Close(nc);
    }
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    natsStatus          s      = NATS_OK;
    struct event_base   *evLoop= NULL;

    nats_Open(-1);

    opts = parseArgs(argc, argv, usage);

    printf("Listening on '%s'.\n", subj);

    // One time initialization of things that we need.
    natsLibevent_Init();

    // Create a loop.
    evLoop = event_base_new();
    if (evLoop == NULL)
        s = NATS_ERR;

    // Indicate which loop and callbacks to use once connected.
    if (s == NATS_OK)
        s = natsOptions_SetEventLoop(opts, (void*) evLoop,
                                     natsLibevent_Attach,
                                     natsLibevent_Read,
                                     natsLibevent_Write,
                                     natsLibevent_Detach);

    // This is setting the maximum number of pending messages allowed in
    // the library for each subscriber. For max performance, we set it
    // here to the expected total number of messages. You can remove, or
    // set it to a low number to see the effect of messages being dropped
    // by the client library.
    if (s == NATS_OK)
        s = natsOptions_SetMaxPendingMsgs(opts, (int) total);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);

    // Run the event loop.
    // This call will return when the connection is closed (either after
    // receiving all messages, or disconnected and unable to reconnect).
    if (s == NATS_OK)
        event_base_dispatch(evLoop);

    if (s == NATS_OK)
    {
        printPerf("Received", count, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (evLoop != NULL)
        event_base_free(evLoop);

    // To silence reports of memory still in used with valgrind
    nats_Close();
    libevent_global_shutdown();

    return 0;
}
