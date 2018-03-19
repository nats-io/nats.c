// Copyright 2017-2018 The NATS Authors
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

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsMsg             *msg  = NULL;
    natsStatus          s;

    printf("Listening on subject 'foo'\n");

    // Creates a connection to the default NATS URL
    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);
    if (s == NATS_OK)
    {
        // Creates an synchronous subscription on subject "foo".
        s = natsConnection_SubscribeSync(&sub, conn, "foo");
    }
    if (s == NATS_OK)
    {
        // With synchronous subscriptions, one need to poll
        // using this function. A timeout is used to instruct
        // how long we are willing to wait. The wait is in milliseconds.
        // So here, we are going to wait for 5 seconds.
        s = natsSubscription_NextMsg(&msg, sub, 5000);
    }
    if (s == NATS_OK)
    {
        // If we are here, we should have received a message.
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

        // Need to destroy the message!
        natsMsg_Destroy(msg);
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
