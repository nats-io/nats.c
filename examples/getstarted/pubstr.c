// Copyright 2017 Apcera Inc. All rights reserved.

#include <nats.h>

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsStatus          s;

    printf("Publishes a message on subject 'foo'\n");

    // Creates a connection to the default NATS URL
    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);
    if (s == NATS_OK)
    {
        // This is a convenient function to send a message on "foo"
        // as a string.
        s = natsConnection_PublishString(conn, "foo", "hello!");
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




