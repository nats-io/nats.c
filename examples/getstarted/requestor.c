// Copyright 2017 Apcera Inc. All rights reserved.

#include <nats.h>

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsMsg             *reply= NULL;
    natsStatus          s;

    printf("Publishes a message on subject 'help'\n");

    // Creates a connection to the default NATS URL
    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);
    if (s == NATS_OK)
    {
        // Sends a request on "help" and expects a reply.
        // Will wait only for a given number of milliseconds,
        // say for 5 seconds in this example.
        s = natsConnection_RequestString(&reply, conn, "help",
                "really need some", 5000);
    }
    if (s == NATS_OK)
    {
        // If we are here, we should have received the reply
        printf("Received reply: %.*s\n",
               natsMsg_GetDataLength(reply),
               natsMsg_GetData(reply));

        // Need to destroy the message!
        natsMsg_Destroy(reply);
    }

    // Anything that is created need to be destroyed
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}




