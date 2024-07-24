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

#define NSUBS 500
#define NMSGS (1 * 1000)
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
    {true, 6},
    {true, 7},
    {true, 8},
    {true, 9},
    {true, 10},
    {true, 11},
    {true, 12},
    {true, 13},
    {true, 14},
    {true, 15},
    {true, 16},
    {true, 17},
    {true, 18},
    {true, 19},
    {true, 20},
    {true, 23},
    {true, 31},
    {true, 47},
    {true, 100},
    {true, NSUBS / 2 - 1},
    {true, NSUBS - 1},
    {true, NSUBS},
};

typedef struct
{
    natsSubscription *sub;
    uint64_t sum;
    uint64_t xor ;
    uint64_t count;
    int64_t closedTimestamp;
} subState;

subState state[NSUBS];

static uint64_t _expectedSum(void)
{
    uint64_t sum = 0;
    for (int64_t i = 0; i < NMSGS; i++)
        sum += i;
    return sum;
}

static uint64_t _expectedXOR(void)
{
    uint64_t xor = 0;
    for (int64_t i = 0; i < NMSGS; i++)
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

static uint64_t expectedSum;
static uint64_t expectedXOR;

static natsStatus
_bench(benchConfig *c, int64_t *start, int64_t *end)
{
    natsStatus s = nats_Open(-1);
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    char buf[16];

    memset(state, 0, sizeof(state));
    *start = nats_Now();

    IFOK(s, natsOptions_Create(&opts));
    IFOK(s, nats_SetMessageDeliveryPoolSize(c->maxThreads));
    IFOK(s, natsOptions_SetErrorHandler(opts, asyncCb, NULL));
    IFOK(s, natsOptions_UseGlobalMessageDelivery(opts, c->useGlobalDelivery));

    IFOK(s, natsConnection_Connect(&conn, opts));

    for (int i = 0; i < NSUBS; i++)
    {
        IFOK(s, natsConnection_Subscribe(&state[i].sub, conn, "foo", _onMessage, &state[i]));
        IFOK(s, natsSubscription_SetPendingLimits(state[i].sub, INT_MAX, INT_MAX));
        IFOK(s, natsSubscription_AutoUnsubscribe(state[i].sub, NMSGS));
        IFOK(s, natsSubscription_SetOnCompleteCB(state[i].sub, _onComplete, &state[i]));
    }

    for (int i = 0; i < NMSGS; i++)
    {
        snprintf(buf, sizeof(buf), "%d", i);
        IFOK(s, natsConnection_PublishString(conn, "foo", buf));
        IFOK(s, natsConnection_Flush(conn));
    }

    while (s == NATS_OK)
    {
        bool done = true;
        for (int i = 0; i < NSUBS; i++)
        {
            // threads don't touch this, should be safe
            if (natsSubscription_IsValid(state[i].sub))
            {
                done = false;
                break;
            }
        }

        nats_Sleep(100);
        if (done)
            break;
    }

    *end = 0;
    if (s == NATS_OK)
    {
        for (int i = 0; i < NSUBS; i++)
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
            if (state[i].count != NMSGS)
            {
                fprintf(stderr, "Error: count is %" PRId64 " for sub %d, expected %d\n", state[i].count, i, NMSGS);
                s = NATS_ERR;
                break;
            }

            if (state[i].closedTimestamp > *end)
                *end = state[i].closedTimestamp;
        }
    }

    // cleanup
    for (int i = 0; i < NSUBS; i++)
        natsSubscription_Destroy(state[i].sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);
    nats_CloseAndWait(0);

    return s;
}

int main(void)
{
    char namebuf[128];
    int64_t start = 0, end = 0;

    expectedSum = _expectedSum();
    expectedXOR = _expectedXOR();

    int pid = _startServer("nats://127.0.0.1:4222", NULL, true);
    CHECK_SERVER_STARTED(pid);

    for (benchConfig *c = configs; c < configs + sizeof(configs) / sizeof(configs[0]); c++)
    {
        natsStatus s = NATS_OK;
        int64_t totalDuration = 0;

        snprintf(namebuf, sizeof(namebuf), "%d_subs_%d_messages_", NSUBS, NMSGS);
        if (c->useGlobalDelivery)
            snprintf(namebuf + strlen(namebuf), sizeof(namebuf) - strlen(namebuf), "global_%d", c->maxThreads);
        else
            snprintf(namebuf + strlen(namebuf), sizeof(namebuf) - strlen(namebuf), "designated");

        for (int i = 0; i < REPEAT; i++)
        {
            s = _bench(c, &start, &end);
            if (s != NATS_OK)
            {
                fprintf(stderr, "Error: %s\n", natsStatus_GetText(s));
                nats_PrintLastErrorStack(stderr);
                exit(1);
            }

            totalDuration += (end - start);
        }

        printf("%s_average_%d: %" PRId64 " ms\n", namebuf, REPEAT, totalDuration / REPEAT);
    }

    _stopServer(pid);
}
