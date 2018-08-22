// Copyright 2018 The NATS Authors
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

#include "../examples.h"

static const char *usage = ""\
"-txt           text to send (default is 'hello')\n";

typedef struct __myPubMsgInfo
{
    const char  *payload;
    int         size;
    char        ID[30];

} myPubMsgInfo;

static volatile bool done = false;

static void
_pubAckHandler(const char *guid, const char *error, void *closure)
{
    myPubMsgInfo *pubMsg = (myPubMsgInfo*) closure;

    printf("Ack handler for message ID=%s Data=%.*s GUID=%s - ",
           pubMsg->ID, pubMsg->size, pubMsg->payload, guid);

    if (error != NULL)
        printf("Error= %s\n", error);
    else
        printf("Success!\n");

    // This is a good place to free the pubMsg info since
    // we no longer need it.
    free(pubMsg);

    // Notify the main thread that we are done. This is
    // not the proper way and you should use some locking.
    done = true;
}

int main(int argc, char **argv)
{
    natsStatus      s;
    stanConnOptions *connOpts = NULL;
    stanConnection  *sc       = NULL;
    myPubMsgInfo    *pubMsg   = NULL;

    // This example demonstrates the use of the pubAckHandler closure
    // to correlate published messages and their acks.

    opts = parseArgs(argc, argv, usage);

    printf("Sending 1 message to channel '%s'\n", subj);

    // Now create STAN Connection Options and set the NATS Options.
    s = stanConnOptions_Create(&connOpts);
    if (s == NATS_OK)
        s = stanConnOptions_SetNATSOptions(connOpts, opts);

    // Create the Connection using the STAN Connection Options
    if (s == NATS_OK)
        s = stanConnection_Connect(&sc, cluster, clientID, connOpts);

    // Once the connection is created, we can destroy the options
    natsOptions_Destroy(opts);
    stanConnOptions_Destroy(connOpts);

    // Create an object that represents our message
    if (s == NATS_OK)
    {
        pubMsg = (myPubMsgInfo*) calloc(1, sizeof(myPubMsgInfo));
        if (pubMsg == NULL)
            s = NATS_NO_MEMORY;

        if (s == NATS_OK)
        {
            // Say we want to bind the data that we are going to send
            // to some unique ID that we know about this message.
            pubMsg->payload = txt;
            pubMsg->size    = (int)strlen(txt);
            snprintf(pubMsg->ID, sizeof(pubMsg->ID), "%s:%d", "xyz", 234);
        }
    }
    // We send the message and pass our message info as the closure
    // for the pubAckHandler.
    if (s == NATS_OK)
    {
        s = stanConnection_PublishAsync(sc, subj, pubMsg->payload, pubMsg->size,
                                        _pubAckHandler, (void*) pubMsg);

        // Note that if this call fails, then we need to free the pubMsg
        // object here since it won't be passed to the ack handler.
        if (s != NATS_OK)
            free(pubMsg);
    }

    if (s == NATS_OK)
    {
        while (!done)
            nats_Sleep(15);
    }

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy the connection
    stanConnection_Destroy(sc);

    // To silence reports of memory still in-use with valgrind.
    nats_Sleep(50);
    nats_Close();

    return 0;
}
