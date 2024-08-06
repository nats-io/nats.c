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

#include "glibp.h"

#include "../util.h"
#include "../crypto.h"

int64_t gLockSpinCount = 2000;

static natsInitOnceType gInitOnce = NATS_ONCE_STATIC_INIT;
static natsLib gLib;

natsLib* nats_lib(void)
{
    return &gLib;
}

static void _freeLib(void);
static void _finalCleanup(void);

void natsLib_Retain(void)
{
    natsMutex_Lock(gLib.lock);

    gLib.refs++;

    natsMutex_Unlock(gLib.lock);
}

void natsLib_Release(void)
{
    int refs = 0;

    natsMutex_Lock(gLib.lock);

    refs = --(gLib.refs);

    natsMutex_Unlock(gLib.lock);

    if (refs == 0)
        _freeLib();
}

static void
_finalCleanup(void)
{
    if (gLib.sslInitialized)
    {
#if defined(NATS_HAS_TLS)
#if !defined(NATS_USE_OPENSSL_1_1)
        ERR_free_strings();
        EVP_cleanup();
        CRYPTO_cleanup_all_ex_data();
        ERR_remove_thread_state(0);
#endif
        sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
#endif
        natsThreadLocal_DestroyKey(gLib.sslTLKey);
    }

    natsThreadLocal_DestroyKey(gLib.errTLKey);
    natsThreadLocal_DestroyKey(gLib.natsThreadKey);
    natsMutex_Destroy(gLib.lock);
    gLib.lock = NULL;
}

static void
natsLib_Destructor(void)
{
    int refs = 0;

    if (!(gLib.wasOpenedOnce))
        return;

    // Destroy thread locals for the current thread.
    nats_ReleaseThreadMemory();

    // Do the final cleanup if possible
    natsMutex_Lock(gLib.lock);
    refs = gLib.refs;
    if (refs > 0)
    {
        // If some thread is still around when the process exits and has a
        // reference to the library, then don't do the final cleanup now.
        // If the process has not fully exited when the lib's last reference
        // is decremented, the final cleanup will be executed from that thread.
        gLib.finalCleanup = true;
    }
    natsMutex_Unlock(gLib.lock);

    if (refs != 0)
        return;

    _finalCleanup();
}

static void
_freeLib(void)
{
    const unsigned int offset = (unsigned int)offsetof(natsLib, refs);
    bool callFinalCleanup = false;

    nats_freeTimers(&gLib);
    nats_freeAsyncCbs(&gLib);
    nats_freeGC(&gLib);

    nats_freeDispatcherPool(&gLib.messageDispatchers);
    nats_freeDispatcherPool(&gLib.replyDispatchers);

    natsNUID_free();
    natsMutex_Destroy(gLib.service_callback_mu);
    natsHash_Destroy(gLib.all_services_to_callback);

    natsCondition_Destroy(gLib.cond);

    memset((void *)(offset + (char *)&gLib), 0, sizeof(natsLib) - offset);

    natsMutex_Lock(gLib.lock);
    callFinalCleanup = gLib.finalCleanup;
    if (gLib.closeCompleteCond != NULL)
    {
        if (gLib.closeCompleteSignal)
        {
            *gLib.closeCompleteBool = true;
            natsCondition_Signal(gLib.closeCompleteCond);
        }
        gLib.closeCompleteCond = NULL;
        gLib.closeCompleteBool = NULL;
        gLib.closeCompleteSignal = false;
    }
    gLib.closed = false;
    gLib.initialized = false;
    gLib.finalCleanup = false;
    natsMutex_Unlock(gLib.lock);

    if (callFinalCleanup)
        _finalCleanup();
}

static void
_destroyErrTL(void *localStorage)
{
    natsTLError *err = (natsTLError *)localStorage;
    NATS_FREE(err);
}

static void
_doInitOnce(void)
{
    natsStatus s;

    memset(&gLib, 0, sizeof(natsLib));

    s = natsMutex_Create(&(gLib.lock));
    if (s == NATS_OK)
        s = natsThreadLocal_CreateKey(&(gLib.errTLKey), _destroyErrTL);
    if (s == NATS_OK)
        s = natsThreadLocal_CreateKey(&(gLib.natsThreadKey), NULL);
    if (s != NATS_OK)
    {
        fprintf(stderr, "FATAL ERROR: Unable to initialize library!\n");
        fflush(stderr);
        abort();
    }

    nats_initForOS();

    // Setup a hook for when the process exits.
    atexit(natsLib_Destructor);
}


