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

#include "test.h"
#include "sub.h"

#define REPEAT 5

typedef struct __env ENV;

typedef struct
{
    bool useGlobalDelivery;
    int max;
} threadConfig;

typedef struct
{
    natsSubscription *sub;
    uint64_t sum;
    uint64_t xor ;
    uint64_t count;
    int64_t closedTimestamp;

    ENV *env;
} subState;

typedef natsStatus (*publishFunc)(natsConnection *nc, const char *subject, ENV *env);

struct __env
{
    natsMutex *mu;
    int numSubs;
    threadConfig threads;
    int numPubMessages;

    bool progressiveFlush;
    publishFunc pubf;
    int64_t delayNano;

    subState subs[1000]; // magic number is always enough.
};

static void _onMessage(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);
static void _onError(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure);
static void _onComplete(void *closure);
static void _benchMatrix(threadConfig *threadsVector, int lent, int *subsVector, int lens, int numMessages, ENV *env);
static natsStatus _bench(ENV *env, int *best, int *avg, int *worst);
static natsStatus _publish(natsConnection *nc, const char *subject, ENV *env);
static natsStatus _inject(natsConnection *nc, const char *subject, ENV *env);
static natsStatus _enqueueToSub(natsSubscription *sub, natsMsg *m);
static uint64_t _expectedSum(int N);
static uint64_t _expectedXOR(int N);

#define RUN_MATRIX(_threads, _subs, _messages, _env) _benchMatrix(_threads, sizeof(_threads) / sizeof(*_threads), _subs, sizeof(_subs) / sizeof(*_subs), _messages, _env)

// This benchmark publishes messages ASAP (no rate limiting) and measures
// message delivery to a few subscribers. This approach does not work well for a
// large number of subscribers because the server will be overwhelmed having to
// send too many messages at once.
void test_BenchSubscribeAsync_Small(void)
{
    threadConfig threads[] = {
        {false, 1}, // 1 is not used in this case, just to quiet nats_SetMessageDeliveryPoolSize
        {true, 1},
        {true, 2},
        {true, 3},
        {true, 5},
        {true, 7},
    };

    int subs[] = {1, 2, 3, 7, 8, 13};

    ENV env = {
        .pubf = _publish,
        .progressiveFlush = false,
    };
    RUN_MATRIX(threads, subs, 200 * 1000, &env);
}

// This benchmark publishes messages, flushing the connection every now and then
// to ensure the server is not overwhelmed, and measures message delivery to a
// few subscribers. This approach works well for a large number of
// subscribers, but can be too slow for a few subscriptions.
void test_BenchSubscribeAsync_Large(void)
{
    threadConfig threads[] = {
        {false, 1},
        {true, 5},
        {true, 11},
        {true, 23},
        {true, 47},
        {true, 91},
    };

    int subs[] = {1, 2, 23, 47, 81, 120};

    ENV env = {
        .pubf = _publish,
        .progressiveFlush = true,
    };

    RUN_MATRIX(threads, subs, 100 * 1000, &env);
}

// This benchmark injects the messages directly into the relevant queue for
// delivery, bypassing the publish step.
void test_BenchSubscribeAsync_Inject(void)
{
    threadConfig threads[] = {
        {false, 1}, // 1 is not used in this case, just to quiet nats_SetMessageDeliveryPoolSize
        {true, 1},
        {true, 2},
        {true, 3},
        {true, 7},
        {true, 11},
        {true, 19},
        {true, 163},
    };

    int subs[] = {1, 2, 3, 5, 10, 23, 83, 163, 499};

    ENV env = {
        .pubf = _inject,
    };

    RUN_MATRIX(threads, subs, 100 * 1000, &env);
}

// This benchmark injects the messages directly into the relevant queue for
// delivery, bypassing the publish step. It uses a delay to simulate a slow-ish
// callback.
void test_BenchSubscribeAsync_InjectSlow(void)
{
#ifdef _WIN32
    // This test relies on nanosleep, not sure what the Windows equivalent is. Skip fr now.
    printf("Skipping BenchSubscribeAsync_InjectSlow on Windows\n");
    return;

#else

    threadConfig threads[] = {
        {false, 1}, // 1 is not used in this case, just to quiet nats_SetMessageDeliveryPoolSize
        {true, 1},
        {true, 2},
        {true, 3},
        {true, 7},
        {true, 11},
        {true, 79},
        {true, 163},
    };

    int subs[] = {1, 3, 7, 23, 83, 163, 499};

    ENV env = {
        .pubf = _inject,
        .delayNano = 10 * 1000, // 10µs
    };

    RUN_MATRIX(threads, subs, 10000, &env);
#endif // _WIN32
}

