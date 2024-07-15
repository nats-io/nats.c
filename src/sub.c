// Copyright 2015-2021 The NATS Authors
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

#include <string.h>
#include <stdio.h>

#include "mem.h"
#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "util.h"
#include "js.h"
#include "opts.h"

#ifdef DEV_MODE

static void _retain(natsSubscription *sub)   { sub->refs++; }
static void _release(natsSubscription *sub)  { sub->refs--; }

void natsSub_Lock(natsSubscription *sub)     { natsMutex_Lock(sub->mu);   }
void natsSub_Unlock(natsSubscription *sub)   { natsMutex_Unlock(sub->mu); }

#else

#define _retain(s)  ((s)->refs++)
#define _release(s) ((s)->refs--)

#endif // DEV_MODE

#define SUB_DLV_WORKER_LOCK(s)      if ((s)->libDlvWorker != NULL) \
                                        natsMutex_Lock((s)->libDlvWorker->lock)

#define SUB_DLV_WORKER_UNLOCK(s)    if ((s)->libDlvWorker != NULL) \
                                        natsMutex_Unlock((s)->libDlvWorker->lock)

bool testDrainAutoUnsubRace = false;

static void
_freeSubscription(natsSubscription *sub)
{
    natsMsg *m;

    if (sub == NULL)
        return;

    while ((m = sub->msgList.head) != NULL)
    {
        sub->msgList.head = m->next;
        natsMsg_Destroy(m);
    }

    NATS_FREE(sub->subject);
    NATS_FREE(sub->queue);

    if (sub->deliverMsgsThread != NULL)
    {
        natsThread_Detach(sub->deliverMsgsThread);
        natsThread_Destroy(sub->deliverMsgsThread);
    }
    natsTimer_Destroy(sub->timeoutTimer);
    natsCondition_Destroy(sub->cond);
    natsMutex_Destroy(sub->mu);
    jsSub_free(sub->jsi);

    natsConn_release(sub->conn);

    NATS_FREE(sub);
}

void
natsSub_retain(natsSubscription *sub)
{
    natsSub_Lock(sub);

    sub->refs++;

    natsSub_Unlock(sub);
}

void
natsSub_release(natsSubscription *sub)
{
    int refs = 0;

    if (sub == NULL)
        return;

    natsSub_Lock(sub);

    refs = --(sub->refs);

    natsSub_Unlock(sub);

    if (refs == 0)
        _freeSubscription(sub);
}

void
natsSubAndLdw_Lock(natsSubscription *sub)
{
    natsMutex_Lock(sub->mu);
    SUB_DLV_WORKER_LOCK(sub);
}

void
natsSubAndLdw_LockAndRetain(natsSubscription *sub)
{
    natsMutex_Lock(sub->mu);
    sub->refs++;
    SUB_DLV_WORKER_LOCK(sub);
}

void
natsSubAndLdw_Unlock(natsSubscription *sub)
{
    SUB_DLV_WORKER_UNLOCK(sub);
    natsMutex_Unlock(sub->mu);
}

void
natsSubAndLdw_UnlockAndRelease(natsSubscription *sub)
{
    int refs = 0;

    SUB_DLV_WORKER_UNLOCK(sub);

    refs = --(sub->refs);
    natsMutex_Unlock(sub->mu);

    if (refs == 0)
        _freeSubscription(sub);
}

// Runs under the subscription lock but will release it for a JS subscription
// if the JS consumer needs to be deleted.
static void
_setDrainCompleteState(natsSubscription *sub)
{
    // It is possible that we are here without being in "drain in progress"
    // or event "started" due to auto-unsubscribe. So unless we already
    // switched to "drain complete", swith the state.
    if (!natsSub_drainComplete(sub))
    {
        // For JS subscription we may need to delete the JS consumer, but
        // we want to do so here ONLY if there was really a drain started.
        // So need to check on drain started state. Also, note that if
        // jsSub_deleteConsumerAfterDrain is invoked, the lock may be
        // released/reacquired in that function.
        if ((sub->jsi != NULL) && natsSub_drainStarted(sub) && sub->jsi->dc)
        {
            jsSub_deleteConsumerAfterDrain(sub);
            // Check drainCompete state again, since another thread may have
            // beat us to it while lock was released.
            if (natsSub_drainComplete(sub))
                return;
        }

        // If drain status is not already set (could be done in _flushAndDrain
        // if flush fails, or timeout occurs), set it here to report if the
        // connection or subscription has been closed prior to drain completion.
        if (sub->drainStatus == NATS_OK)
        {
            if (sub->connClosed)
                sub->drainStatus = NATS_CONNECTION_CLOSED;
            else if (sub->closed)
                sub->drainStatus = NATS_INVALID_SUBSCRIPTION;
        }
        sub->drainState |= SUB_DRAIN_COMPLETE;
        natsCondition_Broadcast(sub->cond);
    }
}

