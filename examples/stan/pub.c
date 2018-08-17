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
"-txt           text to send (default is 'hello')\n" \
"-count         number of messages to send\n" \
"-sync          publish synchronously (default is async)\n";

static volatile int ackCount = 0;
static volatile int errCount = 0;

static void
_pubAckHandler(const char *guid, const char *error, void *closure)
{
    // This callback can be invoked by different threads for the
    // same connection, so access should be protected. For this
    // example, we don't.
    ackCount++;
    if (error != NULL)
    {
        printf("pub ack for guid:%s error=%s\n", guid, error);
        errCount++;
    }
}

static void
connectionLostCB(stanConnection *sc, const char *errTxt, void *closure)
{
    bool *connLost = (bool*) closure;

    printf("Connection lost: %s\n", errTxt);
    *connLost = true;
}

int main(int argc, char **argv)
{
    natsStatus      s;
    stanConnOptions *connOpts = NULL;
    stanConnection  *sc       = NULL;
    int             len;
    bool            connLost = false;

    opts = parseArgs(argc, argv, usage);
    len = (int) strlen(txt);

    printf("Sending %" PRId64 " messages to channel '%s'\n", total, subj);

    // Now create STAN Connection Options and set the NATS Options.
    s = stanConnOptions_Create(&connOpts);
    if (s == NATS_OK)
        s = stanConnOptions_SetNATSOptions(connOpts, opts);

    // Set smaller ping intervals
    if (s == NATS_OK)
        s = stanConnOptions_SetPings(connOpts, 1, 5);

    // Add a callback to be notified if the STAN connection is lost for good.
    if (s == NATS_OK)
        s = stanConnOptions_SetConnectionLostHandler(connOpts, connectionLostCB, (void*)&connLost);

    /*
    // To reduce MaxPubAcksInflight to 1000 and factor of 1.0
    if (s == NATS_OK)
        s = stanConnOptions_SetMaxPubAcksInflight(connOpts, 1000, 1.0);
    */

    // Create the Connection using the STAN Connection Options
    if (s == NATS_OK)
        s = stanConnection_Connect(&sc, cluster, clientID, connOpts);

    // Once the connection is created, we can destroy the options
    natsOptions_Destroy(opts);
    stanConnOptions_Destroy(connOpts);

    if (s == NATS_OK)
        start = nats_Now();

    for (count = 0; (s == NATS_OK) && (count < total); count++)
    {
        if (async)
            s = stanConnection_PublishAsync(sc, subj, (const void*) txt, len, _pubAckHandler, NULL);
        else
            s = stanConnection_Publish(sc, subj, (const void*) txt, len);
    }

    if (!connLost && (s == NATS_OK))
    {
        if (async)
        {
            while (ackCount != total)
                nats_Sleep(15);
        }

        printPerf("Sent", total, start, elapsed);
        printf("Publish ack received: %d - with error: %d\n", ackCount, errCount);
    }

    // If the connection was created, try to close it
    if (!connLost && (sc != NULL))
    {
        natsStatus closeSts = stanConnection_Close(sc);

        if ((s == NATS_OK) && (closeSts != NATS_OK))
            s = closeSts;
    }

    if (s != NATS_OK)
    {
        // If we finished before the end, let's wait a tiny
        // bit to see if the failure is due to connection lost.
        if (ackCount != total)
            nats_Sleep(100);

        // If connection was lost, the real reason is reported
        // the the connectionLostCB callback.
        if (!connLost)
        {
            printf("Error: %d - %s\n", s, natsStatus_GetText(s));
            nats_PrintLastErrorStack(stderr);
        }
    }

    // Destroy the connection
    stanConnection_Destroy(sc);

    // To silence reports of memory still in-use with valgrind.
    nats_Sleep(50);
    nats_Close();

    return 0;
}
