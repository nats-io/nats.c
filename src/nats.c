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

#include "natsp.h"
#if defined(NATS_HAS_STREAMING)
#include "stan/stanp.h"
#endif

#include "mem.h"
#include "glib/glib.h"
#include "sub.h"
#include "conn.h"

#if defined(_WIN32) && _WIN32
#ifndef NATS_STATIC
BOOL WINAPI DllMain(HINSTANCE hinstDLL, // DLL module handle
     DWORD fdwReason,                   // reason called
     LPVOID lpvReserved)                // reserved
{
    switch (fdwReason)
    {
        // For applications linking dynamically NATS library,
        // release thread-local memory for user-created threads.
        // For portable applications, the user should manually call
        // nats_ReleaseThreadMemory() before the thread returns so
        // that no memory is leaked regardless if they link statically
        // or dynamically. It is safe to call nats_ReleaseThreadMemory()
        // twice for the same threads.
        case DLL_THREAD_DETACH:
        {
            nats_ReleaseThreadMemory();
            break;
        }
        default:
            break;
    }

    return TRUE;
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(lpvReserved);
}
#endif
#endif

static void _overwriteInt64(const char *envVar, int64_t *val)
{
    char *str = getenv(envVar);
    if (str != NULL)
        *val = atoll(str);
}

static void _overwriteInt(const char *envVar, int *val)
{
    char *str = getenv(envVar);
    if (str != NULL)
        *val = atoi(str);
}

static void _overwriteBool(const char *envVar, bool *val)
{
    char *str = getenv(envVar);
    if (str != NULL)
        *val = (strcasecmp(str, "true") == 0) ||
               (strcasecmp(str, "on") == 0) ||
               (atoi(str) != 0);
}

static void _overrideWithEnv(natsClientConfig *config)
{
    _overwriteInt64("NATS_DEFAULT_LIB_WRITE_DEADLINE", &config->DefaultWriteDeadline);
    _overwriteBool("NATS_USE_THREAD_POOL", &config->DefaultToThreadPool);
    _overwriteInt("NATS_THREAD_POOL_MAX", &config->ThreadPoolMax);
    _overwriteBool("NATS_USE_THREAD_POOL_FOR_REPLIES", &config->DefaultRepliesToThreadPool);
    _overwriteInt("NATS_REPLY_THREAD_POOL_MAX", &config->ReplyThreadPoolMax);
}

// environment variables will override the default options.
natsStatus
nats_OpenWithConfig(natsClientConfig *config)
{
    return nats_openLib(config);
}

natsStatus
nats_Open(int64_t lockSpinCount)
{
    bool defaultToSharedDispatchers = (getenv("NATS_DEFAULT_TO_LIB_MSG_DELIVERY") != NULL ? true : false);
    
    natsClientConfig config = {
        .LockSpinCount = lockSpinCount,
        .DefaultToThreadPool = defaultToSharedDispatchers,
        .ThreadPoolMax = 1,
        .DefaultRepliesToThreadPool = false,
        .ReplyThreadPoolMax = 0,
    };

    return nats_openLib(&config);
}

