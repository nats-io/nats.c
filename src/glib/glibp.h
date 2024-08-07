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

#ifndef GLIBP_H_
#define GLIBP_H_

#include "../natsp.h"
#include "../mem.h"
#if defined(NATS_HAS_STREAMING)
#include "../stan/stanp.h"
#endif

#include <assert.h>
#include <stdarg.h>

#include "glib.h"

#define WAIT_LIB_INITIALIZED(_l)                         \
    natsMutex_Lock((_l)->lock);                               \
    while (!((_l)->initialized) && !((_l)->initAborted)) \
        natsCondition_Wait((_l)->cond, (_l)->lock);      \
    natsMutex_Unlock((_l)->lock)

typedef struct natsTLError
{
    natsStatus  sts;
    char        text[256];
    const char  *func[MAX_FRAMES];
    int         framesCount;
    int         skipUpdate;

} natsTLError;

typedef struct __natsLibTimers
{
    natsMutex *lock;
    natsCondition *cond;
    natsThread *thread;
    natsTimer *timers;
    int count;
    bool changed;
    bool shutdown;

} natsLibTimers;

typedef struct __natsLibAsyncCbs
{
    natsMutex *lock;
    natsCondition *cond;
    natsThread *thread;
    natsAsyncCbInfo *head;
    natsAsyncCbInfo *tail;
    bool shutdown;

} natsLibAsyncCbs;

typedef struct __natsGCList
{
    natsMutex *lock;
    natsCondition *cond;
    natsThread *thread;
    natsGCItem *head;
    bool shutdown;
    bool inWait;

} natsGCList;

struct __natsDispatcherPool
{
    natsMutex *lock;
    int useNext; // index of next dispatcher to use
    int cap;     // maximum number of concurrent dispatchers allowed

    natsDispatcher *dispatchers;

};

typedef struct __natsLib
{
    // Leave these fields before 'refs'
    natsMutex *lock;
    volatile bool wasOpenedOnce;
    bool sslInitialized;
    natsThreadLocal errTLKey;
    natsThreadLocal sslTLKey;
    natsThreadLocal natsThreadKey;
    bool initialized;
    bool closed;
    natsCondition *closeCompleteCond;
    bool *closeCompleteBool;
    bool closeCompleteSignal;
    bool finalCleanup;
    // Do not move 'refs' without checking _freeLib()
    int refs;

    bool initializing;
    bool initAborted;

    natsClientConfig config;
    natsDispatcherPool messageDispatchers;
    natsDispatcherPool replyDispatchers;

    natsLibTimers timers;
    natsLibAsyncCbs asyncCbs;

    natsCondition *cond;

    natsGCList gc;

    // For micro services code
    natsMutex *service_callback_mu;
    // uses `microService*` as the key and the value.
    natsHash *all_services_to_callback;

} natsLib;

natsLib *nats_lib(void);

void nats_freeTimers(natsLib *lib);
void nats_timerThreadf(void *arg); // arg is &gLib

void nats_freeAsyncCbs(natsLib *lib);
void nats_asyncCbsThreadf(void *arg); // arg is &gLib

void nats_freeGC(natsLib *lib);
void nats_garbageCollectorThreadf(void *arg); // arg is &gLib

natsStatus nats_initDispatcherPool(natsDispatcherPool *pool, int cap);
void nats_freeDispatcherPool(natsDispatcherPool *pool);
void nats_signalDispatcherPoolToShutdown(natsDispatcherPool *pool);
void nats_waitForDispatcherPoolShutdown(natsDispatcherPool *pool);

void nats_cleanupThreadSSL(void *localStorage);

#endif // GLIBP_H_