void
natsSub_setDrainCompleteState(natsSubscription *sub)
{
    natsSub_Lock(sub);
    _setDrainCompleteState(sub);
    natsSub_Unlock(sub);
}

// _deliverMsgs is used to deliver messages to asynchronous subscribers.
void
natsSub_deliverMsgs(void *arg)
{
    natsSubscription    *sub        = (natsSubscription*) arg;
    natsConnection      *nc         = sub->conn;
    natsMsgHandler      mcb         = sub->msgCb;
    void                *mcbClosure = sub->msgCbClosure;
    uint64_t            delivered;
    uint64_t            max;
    natsMsg             *msg;
    int64_t             timeout;
    natsStatus          s = NATS_OK;
    bool                draining = false;
    bool                rmSub    = false;
    natsOnCompleteCB    onCompleteCB = NULL;
    void                *onCompleteCBClosure = NULL;
    char                *fcReply = NULL;
    jsSub               *jsi = NULL;

    // This just serves as a barrier for the creation of this thread.
    natsConn_Lock(nc);
    natsConn_Unlock(nc);

    natsSub_Lock(sub);
    timeout = sub->timeout;
    jsi = sub->jsi;
    natsSub_Unlock(sub);

    while (true)
    {
        natsSub_Lock(sub);

        s = NATS_OK;
        while (((msg = sub->msgList.head) == NULL) && !(sub->closed) && !(sub->draining) && (s != NATS_TIMEOUT))
        {
            if (timeout != 0)
                s = natsCondition_TimedWait(sub->cond, sub->mu, timeout);
            else
                natsCondition_Wait(sub->cond, sub->mu);
        }

        if (sub->closed)
        {
            natsSub_Unlock(sub);
            break;
        }
        draining = sub->draining;

        // Will happen with timeout subscription
        if (msg == NULL)
        {
            natsSub_Unlock(sub);
            if (draining)
            {
                rmSub = true;
                break;
            }
            // If subscription timed-out, invoke callback with NULL message.
            if (s == NATS_TIMEOUT)
                (*mcb)(nc, sub, NULL, mcbClosure);
            continue;
        }

        delivered = ++(sub->delivered);

        sub->msgList.head = msg->next;

        if (sub->msgList.tail == msg)
            sub->msgList.tail = NULL;

        sub->msgList.msgs--;
        sub->msgList.bytes -= natsMsg_dataAndHdrLen(msg);

        msg->next = NULL;

        // Capture this under lock.
        max = sub->max;

        // Check for JS flow control
        fcReply = (jsi == NULL ? NULL : jsSub_checkForFlowControlResponse(sub));

        natsSub_Unlock(sub);

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
            // If we have hit the max for delivered msgs, remove sub.
            rmSub = true;
            break;
        }
    }

    natsSub_Lock(sub);
    onCompleteCB        = sub->onCompleteCB;
    onCompleteCBClosure = sub->onCompleteCBClosure;
    _setDrainCompleteState(sub);
    natsSub_Unlock(sub);

    if (rmSub)
        natsConn_removeSubscription(nc, sub);

    if (onCompleteCB != NULL)
        (*onCompleteCB)(onCompleteCBClosure);

    natsSub_release(sub);
}

bool
natsSub_setMax(natsSubscription *sub, uint64_t max)
{
    bool accepted = false;

    natsSub_Lock(sub);
    SUB_DLV_WORKER_LOCK(sub);
    sub->max = (max <= sub->delivered ? 0 : max);
    accepted = sub->max != 0;
    SUB_DLV_WORKER_UNLOCK(sub);
    natsSub_Unlock(sub);
    return accepted;
}

natsStatus
natsSubscription_SetOnCompleteCB(natsSubscription *sub, natsOnCompleteCB cb, void *closure)
{
    natsStatus s = NATS_OK;

    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);
    if ((sub->closed) || (sub->msgCb == NULL))
        s = nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    else
    {
        sub->onCompleteCB = cb;
        sub->onCompleteCBClosure = closure;
    }
    natsSub_Unlock(sub);

    return s;
}

