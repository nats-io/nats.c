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
"-c             cluster name (default \"test-cluster\")\n" \
"-id            client ID (default \"client\"\n" \
"-count         number of messages to receive\n" \
"-last          deliver starting with last published message (default)\n" \
"-all           deliver all available messages\n" \
"-seq           deliver starting at given sequence number\n" \
"-durable       durable subscription name\n" \
"-qgroup        queue group name\n" \
"-unsubscribe   unsubscribe the durable on exit\n";

static volatile bool done = false;

static void
onMsg(stanConnection *sc, stanSubscription *sub, const char *channel, stanMsg *msg, void *closure)
{
    if (print)
        printf("Received on [%s]: sequence:%" PRIu64 " data:%.*s timestamp:%" PRId64 " redelivered: %s\n",
                channel,
                stanMsg_GetSequence(msg),
                stanMsg_GetDataLength(msg),
                stanMsg_GetData(msg),
                stanMsg_GetTimestamp(msg),
                stanMsg_IsRedelivered(msg) ? "yes" : "no");

    if (start == 0)
        start = nats_Now();

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    if (count == total-1)
    {
        natsStatus s;

        elapsed = nats_Now() - start;

        if (unsubscribe)
            s = stanSubscription_Unsubscribe(sub);
        else
            s = stanSubscription_Close(sub);

        if (s != NATS_OK)
        {
            printf("Error: %d - %s\n", s, natsStatus_GetText(s));
            nats_PrintLastErrorStack(stderr);
        }
    }

    /*
    // If manual acknowledgment was selected, we would acknowledge
    // the message this way:
    stanSubscription_AckMsg(sub, msg);
    */

    stanMsg_Destroy(msg);

    // Increment only here so that when the main thread detects
    // that the total has been sent, it does not start cleaning
    // objects before we have closed the subscription and destroyed
    // the last message. This is to reduce risk of valgrind reporting
    // memory still in-use.
    count++;
}

#if WIN32
static BOOL
sigHandler(DWORD fdwCtrlType)
{
    if (fdwCtrlType==CTRL_C_EVENT)
    {
        done = true;
        return TRUE;
    }
    return FALSE;
}
#else
static void
sigHandler(int ignored) {
    done = true;
}
#endif

static void
connectionLostCB(stanConnection *sc, const char *errTxt, void *closure)
{
    bool *connLost = (bool*) closure;

    printf("Connection lost: %s\n", errTxt);
    *connLost = true;
}

int main(int argc, char **argv)
{
    natsStatus          s;
    stanConnOptions     *connOpts   = NULL;
    stanSubOptions      *subOpts    = NULL;
    stanConnection      *sc         = NULL;
    stanSubscription    *sub        = NULL;
    int64_t             last        = 0;
    bool                connLost    = false;

    opts = parseArgs(argc, argv, usage);

    printf("Receiving %" PRId64 " messages from channel '%s'\n", total, subj);

    // Now create STAN Connection Options and set the NATS Options.
    s = stanConnOptions_Create(&connOpts);
    if (s == NATS_OK)
        s = stanConnOptions_SetNATSOptions(connOpts, opts);

    // Add a callback to be notified if the STAN connection is lost for good.
    if (s == NATS_OK)
        s = stanConnOptions_SetConnectionLostHandler(connOpts, connectionLostCB, (void*)&connLost);

    // Create the Connection using the STAN Connection Options
    if (s == NATS_OK)
        s = stanConnection_Connect(&sc, cluster, clientID, connOpts);

    // Once connection is created, we can destroy opts and connOpts
    natsOptions_Destroy(opts);
    stanConnOptions_Destroy(connOpts);

    if (s == NATS_OK)
        s = stanSubOptions_Create(&subOpts);

    // If durable
    if ((s == NATS_OK) && (durable != NULL))
        s = stanSubOptions_SetDurableName(subOpts, durable);

    // Set position
    if (s == NATS_OK)
    {
        if (deliverAll)
            s = stanSubOptions_DeliverAllAvailable(subOpts);
        else if (deliverLast)
            s = stanSubOptions_StartWithLastReceived(subOpts);
        else if (deliverSeq > 0)
            s = stanSubOptions_StartAtSequence(subOpts, deliverSeq);
    }

    /*
    // To manually acknowledge the messages, you would need to set this option
    if (s == NATS_OK)
        s = stanSubOptions_SetManualAckMode(subOpts, true);

    // To change the number of MaxInflight messages, set this option.
    // For instance, to receive a single message between each ACK, set
    // the value to 1.
    if (s == NATS_OK)
        s = stanSubOptions_SetMaxInflight(subOpts, 1);

    // To change the duration after which the server resends unacknowledged
    // messages, use this option. For instance, cause re-delivery after 5 seconds:
    if (s == NATS_OK)
        s = stanSubOptions_SetAckWait(subOpts, 5000);
    */

    // Create subscription
    if (s == NATS_OK)
    {
        if (qgroup != NULL)
            s = stanConnection_QueueSubscribe(&sub, sc, subj, qgroup, onMsg, NULL, subOpts);
        else
            s = stanConnection_Subscribe(&sub, sc, subj, onMsg, NULL, subOpts);
    }

    // Once subscription is created, we can destroy the subOpts object
    stanSubOptions_Destroy(subOpts);

    if (s == NATS_OK)
    {
#if WIN32
        SetConsoleCtrlHandler((PHANDLER_ROUTINE) sigHandler, TRUE);
#else
        signal(SIGINT, sigHandler);
#endif

        while (!done && !connLost && (count < total))
            nats_Sleep(15);

        if (!connLost)
            printPerf("Received", count, start, elapsed);
    }

    // If test was interrupted before receiving all expected messages,
    // close or unsubscribe. Otherwise, this is done in the message
    // callback.
    if (!connLost && ((sub != NULL) && (count < total)))
    {
        if (unsubscribe)
            s = stanSubscription_Unsubscribe(sub);
        else
            s = stanSubscription_Close(sub);
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
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy our objects
    stanSubscription_Destroy(sub);
    stanConnection_Destroy(sc);

    // To silence reports of memory still in-use with valgrind.
    nats_Sleep(50);
    nats_Close();

    return 0;
}
