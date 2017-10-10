// Copyright 2017 Apcera Inc. All rights reserved.

#include <nats.h>

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("Received msg: %s - %.*s\n",
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg));

    // Sends a reply
    if (natsMsg_GetReply(msg) != NULL)
    {
        natsConnection_PublishString(nc, natsMsg_GetReply(msg),
                                     "here's some help");
    }

    // Need to destroy the message!
    natsMsg_Destroy(msg);

    // Notify the main thread that we are done.
    *(bool *)(closure) = true;
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsStatus          s;
    volatile bool       done  = false;

    printf("Listening for requests on subject 'help'\n");

    // Creates a connection to the default NATS URL
    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);
    if (s == NATS_OK)
    {
        // Creates an asynchronous subscription on subject "help",
        // waiting for a request. If a message arrives on this
        // subject, the callback onMsg() will be invoked and it
        // will send a reply.
        s = natsConnection_Subscribe(&sub, conn, "help", onMsg, (void*) &done);
    }
    if (s == NATS_OK)
    {
        for (;!done;) {
            nats_Sleep(100);
        }
    }

    // Anything that is created need to be destroyed
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