static void _benchMatrix(threadConfig *threadsVector, int lent, int *subsVector, int lens, int NMessages, ENV *env)
{
    if (natsMutex_Create(&env->mu) != NATS_OK)
    {
        fprintf(stderr, "Error creating mutex\n");
        exit(1);
    }
    printf("[\n");
    for (int *sv = subsVector; sv < subsVector + lens; sv++)
    {
        int numSubs = *sv;
        bool uselessFromHere = false;
        int numPubMessages = NMessages / numSubs;
        if (numPubMessages == 0)
            numPubMessages = 1;

        for (threadConfig *tv = threadsVector; tv < threadsVector + lent; tv++)
        {
            natsStatus s = NATS_OK;
            threadConfig threads = *tv;
            int best = 0, average = 0, worst = 0;

            if (threads.useGlobalDelivery)
            {
                if (uselessFromHere)
                    continue;
                if (threads.max > numSubs)
                    uselessFromHere = true; // execute this test, but a larger MaxThreads will not make a difference.
            }

            env->numSubs = numSubs;
            env->numPubMessages = numPubMessages;
            env->threads = threads;
            for (int i = 0; i < REPEAT; i++)
            {
                int b = 0, a = 0, w = 0;
                s = _bench(env, &b, &a, &w);
                if (s != NATS_OK)
                {
                    fprintf(stderr, "Error: %s\n", natsStatus_GetText(s));
                    nats_PrintLastErrorStack(stderr);
                    exit(1);
                }

                if ((b < best) || (best == 0))
                    best = b;
                if (w > worst)
                    worst = w;
                average += a;
            }
            average /= REPEAT;

            const char *comma = (sv == subsVector + lens - 1) && (tv == threadsVector + lent - 1) ? "" : ",";
            printf("\t{\"subs\":%d, \"threads\":%d, \"messages\":%d, \"best\":%d, \"average\":%d, \"worst\":%d}%s\n",
                   numSubs, env->threads.useGlobalDelivery ? env->threads.max : 0, numPubMessages * numSubs, best, average, worst, comma);
            fflush(stdout);
        }
    }
    printf("]\n");
    natsMutex_Destroy(env->mu);
}

static natsStatus _bench(ENV *env, int *best, int *avg, int *worst)
{
    natsConnection *nc = NULL;
    natsOptions *opts = NULL;
    uint64_t expectedSum = _expectedSum(env->numPubMessages);
    uint64_t expectedXOR = _expectedXOR(env->numPubMessages);
    char subject[256];
    int64_t start, b, w, a;

    if (env->numSubs > 1000) // magic number check.
        return NATS_INVALID_ARG;
    memset(env->subs, 0, sizeof(subState) * 1000);
    for (int i = 0; i < env->numSubs; i++)
        env->subs[i].env = env; // set the environment to access it in the callbacks.

    natsPid pid = _startServer("nats://127.0.0.1:4222", NULL, true);
    if (pid == NATS_INVALID_PID)
        return NATS_ERR;

    natsStatus s = nats_Open(-1);
    IFOK(s, natsNUID_Next(subject, NUID_BUFFER_LEN + 1));
    IFOK(s, natsOptions_Create(&opts));
    IFOK(s, nats_SetMessageDeliveryPoolSize(env->threads.max));
    IFOK(s, natsOptions_SetErrorHandler(opts, _onError, NULL));
    IFOK(s, natsOptions_UseGlobalMessageDelivery(opts, env->threads.useGlobalDelivery));

    IFOK(s, natsConnection_Connect(&nc, opts));

    for (int i = 0; i < env->numSubs; i++)
    {
        IFOK(s, natsConnection_Subscribe(&(env->subs[i].sub), nc, subject, _onMessage, &env->subs[i]));
        IFOK(s, natsSubscription_SetPendingLimits(env->subs[i].sub, -1, -1));
        IFOK(s, natsSubscription_AutoUnsubscribe(env->subs[i].sub, env->numPubMessages));
        IFOK(s, natsSubscription_SetOnCompleteCB(env->subs[i].sub, _onComplete, &env->subs[i]));
    }

    start = nats_Now();

    // Publish or inject the messages!
    IFOK(s, env->pubf(nc, subject, env));

    while (s == NATS_OK)
    {
        bool done = true;
        for (int i = 0; i < env->numSubs; i++)
        {
            // threads don't touch this, should be safe
            if (natsSubscription_IsValid(env->subs[i].sub))
            {
                done = false;
                break;
            }
        }

        nats_Sleep(10);
        if (done)
            break;
    }

    b = w = a = 0;
    natsMutex_Lock(env->mu);
    if (s == NATS_OK)
    {
        for (int i = 0; i < env->numSubs; i++)
        {
            if (env->subs[i].sum != expectedSum)
            {
                s = NATS_ERR;
                fprintf(stderr, "Error: sum is %" PRId64 " for sub %d, expected %" PRId64 "\n", env->subs[i].sum, i, expectedSum);
                break;
            }
            if (env->subs[i].xor != expectedXOR)
            {
                fprintf(stderr, "Error: xor is %" PRId64 " for sub %d, expected %" PRId64 "\n", env->subs[i].xor, i, expectedXOR);
                s = NATS_ERR;
                break;
            }
            if ((int)(env->subs[i].count) != env->numPubMessages)
            {
                fprintf(stderr, "Error: count is %" PRId64 " for sub %d, expected %d\n", env->subs[i].count, i, env->numPubMessages);
                s = NATS_ERR;
                break;
            }

            int64_t dur = env->subs[i].closedTimestamp - start;
            if (dur > w)
                w = dur;
            if ((dur < b) || (b == 0))
                b = dur;
            a += dur;
        }
    }
    natsMutex_Unlock(env->mu);

    // cleanup
    for (int i = 0; i < env->numSubs; i++)
        natsSubscription_Destroy(env->subs[i].sub);
    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);
    _stopServer(pid);
    nats_CloseAndWait(0);

    *best = (int)b;
    *avg = (int)(a / env->numSubs);
    *worst = (int)w;

    return s;
}

