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

#include <string.h>
#include <stdio.h>

#include "mem.h"
#include "conn.h"
#include "sub.h"
#include "js.h"
#include "glib/glib.h"

// sub and dispatcher locks must be held.
void
natsSub_enqueueMessage(natsSubscription *sub, natsMsg *msg)
{
    bool                signal  = false;
    natsDispatchQueue   *q      = &sub->dispatcher->queue;

    if (q->head == NULL)
    {
        signal = true;
        msg->next = NULL;
        q->head = msg;
    }
    else
    {
        q->tail->next = msg;
    }
    q->tail = msg;
    q->msgs++;
    q->bytes += natsMsg_dataAndHdrLen(msg);

    if (signal)
        natsCondition_Signal(sub->dispatcher->cond);
}

// sub and dispatcher locks must be held.
natsStatus
natsSub_enqueueUserMessage(natsSubscription *sub, natsMsg *msg)
{
    natsDispatchQueue   *toQ        = &sub->dispatcher->queue;
    natsDispatchQueue   *statsQ     = &sub->ownDispatcher.queue;
    int                 newMsgs     = statsQ->msgs + 1;
    int                 newBytes    = statsQ->bytes + natsMsg_dataAndHdrLen(msg);

    msg->sub = sub;

    if (((sub->msgsLimit > 0) && (newMsgs > sub->msgsLimit)) ||
        ((sub->bytesLimit > 0) && (newBytes > sub->bytesLimit)))
    {
        return NATS_SLOW_CONSUMER;
    }
    sub->slowConsumer = false;

    if (newMsgs > sub->msgsMax)
        sub->msgsMax = newMsgs;
    if (newBytes > sub->bytesMax)
        sub->bytesMax = newBytes;

    if ((sub->jsi != NULL) && sub->jsi->ackNone)
        natsMsg_setAcked(msg);

    // Update the subscription stats if separate, the queue stats will be
    // updated below.
    if (toQ != statsQ)
    {
        statsQ->msgs++;
        statsQ->bytes += natsMsg_dataAndHdrLen(msg);
    }

    natsSub_enqueueMessage(sub, msg);
    return NATS_OK;
}

// Sub/dispatch locks must be held.
static inline void
_removeHeadMsg(natsDispatcher *d, natsMsg *msg)
{
    d->queue.head = msg->next;
    if (d->queue.tail == msg)
        d->queue.tail = NULL;
    msg->next = NULL;
}

// Returns fetch status, sub/dispatch locks must be held.
static inline natsStatus
_preProcessUserMessage(
    natsSubscription *sub, jsSub *jsi, jsFetch *fetch, natsMsg *msg,
    bool *userMsg, bool *overLimit, bool *lastMessageInSub, bool *lastMessageInFetch, char **fcReply)
{
    natsStatus fetchStatus = NATS_OK;
    *userMsg = true;

    // Is this a real message? If so, account for having processed it.
    bool isRealMessage = (msg->subject[0] != '\0');
    if (isRealMessage)
    {
        sub->ownDispatcher.queue.msgs--;
        sub->ownDispatcher.queue.bytes -= natsMsg_dataAndHdrLen(msg);
    }

    // Fetch-specific handling of synthetic and header-only messages
    if ((jsi != NULL) && (fetch != NULL))
        fetchStatus = js_checkFetchedMsg(sub, msg, jsi->fetchID, true, userMsg);

    // Is it another kind of synthetic message?
    *userMsg = *userMsg && (msg->subject[0] != '\0');

    // Check the limits.
    if (*userMsg)
    {
        if (sub->max > 0)
        {
            *overLimit = (sub->delivered == sub->max);
            *lastMessageInSub = (sub->delivered == (sub->max - 1));
        }

        if (fetch)
        {
            bool overMaxBytes = ((fetch->lifetime.MaxBytes > 0) && ((fetch->deliveredBytes) > fetch->lifetime.MaxBytes));
            bool overMaxFetch = ((fetch->deliveredMsgs >= fetch->lifetime.Batch) || overMaxBytes);

            *lastMessageInFetch = (fetch->deliveredMsgs == (fetch->lifetime.Batch - 1) || overMaxBytes);

            // See if we want to override fetch status based on our own data.
            if (fetchStatus == NATS_OK)
            {
                if (*lastMessageInFetch || overMaxFetch)
                {
                    fetchStatus = NATS_MAX_DELIVERED_MSGS;
                }
                if (overMaxBytes)
                {
                    fetchStatus = NATS_LIMIT_REACHED;
                }
            }
            *overLimit = (*overLimit || overMaxFetch || overMaxBytes);
            *lastMessageInSub = (*lastMessageInSub || *lastMessageInFetch);
        }

        if (!*overLimit)
        {
            sub->delivered++;
            if (fetch)
            {
                fetch->deliveredMsgs++;
                fetch->deliveredBytes += natsMsg_dataAndHdrLen(msg);
            }
        }

        *fcReply = (jsi == NULL ? NULL : jsSub_checkForFlowControlResponse(sub));
    }

    return fetchStatus;
}