static void
_libTearDown(void)
{
    nats_waitForDispatcherPoolShutdown(&gLib.messageDispatchers);
    nats_waitForDispatcherPoolShutdown(&gLib.replyDispatchers);

    if (gLib.timers.thread != NULL)
        natsThread_Join(gLib.timers.thread);

    if (gLib.asyncCbs.thread != NULL)
        natsThread_Join(gLib.asyncCbs.thread);

    if (gLib.gc.thread != NULL)
        natsThread_Join(gLib.gc.thread);

    natsLib_Release();
}

// environment variables will override the default options.
natsStatus
nats_openLib(natsClientConfig *config)
{
    natsStatus s = NATS_OK;

    natsClientConfig defaultConfig = {
        .LockSpinCount = -1,
        .ThreadPoolMax = 1,
    };
    if (config == NULL)
        config = &defaultConfig;

    if (!nats_InitOnce(&gInitOnce, _doInitOnce))
        return NATS_FAILED_TO_INITIALIZE;

    natsMutex_Lock(gLib.lock);

    if (gLib.closed || gLib.initialized || gLib.initializing)
    {
        if (gLib.closed)
            s = NATS_FAILED_TO_INITIALIZE;
        else if (gLib.initializing)
            s = NATS_ILLEGAL_STATE;

        natsMutex_Unlock(gLib.lock);
        return s;
    }

    gLib.initializing = true;
    gLib.initAborted = false;

#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif

    srand((unsigned int)nats_NowInNanoSeconds());

    gLib.refs = 1;

    // If the caller specifies negative value, then we use the default
    if (config->LockSpinCount >= 0)
        gLockSpinCount = config->LockSpinCount;

    gLib.config = *config;
    nats_Base32_Init();

    s = natsCondition_Create(&(gLib.cond));

    if (s == NATS_OK)
        s = natsCrypto_Init();

    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.timers.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.timers.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.timers.thread), nats_timerThreadf, &gLib);
        if (s == NATS_OK)
            gLib.refs++;
    }

    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.asyncCbs.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.asyncCbs.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.asyncCbs.thread), nats_asyncCbsThreadf, &gLib);
        if (s == NATS_OK)
            gLib.refs++;
    }
    if (s == NATS_OK)
        s = natsMutex_Create(&(gLib.gc.lock));
    if (s == NATS_OK)
        s = natsCondition_Create(&(gLib.gc.cond));
    if (s == NATS_OK)
    {
        s = natsThread_Create(&(gLib.gc.thread), nats_garbageCollectorThreadf, &gLib);
        if (s == NATS_OK)
            gLib.refs++;
    }
    if (s == NATS_OK)
        s = natsNUID_init();

    if (s == NATS_OK)
        s = nats_initDispatcherPool(&(gLib.messageDispatchers), config->ThreadPoolMax);
    if (s == NATS_OK)
        s = nats_initDispatcherPool(&(gLib.replyDispatchers), config->ReplyThreadPoolMax);

    if (s == NATS_OK)
        s = natsMutex_Create(&gLib.service_callback_mu);
    if (s == NATS_OK)
        s = natsHash_Create(&gLib.all_services_to_callback, 8);

    if (s == NATS_OK)
        gLib.initialized = true;

    // In case of success or error, broadcast so that lib's threads
    // can proceed.
    if (gLib.cond != NULL)
    {
        if (s != NATS_OK)
        {
            gLib.initAborted = true;
            gLib.timers.shutdown = true;
            gLib.asyncCbs.shutdown = true;
            gLib.gc.shutdown = true;
        }
        natsCondition_Broadcast(gLib.cond);
    }

    gLib.initializing = false;
    gLib.wasOpenedOnce = true;

    natsMutex_Unlock(gLib.lock);

    if (s != NATS_OK)
        _libTearDown();

    return s;
}

