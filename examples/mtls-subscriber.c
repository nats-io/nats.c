// Copyright 2015-2024 The NATS Authors
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

#include "examples.h"

static const char *usage = "-tls -tlscacert <ca_file> -tlscert <client_cert> -tlskey <client_key> [-tlshost <hostname>] [-count num_msgs] [-subj subject] [-print]";

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    // Destroy message that was received
    natsMsg_Destroy(msg);

    count++;
}

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s     = NATS_OK;

    opts = parseArgs(argc, argv, usage);

    printf("Connecting to NATS server with mTLS...\n");

    s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        printf("Successfully connected with mutual TLS authentication!\n");
        printf("Subscribing to subject '%s'...\n", subj);

        start = nats_Now();

        s = natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);
    }

    if (s == NATS_OK)
    {
        printf("Waiting for %" PRId64 " messages (or timeout of %" PRId64 " ms)...\n",
               total, timeout);

        while ((s == NATS_OK) && (count < total))
        {
            nats_Sleep(100);

            if (nats_Now() - start > timeout)
            {
                printf("Timeout waiting for messages!\n");
                break;
            }
        }

        if (s == NATS_OK)
        {
            printPerf("Received");
        }
    }
    else
    {
        printf("Error connecting to NATS server with mTLS: %u - %s\n",
               s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy objects that were created
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    nats_Close();

    return (s == NATS_OK ? 0 : 1);
}