natsStatus
natsInbox_Create(natsInbox **newInbox)
{
    natsStatus  s;
    char        *inbox = NULL;
    const int   size   = NATS_DEFAULT_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1;

    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    inbox = NATS_MALLOC(size);
    if (inbox == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(inbox, NATS_DEFAULT_INBOX_PRE, NATS_DEFAULT_INBOX_PRE_LEN);
    s = natsNUID_Next(inbox + NATS_DEFAULT_INBOX_PRE_LEN, NUID_BUFFER_LEN + 1);
    if (s == NATS_OK)
    {
        inbox[size-1] = '\0';
        *newInbox = (natsInbox*) inbox;
    }
    else
        NATS_FREE(inbox);
    return NATS_UPDATE_ERR_STACK(s);
}

void
natsInbox_Destroy(natsInbox *inbox)
{
    if (inbox == NULL)
        return;

    NATS_FREE(inbox);
}

void
nats_Close(void)
{
    nats_closeLib(false, true);
}

natsStatus
nats_CloseAndWait(int64_t timeout)
{
    return nats_closeLib(true, timeout);
}

const char*
nats_GetVersion(void)
{
    return LIB_NATS_VERSION_STRING;
}

uint32_t
nats_GetVersionNumber(void)
{
    return LIB_NATS_VERSION_NUMBER;
}

static void
_versionGetString(char *buffer, size_t bufLen, uint32_t verNumber)
{
    snprintf(buffer, bufLen, "%u.%u.%u",
             ((verNumber >> 16) & 0xF),
             ((verNumber >> 8) & 0xF),
             (verNumber & 0xF));
}

bool
nats_CheckCompatibilityImpl(uint32_t headerReqVerNumber, uint32_t headerVerNumber,
                            const char *headerVerString)
{
    if ((headerVerNumber < LIB_NATS_VERSION_REQUIRED_NUMBER)
        || (headerReqVerNumber > LIB_NATS_VERSION_NUMBER))
    {
        char reqVerString[10];
        char libReqVerString[10];

        _versionGetString(reqVerString, sizeof(reqVerString), headerReqVerNumber);
        _versionGetString(libReqVerString, sizeof(libReqVerString), NATS_VERSION_REQUIRED_NUMBER);

        printf("Incompatible versions:\n" \
               "Header : %s (requires %s)\n" \
               "Library: %s (requires %s)\n",
               headerVerString, reqVerString,
               NATS_VERSION_STRING, libReqVerString);
        exit(1);
    }

    return true;
}

void
nats_deliverMsgsPoolf(void *arg)
{
    natsDispatcher *d = (natsDispatcher *)arg;
    natsConnection *nc;
    natsSubscription *sub;
    natsMsgHandler mcb;
    void *mcbClosure;
    uint64_t delivered;
    uint64_t max;
    natsMsg *msg;
    bool timerNeedReset = false;
    jsSub *jsi;
    char *fcReply;

    natsMutex_Lock(d->mu);

    while (true)
    {
        while (((msg = d->queue.head) == NULL) && !d->shutdown)
            natsCondition_Wait(d->cond, d->mu);

        // Break out only when list is empty
        if ((msg == NULL) && d->shutdown)
        {
            break;
        }

        // Remove message from list now...
        d->queue.head = msg->next;
        if (d->queue.tail == msg)
            d->queue.tail = NULL;
        msg->next = NULL;

        // Get subscription reference from message
        sub = msg->sub;

        // Capture these under lock
        nc = sub->conn;
        mcb = sub->msgCb;
        mcbClosure = sub->msgCbClosure;
        max = sub->max;

        // Is this a control message?
        if (msg->subject[0] == '\0')
        {
            bool closed = (msg == sub->control->sub.close);
            bool timedOut = (msg == sub->control->sub.timeout);
            bool draining = (msg == sub->control->sub.drain);

            // We need to release this lock...
            natsMutex_Unlock(d->mu);

            // Release the message
            natsMsg_Destroy(msg);

            if (draining)
            {
                // Subscription is draining, we are past the last message,
                // remove the subscription. This will schedule another
                // control message for the close.
                natsSub_setDrainCompleteState(sub);
                natsConn_removeSubscription(nc, sub);
            }
            else if (closed)
            {
                natsOnCompleteCB cb = NULL;
                void *closure = NULL;

                // Call this in case the subscription was draining.
                natsSub_setDrainCompleteState(sub);

                // Check for completion callback
                natsSub_Lock(sub);
                cb = sub->onCompleteCB;
                closure = sub->onCompleteCBClosure;
                natsSub_Unlock(sub);

                if (cb != NULL)
                    (*cb)(closure);

                // Subscription closed, just release
                natsSub_release(sub);
            }
            else if (timedOut)
            {
                // Invoke the callback with a NULL message.
                (*mcb)(nc, sub, NULL, mcbClosure);
            }

            // Grab the lock, we go back to beginning of loop.
            natsMutex_Lock(d->mu);

            if (!draining && !closed && timedOut)
            {
                // Reset the timedOut boolean to allow for the
                // subscription to timeout again, and reset the
                // timer to fire again starting from now.
                sub->timedOut = false;
                natsTimer_Reset(sub->timeoutTimer, sub->timeout);
            }

            // Go back to top of loop.
            continue;
        }

        // Update the sub's stats before checking closed state. (We now post
        // control messages to the sub's queue, because hbTimer processing is
        // expecting it, so need to clear the stats for them, too)
        sub->ownDispatcher.queue.msgs--;
        sub->ownDispatcher.queue.bytes -= natsMsg_dataAndHdrLen(msg);

        // Need to check for closed subscription again here.
        // The subscription could have been unsubscribed from a callback
        // but there were already pending messages. The control message
        // is queued up. Until it is processed, we need to simply
        // discard the message and continue.
        if (sub->closed)
        {
            natsMsg_Destroy(msg);
            continue;
        }

        delivered = ++(sub->delivered);

        jsi = sub->jsi;
        fcReply = (jsi == NULL ? NULL : jsSub_checkForFlowControlResponse(sub));

        // Is this a subscription that can timeout?
        if (!sub->draining && (sub->timeout != 0))
        {
            // Prevent the timer to post a timeout control message
            sub->timeoutSuspended = true;

            // If we are dealing with the last pending message for this sub,
            // we will reset the timer after the user callback returns.
            if (sub->ownDispatcher.queue.msgs == 0)
                timerNeedReset = true;
        }

        natsMutex_Unlock(d->mu);

        if ((max == 0) || (delivered <= max))
        {
            (*mcb)(nc, sub, msg, mcbClosure);
        }
        else
        {
            // We need to destroy the message since the user can't do it
            natsMsg_Destroy(msg);
        }

        if (fcReply != NULL)
        {
            natsConnection_Publish(nc, fcReply, NULL, 0);
            NATS_FREE(fcReply);
        }

        // Don't do 'else' because we need to remove when we have hit
        // the max (after the callback returns).
        if ((max > 0) && (delivered >= max))
        {
            // Call this blindly, it will be a no-op if the subscription was not draining.
            natsSub_setDrainCompleteState(sub);
            // If we have hit the max for delivered msgs, remove sub.
            natsConn_removeSubscription(nc, sub);
        }

        natsMutex_Lock(d->mu);

        // Check if timer need to be reset for subscriptions that can timeout.
        if (!sub->closed && (sub->timeout != 0) && timerNeedReset)
        {
            timerNeedReset = false;

            // Do this only on timer reset instead of after each return
            // from callback. The reason is that if there are still pending
            // messages for this subscription (this is the case otherwise
            // timerNeedReset would be false), we should prevent
            // the subscription to timeout anyway.
            sub->timeoutSuspended = false;

            // Reset the timer to fire in `timeout` from now.
            natsTimer_Reset(sub->timeoutTimer, sub->timeout);
        }
    }

    natsMutex_Unlock(d->mu);

    natsLib_Release();
}

natsStatus
nats_SetMessageDeliveryPoolSize(int max)
{
    natsStatus          s = NATS_OK;

    // Ensure the library is loaded
    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    s = nats_setMessageDispatcherPoolCap(max);
    return NATS_UPDATE_ERR_STACK(s);
}
