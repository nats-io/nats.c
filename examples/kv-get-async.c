// Copyright 2026 The NATS Authors
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

// Puts a number of keys in a KeyValue bucket, then retrieves them all with
// kvStore_GetAsync(). The get callbacks run on a library thread and must not
// block, so they only hand the entries over to the main thread, through a
// small work queue, for processing.

#include "examples.h"

// Mutex and condition variable used by the work queue.
#ifdef _WIN32
#include <windows.h>
#define QUEUE_MUTEX_T           CRITICAL_SECTION
#define QUEUE_COND_T            CONDITION_VARIABLE
#define QUEUE_MUTEX_INIT(mu)    (InitializeCriticalSection(mu), 0)
#define QUEUE_COND_INIT(c)      (InitializeConditionVariable(c), 0)
#define QUEUE_MUTEX_DESTROY(mu) DeleteCriticalSection(mu)
#define QUEUE_COND_DESTROY(c)   ((void) 0)
#define QUEUE_LOCK(mu)          EnterCriticalSection(mu)
#define QUEUE_UNLOCK(mu)        LeaveCriticalSection(mu)
#define QUEUE_WAIT(c, mu)       SleepConditionVariableCS(c, mu, INFINITE)
#define QUEUE_SIGNAL(c)         WakeConditionVariable(c)
#else
#include <pthread.h>
#define QUEUE_MUTEX_T           pthread_mutex_t
#define QUEUE_COND_T            pthread_cond_t
#define QUEUE_MUTEX_INIT(mu)    pthread_mutex_init(mu, NULL)
#define QUEUE_COND_INIT(c)      pthread_cond_init(c, NULL)
#define QUEUE_MUTEX_DESTROY(mu) pthread_mutex_destroy(mu)
#define QUEUE_COND_DESTROY(c)   pthread_cond_destroy(c)
#define QUEUE_LOCK(mu)          pthread_mutex_lock(mu)
#define QUEUE_UNLOCK(mu)        pthread_mutex_unlock(mu)
#define QUEUE_WAIT(c, mu)       pthread_cond_wait(c, mu)
#define QUEUE_SIGNAL(c)         pthread_cond_signal(c)
#endif

static const char *usage = ""\
"-count         number of keys to put, then get asynchronously (default is 10)\n";

static const char *bucket = "kv-get-async-example";

// Outcome of one asynchronous get, filled in by the callback.
typedef struct getResult
{
    char                key[32];
    kvEntry             *entry;     // NULL unless status is NATS_OK
    natsStatus          status;
    struct getResult    *next;

} getResult;

// Thread-safe FIFO of get results, pushed by the callbacks and popped by the
// main thread.
typedef struct workQueue
{
    QUEUE_MUTEX_T       mu;
    QUEUE_COND_T        cond;
    getResult           *head;
    getResult           *tail;

} workQueue;

static workQueue queue;

static natsStatus
workQueue_Init(workQueue *q)
{
    q->head = NULL;
    q->tail = NULL;
    if (QUEUE_MUTEX_INIT(&q->mu) != 0)
        return NATS_ERR;
    if (QUEUE_COND_INIT(&q->cond) != 0)
    {
        QUEUE_MUTEX_DESTROY(&q->mu);
        return NATS_ERR;
    }
    return NATS_OK;
}

static void
workQueue_Destroy(workQueue *q)
{
    QUEUE_COND_DESTROY(&q->cond);
    QUEUE_MUTEX_DESTROY(&q->mu);
}

// Appends `r` and wakes up workQueue_Pop().
static void
workQueue_Push(workQueue *q, getResult *r)
{
    QUEUE_LOCK(&q->mu);
    r->next = NULL;
    if (q->tail != NULL)
        q->tail->next = r;
    else
        q->head = r;
    q->tail = r;
    QUEUE_SIGNAL(&q->cond);
    QUEUE_UNLOCK(&q->mu);
}

// Waits for a result and removes it from the queue.
static getResult*
workQueue_Pop(workQueue *q)
{
    getResult *r = NULL;

    QUEUE_LOCK(&q->mu);
    while ((r = q->head) == NULL)
        QUEUE_WAIT(&q->cond, &q->mu);
    q->head = r->next;
    if (q->head == NULL)
        q->tail = NULL;
    QUEUE_UNLOCK(&q->mu);

    return r;
}