// Thread main function for a thread pool of dispatchers.
void
nats_dispatchThreadPool(void *arg)
{
    natsDispatcher *d = (natsDispatcher *)arg;

    nats_lockDispatcher(d);

    while (true)
    {
        natsMsg     *msg                = NULL;
        char        *fcReply            = NULL;
        bool        timerNeedReset      = false;
        bool        userMsg             = true;
        bool        timeout             = false;
        bool        overLimit           = false;
        bool        lastMessageInSub    = false;
        bool        lastMessageInFetch  = false;
        natsStatus  fetchStatus         = NATS_OK;

        while (((msg = d->queue.head) == NULL) && !d->shutdown)
            natsCondition_Wait(d->cond, d->mu);

        // Break out only when list is empty
        if ((msg == NULL) && d->shutdown)
        {
            break;
        }

        _removeHeadMsg(d, msg);

        // Get subscription reference from message and capture values we need
        // while under lock.
        natsSubscription    *sub                = msg->sub;
        natsConnection      *nc                 = sub->conn;
        jsSub               *jsi                = sub->jsi;
        jsFetch             *fetch              = (jsi != NULL) ? jsi->fetch : NULL;
        natsMsgHandler      messageCB           = sub->msgCb;
        void                *messageClosure     = sub->msgCbClosure;
        natsOnCompleteCB    completeCB          = sub->onCompleteCB;
        void                *completeCBClosure  = sub->onCompleteCBClosure;
        natsSubscriptionControlMessages *ctrl   = sub->control;

        fetchStatus = _preProcessUserMessage(
            sub, jsi, fetch, msg,
            &userMsg, &overLimit, &lastMessageInSub, &lastMessageInFetch, &fcReply);

        // Check the timeout timer.
        timerNeedReset = false;
        if (userMsg || (msg == sub->control->sub.timeout))
        {
            sub->timeoutSuspended = true;
            // Need to reset the timer after the user callback returns, but only
            // if we are already in a timeout, or there are no more messages in
            // the queue.
            if (!sub->draining && !sub->closed && (sub->timeout > 0))
                if (timeout || (sub->ownDispatcher.queue.msgs == 0))
                {
                    timerNeedReset = true; // after the callbacks return
                }
        }

        // Process synthetic messages
        if (msg == ctrl->sub.drain)
        {
            // Subscription is draining, we are past the last message,
            // remove the subscription. This will schedule another
            // control message for the close.
            nats_unlockDispatcher(d);
            natsSub_setDrainCompleteState(sub);
            natsConn_removeSubscription(nc, sub);
            nats_lockDispatcher(d);
            continue;
        }
        else if (msg == ctrl->sub.close)
        {
            nats_unlockDispatcher(d);
            // Call this in case the subscription was draining.
            natsSub_setDrainCompleteState(sub);

            if (completeCB != NULL)
                (*completeCB)(completeCBClosure);

            // Subscription closed, just release
            natsSub_release(sub);

            nats_lockDispatcher(d);
            continue;
        }
        else if (msg == ctrl->sub.timeout)
        {
            nats_unlockDispatcher(d);

            // Invoke the callback with a NULL message.
            (*messageCB)(nc, sub, NULL, messageClosure);

            nats_lockDispatcher(d);

            if (!sub->draining && !sub->closed)
            {
                // Reset the timedOut boolean to allow for the
                // subscription to timeout again, and reset the
                // timer to fire again starting from now.
                sub->timedOut = false;
                natsTimer_Reset(sub->timeoutTimer, sub->timeout);
            }
            continue;
        }

        // Fetch control messages
        else if ((fetchStatus != NATS_OK) && !lastMessageInFetch)
        {
            // We drop here only if this is not already marked as last message
            // in fetch. The last message will be delivered first.
            natsFetchCompleteHandler fetchCompleteCB = fetch->completeCB;
            void *fetchCompleteCBClosure = fetch->completeCBClosure;

            // TODO: future: options for handling missed heartbeat, for now
            // treat it as any other error and terminate.

            nats_unlockDispatcher(d);
            if (fetchCompleteCB != NULL)
                (*fetchCompleteCB)(nc, sub, fetchStatus, fetchCompleteCBClosure);

            // Call this blindly, it will be a no-op if the subscription
            // was not draining.
            natsSub_setDrainCompleteState(sub);
            natsConn_removeSubscription(nc, sub);
            natsMsg_Destroy(msg); // may be an actual headers-only message
            nats_lockDispatcher(d);
            continue;
        }
        else if ((fetch != NULL) && (fetchStatus == NATS_OK) && !userMsg)
        {
            // Fetch heartbeat. Need to set the active bit to prevent the missed
            // heartbeat condition when the timer fires.
            jsi->active = true;
            natsMsg_Destroy(msg);
            continue;
        }

        // Need to check for closed subscription again here. The subscription
        // could have been unsubscribed from a callback but there were already
        // pending messages. The control message is queued up. Until it is
        // processed, we need to simply discard the message and continue.
        //
        // Other invalid states: same handling, discard the message and
        // continue.
        else if ((sub->closed) ||
                 (msg->sub == NULL) || (msg->subject == NULL) || (strcmp(msg->subject, "") == 0))
        {
            natsMsg_Destroy(msg);
            continue;
        }

        // --- Normal user message delivery. ---

        // Is this a subscription that can timeout?
        if (!sub->draining && (sub->timeout != 0))
        {
            // Prevent the timer from posting a timeout synthetic message.
            sub->timeoutSuspended = true;

            // If we are dealing with the last pending message for this sub,
            // we will reset the timer after the user callback returns.
            if (sub->ownDispatcher.queue.msgs == 0)
                timerNeedReset = true;
        }

        nats_unlockDispatcher(d);

        // If we are fetching, see if we need to ask the server for more.
        if (fetch != NULL)
            js_maybeFetchMore(sub, fetch);

        if (!overLimit)
            (*messageCB)(nc, sub, msg, messageClosure);
        else
            natsMsg_Destroy(msg);

        if (fcReply != NULL)
        {
            natsConnection_Publish(nc, fcReply, NULL, 0);
            NATS_FREE(fcReply);
        }

        if (lastMessageInFetch)
        {
            if (fetch->completeCB != NULL)
                fetch->completeCB(nc, sub, fetchStatus, fetch->completeCBClosure);
        }

        // If we have reached the sub's message max, we need to remove
        // the sub.
        if (lastMessageInSub)
        {
            // Call this blindly, it will be a no-op if the subscription
            // was not draining.
            natsSub_setDrainCompleteState(sub);
            natsConn_removeSubscription(nc, sub);
        }

        nats_lockDispatcher(d);

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

    nats_destroyQueuedMessages(&d->queue);
    nats_unlockDispatcher(d);

    natsLib_Release();
}

// Thread main function for a subscription-owned dispatcher.
void
nats_dispatchThreadOwn(void *arg)
{
    natsSubscription    *sub = (natsSubscription *)arg;
    bool                rmSub = false;

    // These are set at sub creation time and never change, no need to lock.
    natsConnection *nc          = sub->conn;
    natsMsgHandler messageCB    = sub->msgCb;
    void *messageClosure        = sub->msgCbClosure;
    natsOnCompleteCB completeCB = NULL;
    void *completeCBClosure     = NULL;

    // This just serves as a barrier for the creation of this thread.
    natsConn_Lock(nc);
    natsConn_Unlock(nc);

    while (true)
    {
        natsStatus  s                   = NATS_OK;
        natsStatus  fetchStatus         = NATS_OK;
        natsMsg     *msg                = NULL;
        bool        userMsg             = true;
        bool        overLimit           = false;
        bool        lastMessageInSub    = false;
        bool        lastMessageInFetch  = false;

        natsSub_Lock(sub);
        int64_t timeout = sub->timeout;

        while (((msg = sub->ownDispatcher.queue.head) == NULL) && !(sub->closed) && !(sub->draining) && (s != NATS_TIMEOUT))
        {
            if (timeout != 0)
                s = natsCondition_TimedWait(sub->ownDispatcher.cond, sub->mu, timeout);
            else
                natsCondition_Wait(sub->ownDispatcher.cond, sub->mu);
        }

        bool draining = sub->draining;
        completeCB = sub->onCompleteCB;
        completeCBClosure = sub->onCompleteCBClosure;

        if (sub->closed)
        {
            natsSub_Unlock(sub);
            break;
        }

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
                (*messageCB)(nc, sub, NULL, messageClosure);
            continue;
        }

        _removeHeadMsg(&sub->ownDispatcher, msg);

        jsSub *jsi = sub->jsi;
        jsFetch *fetch = (jsi != NULL) ? jsi->fetch : NULL;
        char *fcReply = NULL;
        fetchStatus = _preProcessUserMessage(
            sub, jsi, fetch, msg,
            &userMsg, &overLimit, &lastMessageInSub, &lastMessageInFetch, &fcReply);

        // Fetch control messages
        if ((fetchStatus != NATS_OK) && !lastMessageInFetch)
        {
            // We drop here only if this is not already marked as last message
            // in fetch. The last message will be delivered first.
            natsFetchCompleteHandler fetchCompleteCB = fetch->completeCB;
            void *fetchCompleteCBClosure = fetch->completeCBClosure;

            natsSub_Unlock(sub);
            if (fetchCompleteCB != NULL)
                (*fetchCompleteCB)(nc, sub, fetchStatus, fetchCompleteCBClosure);

            natsMsg_Destroy(msg); // may be an actual headers-only message
            rmSub = true;
            break;
        }
        else if ((fetch != NULL) && (fetchStatus == NATS_OK) && !userMsg)
        {
            // Fetch heartbeat. Need to set the active bit to prevent the missed
            // heartbeat condition when the timer fires.
            jsi->active = true;
            natsSub_Unlock(sub);
            natsMsg_Destroy(msg);
            continue;
        }

        natsSub_Unlock(sub);

        // If we are fetching, see if we need to ask the server for more.
        if (fetch != NULL)
            js_maybeFetchMore(sub, fetch);

        if (!overLimit)
            (*messageCB)(nc, sub, msg, messageClosure);
        else
            natsMsg_Destroy(msg);

        if (fcReply != NULL)
        {
            natsConnection_Publish(nc, fcReply, NULL, 0);
            NATS_FREE(fcReply);
        }

        if (lastMessageInFetch)
        {
            if (fetch->completeCB != NULL)
                fetch->completeCB(nc, sub, fetchStatus, fetch->completeCBClosure);
        }

        if (lastMessageInSub)
        {
            // If we have hit the max for delivered msgs, remove sub.
            rmSub = true;
            break;
        }
    }

    natsSub_setDrainCompleteState(sub);

    if (rmSub)
        natsConn_removeSubscription(nc, sub);

    if (completeCB != NULL)
        (*completeCB)(completeCBClosure);

    natsSub_release(sub);
}