void
natsSub_close(natsSubscription *sub, bool connectionClosed)
{
    natsSub_Lock(sub);

    SUB_DLV_WORKER_LOCK(sub);

    if (!(sub->closed))
    {
        sub->closed = true;
        sub->connClosed = connectionClosed;

        if ((sub->jsi != NULL) && (sub->jsi->hbTimer != NULL))
            natsTimer_Stop(sub->jsi->hbTimer);

        if (sub->libDlvWorker != NULL)
        {
            // If this is a subscription with timeout, stop the timer.
            if (sub->timeout != 0)
                natsTimer_Stop(sub->timeoutTimer);

            // Post a control message to wake-up the worker which will
            // ensure that all pending messages for this subscription
            // are removed and the subscription will ultimately be
            // released in the worker thread.
            natsLib_msgDeliveryPostControlMsg(sub);
        }
        else
            natsCondition_Broadcast(sub->cond);
    }

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);
}

static void
_asyncTimeoutCb(natsTimer *timer, void* closure)
{
    natsSubscription *sub = (natsSubscription*) closure;

    // Should not happen, but in case
    if (sub->libDlvWorker == NULL)
        return;

    SUB_DLV_WORKER_LOCK(sub);

    // If the subscription is closed, or if we are prevented from posting
    // a "timeout" control message, do nothing.
    if (!sub->closed && !sub->timedOut && !sub->timeoutSuspended)
    {
        // Prevent from scheduling another control message while we are not
        // done with previous one.
        sub->timedOut = true;

        // Set the timer to a very high value, it will be reset from the
        // worker thread.
        natsTimer_Reset(sub->timeoutTimer, 60*60*1000);

        // Post a control message to the worker thread.
        natsLib_msgDeliveryPostControlMsg(sub);
    }

    SUB_DLV_WORKER_UNLOCK(sub);
}