natsStatus
nats_closeLib(bool wait, int64_t timeout)
{
    natsStatus s = NATS_OK;
    natsCondition *cond = NULL;
    bool complete = false;
    // int             i;

    // This is to protect against a call to nats_Close() while there
    // was no prior call to nats_Open(), either directly or indirectly.
    if (!nats_InitOnce(&gInitOnce, _doInitOnce))
        return NATS_ERR;

    natsMutex_Lock(gLib.lock);

    if (gLib.closed || !gLib.initialized)
    {
        bool closed = gLib.closed;

        natsMutex_Unlock(gLib.lock);

        if (closed)
            return NATS_ILLEGAL_STATE;
        return NATS_NOT_INITIALIZED;
    }
    if (wait)
    {
        if (natsThreadLocal_Get(gLib.natsThreadKey) != NULL)
            s = NATS_ILLEGAL_STATE;
        if (s == NATS_OK)
            s = natsCondition_Create(&cond);
        if (s != NATS_OK)
        {
            natsMutex_Unlock(gLib.lock);
            return s;
        }
        gLib.closeCompleteCond = cond;
        gLib.closeCompleteBool = &complete;
        gLib.closeCompleteSignal = true;
    }

    gLib.closed = true;

    natsMutex_Lock(gLib.timers.lock);
    gLib.timers.shutdown = true;
    natsCondition_Signal(gLib.timers.cond);
    natsMutex_Unlock(gLib.timers.lock);

    natsMutex_Lock(gLib.asyncCbs.lock);
    gLib.asyncCbs.shutdown = true;
    natsCondition_Signal(gLib.asyncCbs.cond);
    natsMutex_Unlock(gLib.asyncCbs.lock);

    natsMutex_Lock(gLib.gc.lock);
    gLib.gc.shutdown = true;
    natsCondition_Signal(gLib.gc.cond);
    natsMutex_Unlock(gLib.gc.lock);

    natsMutex_Unlock(gLib.lock);

    nats_signalDispatcherPoolToShutdown(&gLib.messageDispatchers);
    nats_signalDispatcherPoolToShutdown(&gLib.replyDispatchers);

    nats_ReleaseThreadMemory();
    _libTearDown();

    if (wait)
    {
        natsMutex_Lock(gLib.lock);
        while ((s != NATS_TIMEOUT) && !complete)
        {
            if (timeout <= 0)
                natsCondition_Wait(cond, gLib.lock);
            else
                s = natsCondition_TimedWait(cond, gLib.lock, timeout);
        }
        if (s != NATS_OK)
            gLib.closeCompleteSignal = false;
        natsMutex_Unlock(gLib.lock);

        natsCondition_Destroy(cond);
    }

    return s;
}

void nats_setNATSThreadKey(void)
{
    natsThreadLocal_Set(gLib.natsThreadKey, (const void *)1);
}

void nats_ReleaseThreadMemory(void)
{
    void *tl = NULL;

    if (!(gLib.wasOpenedOnce))
        return;

    tl = natsThreadLocal_Get(gLib.errTLKey);
    if (tl != NULL)
    {
        _destroyErrTL(tl);
        natsThreadLocal_SetEx(gLib.errTLKey, NULL, false);
    }

    tl = NULL;

    natsMutex_Lock(gLib.lock);
    if (gLib.sslInitialized)
    {
        tl = natsThreadLocal_Get(gLib.sslTLKey);
        if (tl != NULL)
        {
            nats_cleanupThreadSSL(tl);
            natsThreadLocal_SetEx(gLib.sslTLKey, NULL, false);
        }
    }
    natsMutex_Unlock(gLib.lock);
}

natsStatus
natsLib_startServiceCallbacks(microService *m)
{
    natsStatus s;

    natsMutex_Lock(gLib.service_callback_mu);
    s = natsHash_Set(gLib.all_services_to_callback, (int64_t)m, (void *)m, NULL);
    natsMutex_Unlock(gLib.service_callback_mu);

    return NATS_UPDATE_ERR_STACK(s);
}

void natsLib_stopServiceCallbacks(microService *m)
{
    if (m == NULL)
        return;

    natsMutex_Lock(gLib.service_callback_mu);
    natsHash_Remove(gLib.all_services_to_callback, (int64_t)m);
    natsMutex_Unlock(gLib.service_callback_mu);
}

natsMutex *
natsLib_getServiceCallbackMutex(void)
{
    return gLib.service_callback_mu;
}

natsHash *
natsLib_getAllServicesToCallback(void)
{
    return gLib.all_services_to_callback;
}

natsClientConfig *nats_testInspectClientConfig(void)
{
    // Immutable after startup
    return &gLib.config;
}

void nats_overrideDefaultOptionsWithConfig(natsOptions *opts)
{
    opts->writeDeadline = gLib.config.DefaultWriteDeadline;
    opts->useSharedDispatcher = gLib.config.DefaultToThreadPool;
    opts->useSharedReplyDispatcher = gLib.config.DefaultRepliesToThreadPool;
}
