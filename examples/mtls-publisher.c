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

static const char *usage = "-tls -tlscacert <ca_file> -tlscert <client_cert> -tlskey <client_key> [-tlshost <hostname>] [-count num_msgs] [-subj subject]";

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s     = NATS_OK;
    int64_t             i;

    opts = parseArgs(argc, argv, usage);

    printf("Connecting to NATS server with mTLS...\n");

    s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
    {
        printf("Successfully connected with mutual TLS authentication!\n");
        printf("Publishing %" PRId64 " messages to subject '%s'...\n", total, subj);

        start = nats_Now();

        for (i = 0; (s == NATS_OK) && (i < total); i++)
        {
            s = natsConnection_PublishString(conn, subj, payload);
            if (s == NATS_OK)
                count++;
        }

        if (s == NATS_OK)
            s = natsConnection_FlushTimeout(conn, 5000);

        if (s == NATS_OK)
        {
            printPerf("Published");
        }
        else
        {
            printf("Error during publish: %u - %s\n", s, natsStatus_GetText(s));
            nats_PrintLastErrorStack(stderr);
        }

        natsConnection_Destroy(conn);
    }
    else
    {
        printf("Error connecting to NATS server with mTLS: %u - %s\n",
               s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    natsOptions_Destroy(opts);

    nats_Close();

    return (s == NATS_OK ? 0 : 1);
}
