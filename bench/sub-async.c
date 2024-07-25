// Copyright 2024 The NATS Authors
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

#include "bench.h"

#define MAX_SUBS 500
int numSubs[] = {1, 2, 3, 5, 7, 11, 23, 43, 83, 163, 317, 499};

#define TOTAL_MESSAGES (500 * 1000)
#define REPEAT 5

typedef struct
{
    bool useGlobalDelivery;
    int maxThreads;
} benchConfig;

benchConfig configs[] = {
    {false, 1}, // 1 is not used in this case, just to quiet nats_SetMessageDeliveryPoolSize
    {true, 1},
    {true, 2},
    {true, 3},
    {true, 4},
    {true, 5},
    {true, 7},
    {true, 11},
    {true, 19},
    {true, 41},
    {true, 79},
    {true, 157},
    {true, 307},
};

typedef struct
{
    natsSubscription *sub;
    uint64_t sum;
    uint64_t xor ;
    uint64_t count;
    int64_t closedTimestamp;
} subState;

subState state[MAX_SUBS];

static uint64_t _expectedSum(int N)
{
    uint64_t sum = 0;
    for (int64_t i = 0; i < N; i++)
        sum += i;
    return sum;
}

static uint64_t _expectedXOR(int N)
{
    uint64_t xor = 0;
    for (int64_t i = 0; i < N; i++)
        xor ^= i;
    return xor;
}

static void
_onMessage(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    subState *s = (subState *)closure;
    char buf[32];
    int len = natsMsg_GetDataLength(msg);
    if (len > 31)
        len = 31;

    strncpy(buf, natsMsg_GetData(msg), len);
    buf[len] = '\0';
    int64_t val = atoi(buf);

    s->sum += val;
    s->xor ^= val;
    s->count++;

    natsMsg_Destroy(msg);
}

static void
_onComplete(void *closure)
{
    subState *s = (subState *)closure;
    s->closedTimestamp = nats_Now();
}

static natsStatus
_bench(benchConfig *c, int nSubs, int nMessages, int flushAfter, int *durMillis)
{
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    uint64_t expectedSum = _expectedSum(nMessages);
    uint64_t expectedXOR = _expectedXOR(nMessages);
    char buf[16];
    int64_t start, end;

    memset(state, 0, sizeof(state));
    start = nats_Now();

    natsStatus s = nats_Open(-1);
    IFOK(s, natsOptions_Create(&opts));
    IFOK(s, nats_SetMessageDeliveryPoolSize(c->maxThreads));
    IFOK(s, natsOptions_SetErrorHandler(opts, asyncCb, NULL));
    IFOK(s, natsOptions_UseGlobalMessageDelivery(opts, c->useGlobalDelivery));

    IFOK(s, natsConnection_Connect(&conn, opts));

    for (int i = 0; i < nSubs; i++)
    {
        IFOK(s, natsConnection_Subscribe(&state[i].sub, conn, "foo", _onMessage, &state[i]));
        IFOK(s, natsSubscription_SetPendingLimits(state[i].sub, -1, -1));
        IFOK(s, natsSubscription_AutoUnsubscribe(state[i].sub, nMessages));
        IFOK(s, natsSubscription_SetOnCompleteCB(state[i].sub, _onComplete, &state[i]));
    }

    for (int i = 0; i < nMessages; i++)
    {
        snprintf(buf, sizeof(buf), "%d", i);
        IFOK(s, natsConnection_PublishString(conn, "foo", buf));
        if ((i % flushAfter) == 0)
            IFOK(s, natsConnection_Flush(conn));
    }

    while (s == NATS_OK)
    {
        bool done = true;
        for (int i = 0; i < nSubs; i++)
        {
            // threads don't touch this, should be safe
            if (natsSubscription_IsValid(state[i].sub))
            {
                done = false;
                break;
            }
        }

        nats_Sleep(10);
        if (done)
            break;
    }

    end = 0;
    if (s == NATS_OK)
    {
        for (int i = 0; i < nSubs; i++)
        {
            if (state[i].sum != expectedSum)
            {
                s = NATS_ERR;
                fprintf(stderr, "Error: sum is %" PRId64 " for sub %d, expected %" PRId64 "\n", state[i].sum, i, expectedSum);
                break;
            }
            if (state[i].xor != expectedXOR)
            {
                fprintf(stderr, "Error: xor is %" PRId64 " for sub %d, expected %" PRId64 "\n", state[i].xor, i, expectedXOR);
                s = NATS_ERR;
                break;
            }
            if ((int)(state[i].count) != nMessages)
            {
                fprintf(stderr, "Error: count is %" PRId64 " for sub %d, expected %d\n", state[i].count, i, nMessages);
                s = NATS_ERR;
                break;
            }

            if (state[i].closedTimestamp > end)
                end = state[i].closedTimestamp;
        }
    }

    // cleanup
    for (int i = 0; i < nSubs; i++)
        natsSubscription_Destroy(state[i].sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);
    nats_CloseAndWait(0);

    *durMillis = (int)(end - start);

    return s;
}

int main(void)
{
    int pid = _startServer("nats://127.0.0.1:4222", NULL, true);
    CHECK_SERVER_STARTED(pid);

    for (benchConfig *c = configs; c < configs + sizeof(configs) / sizeof(configs[0]); c++)
    {
        for (int *n = numSubs; n < numSubs + sizeof(numSubs) / sizeof(numSubs[0]); n++)
        {
            natsStatus s = NATS_OK;
            int nMessages = TOTAL_MESSAGES / *n;
            int flushAfter = nMessages / *n / 10;
            if (flushAfter < 5)
                flushAfter = 5;
            int durMillis = 0;

            for (int i = 0; i < REPEAT; i++)
            {
                s = _bench(c, *n, nMessages, flushAfter, &durMillis);
                if (s != NATS_OK)
                {
                    fprintf(stderr, "Error: %s\n", natsStatus_GetText(s));
                    nats_PrintLastErrorStack(stderr);
                    exit(1);
                }
            }

            printf("%d,%d,%d\n", c->useGlobalDelivery ? c->maxThreads : -1, *n, durMillis / REPEAT);
            fflush(stdout);
        }
    }

    _stopServer(pid);
}