static void _onMessage(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    subState *ss = (subState *)closure;

#ifndef _WIN32
    if (ss->env->delayNano > 0)
    {
        struct timespec wait = {0, ss->env->delayNano};
        nanosleep(&wait, NULL);
    }
#endif

    char buf[32];
    int len = natsMsg_GetDataLength(msg);
    if (len > 31)
        len = 31;

    strncpy(buf, natsMsg_GetData(msg), len);
    buf[len] = '\0';
    int64_t val = atoi(buf);

    ss->sum += val;
    ss->xor ^= val;
    ss->count++;

    natsMsg_Destroy(msg);
}

static void _onComplete(void *closure)
{
    subState *ss = (subState *)closure;
    natsMutex_Lock(ss->env->mu);
    ss->closedTimestamp = nats_Now();
    natsMutex_Unlock(ss->env->mu);
}

static natsStatus _publish(natsConnection *nc, const char *subject, ENV *env)
{
    natsStatus s = NATS_OK;
    char buf[16];

    int flushAfter = env->progressiveFlush ? env->numPubMessages / (env->numSubs * 3) : // trigger
                         env->numPubMessages + 1;                                       // do not trigger
    for (int i = 0; i < env->numPubMessages; i++)
    {
        snprintf(buf, sizeof(buf), "%d", i);
        IFOK(s, natsConnection_PublishString(nc, subject, buf));

        if (((i != 0) && ((i % flushAfter) == 0)) || // progressive flush
            (i == (env->numPubMessages - 1)))        // last message in batch
        {
            IFOK(s, natsConnection_Flush(nc));
        }
    }

    return s;
}

static natsStatus _inject(natsConnection *nc, const char *subject, ENV *env)
{
    natsStatus s = NATS_OK;
    natsMsg *m = NULL;
    char buf[16];

    for (int i = 0; i < env->numPubMessages; i++)
    {
        for (int n = 0; n < env->numSubs; n++)
        {
            snprintf(buf, sizeof(buf), "%d", i);

            s = natsMsg_Create(&m, subject, NULL, buf, (int)strlen(buf));
            natsSubscription *sub = env->subs[n].sub;
            nats_lockSubAndDispatcher(sub);
            IFOK(s, natsSub_enqueueUserMessage(sub, m));
            nats_unlockSubAndDispatcher(sub);
        }
    }

    return s;
}

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

static void _onError(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    int64_t dropped = 0;
    natsSubscription_GetDropped(sub, (int64_t *)&dropped);
    printf("Async error: sid:%" PRId64 ", dropped:%" PRId64 ": %u - %s\n", sub->sid, dropped, err, natsStatus_GetText(err));
}