static void
_asyncTimeoutStopCb(natsTimer *timer, void* closure)
{
    natsSubscription *sub = (natsSubscription*) closure;

    natsSub_release(sub);
}

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, int64_t timeout, natsMsgHandler cb, void *cbClosure,
               bool preventUseOfLibDlvPool, jsSub *jsi)
{
    natsStatus          s = NATS_OK;
    natsSubscription    *sub = NULL;

    sub = (natsSubscription*) NATS_CALLOC(1, sizeof(natsSubscription));
    if (sub == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = natsMutex_Create(&(sub->mu));
    if (s != NATS_OK)
    {
        NATS_FREE(sub);
        return NATS_UPDATE_ERR_STACK(s);
    }

    natsConn_retain(nc);

    sub->refs           = 1;
    sub->conn           = nc;
    sub->timeout        = timeout;
    sub->msgCb          = cb;
    sub->msgCbClosure   = cbClosure;
    sub->msgsLimit      = nc->opts->maxPendingMsgs;
    sub->bytesLimit     = nc->opts->maxPendingBytes == -1 ? nc->opts->maxPendingMsgs * 1024 : nc->opts->maxPendingBytes;;
    sub->jsi            = jsi;

    sub->subject = NATS_STRDUP(subj);
    if (sub->subject == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if ((s == NATS_OK) && (queueGroup != NULL) && (strlen(queueGroup) > 0))
    {
        sub->queue = NATS_STRDUP(queueGroup);
        if (sub->queue == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    if (s == NATS_OK)
        s = natsCondition_Create(&(sub->cond));
    if ((s == NATS_OK) && (cb != NULL))
    {
        if (!(nc->opts->libMsgDelivery) || preventUseOfLibDlvPool)
        {
            // Let's not rely on the created thread acquiring the lock that
            // would make it safe to retain only on success.
            _retain(sub);

            // If we have an async callback, start up a sub specific
            // thread to deliver the messages.
            s = natsThread_Create(&(sub->deliverMsgsThread), natsSub_deliverMsgs,
                                  (void*) sub);
            if (s != NATS_OK)
                _release(sub);
        }
        else
        {
            _retain(sub);
            s = natsLib_msgDeliveryAssignWorker(sub);
            if ((s == NATS_OK) && (timeout > 0))
            {
                _retain(sub);
                s = natsTimer_Create(&sub->timeoutTimer, _asyncTimeoutCb,
                                     _asyncTimeoutStopCb, timeout, (void*) sub);
                if (s != NATS_OK)
                    _release(sub);
            }
            if (s != NATS_OK)
                _release(sub);
        }
    }

    if (s == NATS_OK)
        *newSub = sub;
    else
        natsSub_release(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Expresses interest in the given subject. The subject can have wildcards
 * (partial:*, full:>). Messages will be delivered to the associated
 * natsMsgHandler. If no natsMsgHandler is given, the subscription is a
 * synchronous subscription and can be polled via natsSubscription_NextMsg().
 */
natsStatus
natsConnection_Subscribe(natsSubscription **sub, natsConnection *nc, const char *subject,
                         natsMsgHandler cb, void *cbClosure)
{
    natsStatus s;

    if (cb == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConn_subscribe(sub, nc, subject, cb, cbClosure);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Similar to natsConnection_Subscribe() except that a timeout is given.
 * If the subscription has not receive any message for the given timeout,
 * the callback is invoked with a `NULL` message. The subscription can
 * then be destroyed, if not, the callback will be invoked again when
 * a message is received or the subscription times-out again.
 */
natsStatus
natsConnection_SubscribeTimeout(natsSubscription **sub, natsConnection *nc, const char *subject,
                                int64_t timeout, natsMsgHandler cb, void *cbClosure)
{
    natsStatus s;

    if ((cb == NULL) || (timeout <= 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConn_subscribeWithTimeout(sub, nc, subject, timeout, cb, cbClosure);

    return NATS_UPDATE_ERR_STACK(s);
}


/*
 * natsSubscribeSync is syntactic sugar for natsSubscribe(&sub, nc, subject, NULL).
 */
natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc, const char *subject)
{
    natsStatus s;

    s = natsConn_subscribeSync(sub, nc, subject);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Creates an asynchronous queue subscriber on the given subject.
 * All subscribers with the same queue name will form the queue group and
 * only one member of the group will be selected to receive any given
 * message asynchronously.
 */
natsStatus
natsConnection_QueueSubscribe(natsSubscription **sub, natsConnection *nc,
                   const char *subject, const char *queueGroup,
                   natsMsgHandler cb, void *cbClosure)
{
    natsStatus s;

    if ((queueGroup == NULL) || (strlen(queueGroup) == 0) || (cb == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConn_queueSubscribe(sub, nc, subject, queueGroup, cb, cbClosure);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Similar to natsConnection_QueueSubscribe() except that a timeout is given.
 * If the subscription has not receive any message for the given timeout,
 * the callback is invoked with a `NULL` message. The subscription can
 * then be destroyed, if not, the callback will be invoked again when
 * a message is received or the subscription times-out again.
 */
natsStatus
natsConnection_QueueSubscribeTimeout(natsSubscription **sub, natsConnection *nc,
                   const char *subject, const char *queueGroup,
                   int64_t timeout, natsMsgHandler cb, void *cbClosure)
{
    natsStatus s;

    if ((queueGroup == NULL) || (strlen(queueGroup) == 0) || (cb == NULL)
            || (timeout <= 0))
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }

    s = natsConn_queueSubscribeWithTimeout(sub, nc, subject, queueGroup, timeout, cb, cbClosure);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Similar to natsQueueSubscribe except that the subscription is synchronous.
 */
natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                       const char *subject, const char *queueGroup)
{
    natsStatus s;

    if ((queueGroup == NULL) || (strlen(queueGroup) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConn_queueSubscribeSync(sub, nc, subject, queueGroup);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * By default, messages that arrive are not immediately delivered. This
 * generally improves performance. However, in case of request-reply,
 * this delay has a negative impact. In such case, call this function
 * to have the subscriber be notified immediately each time a message
 * arrives.
 *
 * DEPRECATED
 */
natsStatus
natsSubscription_NoDeliveryDelay(natsSubscription *sub)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    return NATS_OK;
}

natsStatus
natsSub_nextMsg(natsMsg **nextMsg, natsSubscription *sub, int64_t timeout, bool pullSubInternal)
{
    natsStatus      s    = NATS_OK;
    natsConnection  *nc  = NULL;
    natsMsg         *msg = NULL;
    bool            removeSub = false;
    int64_t         target    = 0;
    jsSub           *jsi      = NULL;
    char            *fcReply  = NULL;

    if ((sub == NULL) || (nextMsg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->connClosed)
    {
        natsSub_Unlock(sub);

        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }
    if (sub->closed)
    {
        if ((sub->max > 0) && (sub->delivered >= sub->max))
            s = NATS_MAX_DELIVERED_MSGS;
        else
            s = NATS_INVALID_SUBSCRIPTION;

        natsSub_Unlock(sub);

        return nats_setDefaultError(s);
    }
    if (sub->msgCb != NULL)
    {
        natsSub_Unlock(sub);

        return nats_setDefaultError(NATS_ILLEGAL_STATE);
    }
    if (sub->slowConsumer)
    {
        sub->slowConsumer = false;
        natsSub_Unlock(sub);

        return nats_setDefaultError(NATS_SLOW_CONSUMER);
    }
    if (sub->jsi != NULL)
    {
        if (sub->jsi->sm)
        {
            sub->jsi->sm = false;
            natsSub_Unlock(sub);

            return nats_setError(NATS_MISMATCH, "%s", jsErrConsumerSeqMismatch);
        }
        else if (!pullSubInternal && sub->jsi->pull)
        {
            natsSub_Unlock(sub);
            return nats_setError(NATS_INVALID_SUBSCRIPTION, "%s", jsErrNotApplicableToPullSub);
        }
    }

    nc = sub->conn;
    jsi= sub->jsi;

    if (timeout > 0)
    {
        while ((sub->msgList.msgs == 0)
               && (s != NATS_TIMEOUT)
               && !(sub->closed)
               && !(sub->draining))
        {
            if (target == 0)
                target = nats_setTargetTime(timeout);

            s = natsCondition_AbsoluteTimedWait(sub->cond, sub->mu, target);
            if (s != NATS_OK)
                s = nats_setDefaultError(s);
        }

        if (sub->connClosed)
            s = nats_setDefaultError(NATS_CONNECTION_CLOSED);
        else if (sub->closed)
            s = nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }
    else
    {
        s = (sub->msgList.msgs == 0 ? NATS_TIMEOUT : NATS_OK);
        if ((s != NATS_OK) && !pullSubInternal)
            s = nats_setDefaultError(s);
    }

    if (s == NATS_OK)
    {
        msg = sub->msgList.head;
        if ((msg == NULL) && sub->draining)
        {
            removeSub = true;
            s = NATS_TIMEOUT;
        }
        else
        {
            sub->msgList.head = msg->next;

            if (sub->msgList.tail == msg)
                sub->msgList.tail = NULL;

            sub->msgList.msgs--;
            sub->msgList.bytes -= natsMsg_dataAndHdrLen(msg);

            msg->next = NULL;

            sub->delivered++;
            fcReply = (jsi == NULL ? NULL : jsSub_checkForFlowControlResponse(sub));

            if (sub->max > 0)
            {
                if (sub->delivered > sub->max)
                    s = nats_setDefaultError(NATS_MAX_DELIVERED_MSGS);
                else if (sub->delivered == sub->max)
                    removeSub = true;
            }

            if (sub->draining && (sub->msgList.msgs == 0))
                removeSub = true;
        }
        if (removeSub)
            _retain(sub);
    }
    if ((s == NATS_OK) && natsMsg_IsNoResponders(msg))
    {
        natsMsg_Destroy(msg);
        s = NATS_NO_RESPONDERS;
    }
    else if (s == NATS_OK)
        *nextMsg = msg;

    natsSub_Unlock(sub);

    if (fcReply != NULL)
    {
        natsConnection_Publish(nc, fcReply, NULL, 0);
        NATS_FREE(fcReply);
    }

    if (removeSub)
    {
        natsSub_setDrainCompleteState(sub);
        natsConn_removeSubscription(nc, sub);
        natsSub_release(sub);
    }

    if (pullSubInternal && (s == NATS_TIMEOUT))
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Return the next message available to a synchronous subscriber or block until
 * one is available. A timeout can be used to return when no message has been
 * delivered.
 */
natsStatus
natsSubscription_NextMsg(natsMsg **nextMsg, natsSubscription *sub, int64_t timeout)
{
    natsStatus s = natsSub_nextMsg(nextMsg, sub, timeout, false);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unsubscribe(natsSubscription *sub, int max, bool drainMode, int64_t timeout)
{
    natsStatus      s   = NATS_OK;
    natsConnection  *nc = NULL;
    bool            dc  = false;
    jsSub           *jsi;

    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);
    nc = sub->conn;
    _retain(sub);

    if ((jsi = sub->jsi) != NULL)
    {
        if (jsi->hbTimer != NULL)
            natsTimer_Stop(jsi->hbTimer);

        dc = jsi->dc;
    }

    natsSub_Unlock(sub);

    s = natsConn_unsubscribe(nc, sub, max, drainMode, timeout);

    // If user calls natsSubscription_Unsubscribe() and this
    // is a JS subscription that is supposed to delete the JS
    // consumer, do so now.
    if ((s == NATS_OK) && (max == 0) && !drainMode && dc)
        s = jsSub_deleteConsumer(sub);

    natsSub_release(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub)
{
    natsStatus s = _unsubscribe(sub, 0, false, 0);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_AutoUnsubscribe(natsSubscription *sub, int max)
{
    natsStatus s = _unsubscribe(sub, max, false, 0);
    return NATS_UPDATE_ERR_STACK(s);
}

void
natsSub_drain(natsSubscription *sub)
{
    natsSub_Lock(sub);
    SUB_DLV_WORKER_LOCK(sub);
    if (sub->closed)
    {
        SUB_DLV_WORKER_UNLOCK(sub);
        natsSub_Unlock(sub);
        return;
    }
    sub->draining = true;
    if (sub->libDlvWorker != NULL)
    {
        // If this is a subscription with timeout, stop the timer.
        if (sub->timeout != 0)
        {
            natsTimer_Stop(sub->timeoutTimer);
            // Prevent code to reset this timer
            sub->timeoutSuspended = true;
        }

        // Set this to true. It will be set to false in the
        // worker delivery thread when the control message is
        // processed.
        sub->libDlvDraining = true;

        // Post a control message to wake-up the worker which will
        // ensure that all pending messages for this subscription
        // are removed and the subscription will ultimately be
        // released in the worker thread.
        natsLib_msgDeliveryPostControlMsg(sub);
    }
    else
        natsCondition_Broadcast(sub->cond);
    SUB_DLV_WORKER_UNLOCK(sub);
    natsSub_Unlock(sub);
}

static void
_updateDrainStatus(natsSubscription *sub, natsStatus s)
{
    // Do not override a drain status if already set.
    if (sub->drainStatus == NATS_OK)
        sub->drainStatus = s;
}

void
natsSub_updateDrainStatus(natsSubscription *sub, natsStatus s)
{
    natsSub_Lock(sub);
    _updateDrainStatus(sub, s);
    natsSub_Unlock(sub);
}

// Mark the subscription such that connection stops to try to push messages into its list.
void
natsSub_setDrainSkip(natsSubscription *sub, natsStatus s)
{
    natsSub_Lock(sub);
    SUB_DLV_WORKER_LOCK(sub);
    _updateDrainStatus(sub, s);
    sub->drainSkip = true;
    SUB_DLV_WORKER_UNLOCK(sub);
    natsSub_Unlock(sub);
}

static void
_flushAndDrain(void *closure)
{
    natsSubscription *sub     = (natsSubscription*) closure;
    natsConnection   *nc      = NULL;
    natsThread       *t       = NULL;
    int64_t          timeout  = 0;
    int64_t          deadline = 0;
    bool             sync     = false;
    natsStatus       s;

    natsSub_Lock(sub);
    nc      = sub->conn;
    t       = sub->drainThread;
    timeout = sub->drainTimeout;
    sync    = (sub->msgCb == NULL ? true : false);
    natsSub_Unlock(sub);

    // Make sure that negative value is considered no timeout.
    if (timeout < 0)
        timeout = 0;
    else
        deadline = nats_setTargetTime(timeout);

    // Flush to make sure server has processed UNSUB and no new messages are coming.
    if (timeout == 0)
        s = natsConnection_Flush(nc);
    else
        s = natsConnection_FlushTimeout(nc, timeout);

    // If flush failed, update drain status and prevent connection from
    // pushing new messages to this subscription.
    if (s != NATS_OK)
        natsSub_setDrainSkip(sub, s);

    // Switch to drain regardless of status
    natsSub_drain(sub);

    // We are going to check for completion only if a timeout is specified.
    // If that is the case, the library will forcibly close the subscription.
    if (timeout > 0)
    {
        // Reset status from possibly failed flush. We are now checking for
        // the drain timeout.
        s = NATS_OK;
        // Wait for drain to complete or deadline is reached.
        natsSub_Lock(sub);
        // For sync subs, it is possible that we get here and users have
        // already called NextMsg() for all pending messages before the sub
        // was marked as "draining", so if we detect this situation, we need
        // to switch status to complete here.
        if (sync && !natsSub_drainComplete(sub) && (sub->msgList.msgs == 0))
        {
            _setDrainCompleteState(sub);
        }
        else
        {
            while ((s != NATS_TIMEOUT) && !natsSub_drainComplete(sub))
                s = natsCondition_AbsoluteTimedWait(sub->cond, sub->mu, deadline);
        }
        natsSub_Unlock(sub);

        if (s != NATS_OK)
        {
            natsSub_updateDrainStatus(sub, s);
            natsConn_removeSubscription(nc, sub);
        }
    }

    natsThread_Detach(t);
    natsThread_Destroy(t);
    natsSub_release(sub);
}

// Switch subscription's drain state to "started".
void
natsSub_initDrain(natsSubscription *sub)
{
    natsSub_Lock(sub);
    sub->drainState |= SUB_DRAIN_STARTED;
    natsSub_Unlock(sub);
}

// Initiates draining, unless already done.
// Note that this runs under the associated connection lock.
natsStatus
natsSub_startDrain(natsSubscription *sub, int64_t timeout)
{
    natsStatus s;

    if (testDrainAutoUnsubRace)
        nats_Sleep(1);

    natsSub_Lock(sub);
    if (natsSub_drainStarted(sub))
    {
        natsSub_Unlock(sub);
        return NATS_OK;
    }
    // Make sure that we just add to buffer but we don't flush it in place
    // to make sure that this call will not block.
    s = natsConn_enqueueUnsubProto(sub->conn, sub->sid);
    if (s == NATS_OK)
        s = natsThread_Create(&(sub->drainThread), _flushAndDrain, (void*) sub);
    if (s == NATS_OK)
    {
        sub->drainTimeout = timeout;
        sub->drainState |= SUB_DRAIN_STARTED;
        _retain(sub);
    }
    natsSub_Unlock(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_Drain(natsSubscription *sub)
{
    natsStatus s;

    s = _unsubscribe(sub, 0, true, DEFAULT_DRAIN_TIMEOUT);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_DrainTimeout(natsSubscription *sub, int64_t timeout)
{
    natsStatus s;

    s = _unsubscribe(sub, 0, true, timeout);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_WaitForDrainCompletion(natsSubscription *sub, int64_t timeout)
{
    natsStatus  s        = NATS_OK;
    int64_t     deadline = 0;
    bool        dc       = false;

    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);
    if (!natsSub_drainStarted(sub))
    {
        natsSub_Unlock(sub);
        return nats_setError(NATS_ILLEGAL_STATE, "%s", "Subscription not in draining mode");
    }
    _retain(sub);

    dc = (sub->jsi != NULL ? sub->jsi->dc : false);

    if (timeout > 0)
        deadline = nats_setTargetTime(timeout);

    while ((s != NATS_TIMEOUT) && !natsSub_drainComplete(sub))
    {
        if (timeout > 0)
            s = natsCondition_AbsoluteTimedWait(sub->cond, sub->mu, deadline);
        else
            natsCondition_Wait(sub->cond, sub->mu);
    }
    natsSub_Unlock(sub);

    if ((s == NATS_OK) && dc)
        s = jsSub_deleteConsumer(sub);

    natsSub_release(sub);

    // Here, we return a status as a result, not as if there was something wrong
    // with the execution of this function. So we do not update the error stack.
    return s;
}

natsStatus
natsSubscription_DrainCompletionStatus(natsSubscription *sub)
{
    natsStatus s;

    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);
    if (!natsSub_drainComplete(sub))
        s = NATS_ILLEGAL_STATE;
    else
        s = sub->drainStatus;
    natsSub_Unlock(sub);

    return s;
}

/*
 * Returns the number of queued messages in the client for this subscription.
 */
natsStatus
natsSubscription_QueuedMsgs(natsSubscription *sub, uint64_t *queuedMsgs)
{
    natsStatus  s;
    int         msgs = 0;

    if (queuedMsgs == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsSubscription_GetPending(sub, &msgs, NULL);
    if (s == NATS_OK)
        *queuedMsgs = (uint64_t) msgs;

    return s;
}

int64_t
natsSubscription_GetID(natsSubscription* sub)
{
    int64_t id = 0;

    if (sub == NULL)
        return 0;

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return 0;
    }

    id = sub->sid;

    natsSub_Unlock(sub);

    return id;
}

const char*
natsSubscription_GetSubject(natsSubscription* sub)
{
    const char* subject = NULL;

    if (sub == NULL)
        return NULL;

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return NULL;
    }

    subject = (const char*)sub->subject;

    natsSub_Unlock(sub);

    return subject;
}

natsStatus
natsSubscription_GetPending(natsSubscription *sub, int *msgs, int *bytes)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    if (msgs != NULL)
        *msgs = sub->msgList.msgs;

    if (bytes != NULL)
        *bytes = sub->msgList.bytes;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_SetPendingLimits(natsSubscription *sub, int msgLimit, int bytesLimit)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((msgLimit == 0) || (bytesLimit == 0))
        return nats_setError(NATS_INVALID_ARG, "%s",
                "Limits must be either > 0 or negative to specify no limit");

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    sub->msgsLimit = msgLimit;
    sub->bytesLimit = bytesLimit;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_GetPendingLimits(natsSubscription *sub, int *msgLimit, int *bytesLimit)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    if (msgLimit != NULL)
        *msgLimit = sub->msgsLimit;

    if (bytesLimit != NULL)
        *bytesLimit = sub->bytesLimit;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_GetDelivered(natsSubscription *sub, int64_t *msgs)
{
    if ((sub == NULL) || (msgs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    *msgs = (int64_t) sub->delivered;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_GetDropped(natsSubscription *sub, int64_t *msgs)
{
    if ((sub == NULL) || (msgs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    *msgs = sub->dropped;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_GetMaxPending(natsSubscription *sub, int *msgs, int *bytes)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    if (msgs != NULL)
        *msgs = sub->msgsMax;

    if (bytes != NULL)
        *bytes = sub->bytesMax;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_ClearMaxPending(natsSubscription *sub)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    sub->msgsMax = 0;
    sub->bytesMax = 0;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

natsStatus
natsSubscription_GetStats(natsSubscription *sub,
        int     *pendingMsgs,
        int     *pendingBytes,
        int     *maxPendingMsgs,
        int     *maxPendingBytes,
        int64_t *deliveredMsgs,
        int64_t *droppedMsgs)
{
    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }

    SUB_DLV_WORKER_LOCK(sub);

    if (pendingMsgs != NULL)
        *pendingMsgs = sub->msgList.msgs;

    if (pendingBytes != NULL)
        *pendingBytes = sub->msgList.bytes;

    if (maxPendingMsgs != NULL)
        *maxPendingMsgs = sub->msgsMax;

    if (maxPendingBytes != NULL)
        *maxPendingBytes = sub->bytesMax;

    if (deliveredMsgs != NULL)
        *deliveredMsgs = (int) sub->delivered;

    if (droppedMsgs != NULL)
        *droppedMsgs = sub->dropped;

    SUB_DLV_WORKER_UNLOCK(sub);

    natsSub_Unlock(sub);

    return NATS_OK;
}

/*
 * Returns a boolean indicating whether the subscription is still active.
 * This will return false if the subscription has already been closed,
 * or auto unsubscribed.
 */
bool
natsSubscription_IsValid(natsSubscription *sub)
{
    bool valid = false;

    if (sub == NULL)
        return false;

    natsSub_Lock(sub);

    valid = !(sub->closed);

    natsSub_Unlock(sub);

    return valid;
}

/*
 * Destroys the subscription object, freeing up memory.
 * If not already done, this call will removes interest on the subject.
 */
void
natsSubscription_Destroy(natsSubscription *sub)
{
    bool doUnsub = false;

    if (sub == NULL)
        return;

    natsSub_Lock(sub);

    doUnsub = !(sub->closed);
    // If not yet closed but user is closing from message callback but it
    // happens that auto-unsub was used and the max number was delivered, then
    // we can suppress the UNSUB protocol.
    if (doUnsub && (sub->max > 0))
        doUnsub = sub->delivered < sub->max;

    // For a JetStream subscription, disable the "delete consumer" flag
    // because we auto-delete only on explicit calls to unsub/drain.
    if (sub->jsi != NULL)
        sub->jsi->dc = false;

    natsSub_Unlock(sub);

    if (doUnsub)
        (void) natsSubscription_Unsubscribe(sub);

    natsSub_release(sub);
}