// Invoked from a library thread, once per successful kvStore_GetAsync() call.
// Must not block: just records the outcome and hands it to the main thread.
static void
getCompleted(kvStore *kv, kvEntry *e, natsStatus s, void *closure)
{
    getResult *r = (getResult*) closure;

    // The entry (NULL if s != NATS_OK) is now ours to destroy.
    r->entry  = e;
    r->status = s;

    workQueue_Push(&queue, r);
}

int main(int argc, char **argv)
{
    natsConnection  *conn       = NULL;
    natsOptions     *opts       = NULL;
    jsCtx           *js         = NULL;
    kvStore         *kv         = NULL;
    getResult       *results    = NULL;
    bool            delBucket   = false;
    natsStatus      s;
    int64_t         numKeys     = 0;
    int64_t         pending     = 0;
    int64_t         found       = 0;
    int64_t         notFound    = 0;
    int64_t         errors      = 0;
    int64_t         i;

    // Default number of keys, unless overridden with -count.
    total = 10;
    opts = parseArgs(argc, argv, usage);
    numKeys = total;

    if (workQueue_Init(&queue) != NATS_OK)
    {
        printf("Unable to initialize the work queue\n");
        natsOptions_Destroy(opts);
        nats_Close();
        return 1;
    }

    // One more result than keys: the last get is for a missing key.
    results = (getResult*) calloc((size_t) numKeys + 1, sizeof(getResult));
    s = (results == NULL ? NATS_NO_MEMORY : NATS_OK);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
        s = natsConnection_JetStream(&js, conn, NULL);
    if (s == NATS_OK)
    {
        // Bind to the bucket, or create it (and delete it at the end).
        s = js_KeyValue(&kv, js, bucket);
        if (s == NATS_NOT_FOUND)
        {
            kvConfig kvc;

            delBucket = true;

            kvConfig_Init(&kvc);
            kvc.Bucket = bucket;
            s = js_CreateKeyValue(&kv, js, &kvc);
        }
    }

    // Populate the bucket.
    for (i = 0; (s == NATS_OK) && (i < numKeys); i++)
    {
        char value[64];

        snprintf(results[i].key, sizeof(results[i].key), "key-%" PRId64, i);
        snprintf(value, sizeof(value), "value-%" PRId64, i);
        s = kvStore_PutString(NULL, kv, results[i].key, value);
    }
    if (s == NATS_OK)
        snprintf(results[numKeys].key, sizeof(results[numKeys].key), "does-not-exist");

    // Start all the gets without waiting: the callback is invoked when the
    // response arrives, possibly before the call returns.
    if (s == NATS_OK)
        printf("Getting %" PRId64 " keys asynchronously\n\n", numKeys + 1);
    for (i = 0; (s == NATS_OK) && (i <= numKeys); i++)
    {
        getResult *r = &results[i];

        s = kvStore_GetAsync(kv, r->key, getCompleted, (void*) r);
        if (s == NATS_OK)
            pending++;
    }

    // Process the results as they arrive (not necessarily in the order of
    // the gets) until all the gets that were started have completed (with
    // NATS_TIMEOUT if the server does not respond).
    while (pending > 0)
    {
        getResult *r = workQueue_Pop(&queue);

        pending--;
        switch (r->status)
        {
            case NATS_OK:
                printf("%-16s revision %3" PRIu64 " value '%s'\n",
                       kvEntry_Key(r->entry), kvEntry_Revision(r->entry),
                       kvEntry_ValueString(r->entry));
                found++;
                break;
            case NATS_NOT_FOUND:
                printf("%-16s not found\n", r->key);
                notFound++;
                break;
            default:
                printf("%-16s error: %u - %s\n",
                       r->key, r->status, natsStatus_GetText(r->status));
                errors++;
                break;
        }
        // The entry (if any) is ours to destroy.
        kvEntry_Destroy(r->entry);
        r->entry = NULL;
    }
    if (found + notFound + errors > 0)
    {
        printf("\nFound: %" PRId64 " - Not found: %" PRId64 " - Errors: %" PRId64 "\n",
               found, notFound, errors);
    }

    if (s != NATS_OK)
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }
    if (delBucket)
    {
        printf("\nDeleting bucket %s: ", bucket);
        if (js_DeleteKeyValue(js, bucket) == NATS_OK)
            printf("OK!");
        printf("\n");
    }

    // Destroy all our objects to avoid report of memory leak. Destroying the
    // context completes any pending get (with NATS_ILLEGAL_STATE), which
    // pushes to the queue, so the queue and the results must outlive it.
    kvStore_Destroy(kv);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);
    free(results);
    workQueue_Destroy(&queue);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
