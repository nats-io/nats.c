// Copyright 2015-2018 The NATS Authors
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

static const char *usage = ""\
"-txt           text to send (default is 'hello')\n" \
"-count         number of messages to send\n";

int main(int argc, char **argv)
{
    natsConnection  *conn  = NULL;
    natsStatistics  *stats = NULL;
    natsOptions     *opts  = NULL;
    int64_t         last   = 0;
    natsStatus      s;
    int             dataLen=0;

    opts = parseArgs(argc, argv, usage);

    printf("Sending %" PRId64 " messages to subject '%s'\n", total, subj);

    s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if (s == NATS_OK)
        start = nats_Now();

    dataLen = (int) strlen(txt);
    for (count = 0; (s == NATS_OK) && (count < total); count++)
        s = natsConnection_Publish(conn, subj, (const void*) txt, dataLen);

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    if (s == NATS_OK)
    {
        printStats(STATS_OUT, conn, NULL, stats);
        printPerf("Sent", total, start, elapsed);
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    // Destroy all our objects to avoid report of memory leak
    natsStatistics_Destroy(stats);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
