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

// When using the global dispatch queue, we still use the subscription's own
// queue storage to keep track of message stats.
//
// sub lock must be held
natsStatus natsSub_enqueueMsgImpl(natsSubscription *sub, natsMsg *msg, bool isCtrlMesssage)
{
    bool signal = false;
    bool shared = (sub->dispatcher->dedicatedTo == NULL);

    natsDispatchQueue *toQ = &sub->dispatcher->queue;
    natsDispatchQueue *statsQ = &sub->ownDispatcher.queue;

    int newMsgs = statsQ->msgs + 1;
    int newBytes = statsQ->bytes + natsMsg_dataAndHdrLen(msg);

    if (!isCtrlMesssage)
    {
        if (((sub->msgsLimit > 0) && (newMsgs > sub->msgsLimit)) ||
            ((sub->bytesLimit > 0) && (newBytes > sub->bytesLimit)))
        {
            return NATS_SLOW_CONSUMER;
        }

        if ((sub->jsi != NULL) && (sub->jsi->fetch != NULL))
        {
            sub->jsi->fetch->receivedMsgs++;
            sub->jsi->fetch->receivedBytes += natsMsg_dataAndHdrLen(msg);
        }
    }

    if (newMsgs > sub->msgsMax)
        sub->msgsMax = newMsgs;
    if (newBytes > sub->bytesMax)
        sub->bytesMax = newBytes;

    // Update the subscription stats if separate, the queue stats will be
    // updated below.
    if (toQ != statsQ)
    {
        statsQ->msgs++;
        statsQ->bytes += natsMsg_dataAndHdrLen(msg);
    }
    sub->slowConsumer = false;

    // For shared dispatchers, we need to lock the dispatcher to place items on
    // its queue. When we have a dedicated one, it uses the sub's mu and it's
    // already locked.
    if (shared)
        nats_lockDispatcher(sub->dispatcher);

    if (toQ->head == NULL)
    {
        signal = true;
        msg->next = NULL;
        toQ->head = msg;
        if (toQ->tail == NULL)
            toQ->tail = msg;
    }
    else
    {
        toQ->tail->next = msg;
        toQ->tail = msg;
    }

    toQ->msgs++;
    toQ->bytes += natsMsg_dataAndHdrLen(msg);

    if (signal)
        natsCondition_Signal(sub->dispatcher->cond);

    if (shared)
        nats_unlockDispatcher(sub->dispatcher);

    return NATS_OK;
}

static inline void
_removeHeadMessage(natsDispatchQueue *queue)
{
    natsMsg *msg = queue->head;

    queue->head = msg->next;
    if (queue->head == NULL)
        queue->tail = NULL;

    queue->msgs--;
    queue->bytes -= natsMsg_dataAndHdrLen(msg);
    msg->next = NULL;
}

static inline void
_resetSubTimeoutTimer(natsSubscription *sub, bool *timerNeedReset)
{
    if (!*timerNeedReset)
        return;

    *timerNeedReset = false;
    natsSub_Lock(sub);
    sub->timeoutSuspended = false;
    natsTimer *timer = sub->timeoutTimer;
    int64_t timeout = sub->timeout;
    natsSub_Unlock(sub);
    natsTimer_Reset(timer, timeout);
}

void nats_dispatchMessages(natsDispatcher *d)
{
    const bool shared = (d->dedicatedTo == NULL);

    while (true)
    {
        natsStatus s = NATS_OK;
        natsMsg *msg = NULL;
        char *fcReply;
        bool timerNeedReset = false;
        bool userMsg = false;
        bool timeout = false;
        bool overLimit = false;
        bool lastMessageInSub = false;
        bool lastMessageInFetch = false;
        natsStatus fetchStatus = NATS_OK;

        // values we gather from sub under lock to use later.
        natsConnection *nc = NULL;
        natsMsgHandler messageCB = NULL;
        void *messageClosure = NULL;
        natsOnCompleteCB completeCB = NULL;
        void *completeCBClosure = NULL;
        natsSubscriptionControlMessages *ctrl = NULL;
        bool closed = false;
        jsSub *jsi = NULL;
        jsFetch *fetch = NULL;

        // default to dedicated sub, but usually set from the message.
        natsSubscription *sub = d->dedicatedTo;

        // Get the next message under the dispatcher lock.
        nats_lockDispatcher(d);

        while (!d->shutdown && ((msg = d->queue.head) == NULL) && (s != NATS_TIMEOUT))
        {
            // When we are dedicated to a subscription, we can use
            // natsCondition_TimedWait, and do not need an expiration timer, saving
            // some overhead. We don't need to lock the sub because it is already
            // locked, we share the mutex.
            //
            // In the shared mode the sub is not known, so a per-sub expiration
            // timer is set up and it sends control messages to the sub to notify us
            // here.
            //
            // All other events (Drain, Close, fetch timeouts/HB misses) are less
            // frequent and handled similarly, with the use of control messages.

            // if dedicatedSub is set, we don't need to lock it, it already is.
            if ((sub != NULL) && (sub->timeout > 0))
            {
                s = natsCondition_TimedWait(d->cond, d->mu, sub->timeout);
            }
            else
                natsCondition_Wait(d->cond, d->mu);
        }

        // Check for shutdown.
        if ((msg == NULL) && (s != NATS_TIMEOUT))
        {
            if (d->shutdown)
            {
                // We are still under the dispatcher lock
                nats_destroyQueuedMessages(&d->queue);
                nats_unlockDispatcher(d);

                natsLib_Release();
                return;
            }
            else
            {
                // Spurious NULL message, where did it come from? (unreachable)
                nats_unlockDispatcher(d);
                continue;
            }
        }

        userMsg = true;
        if (msg != NULL)
        {
            if (msg->sub != NULL)
                sub = msg->sub;

            jsi = sub->jsi;
            fetch = (jsi != NULL) ? jsi->fetch : NULL;

            _removeHeadMessage(&d->queue);

            // Is ths a fetch status or synthetic message?
            if (fetch != NULL)
                fetchStatus = js_checkFetchedMsg(sub, msg, true, &userMsg);

            // Is it any other kind of a synthetic message?
            userMsg = userMsg && (msg->subject[0] != '\0');
        }

        // At this point sub is set, no need to check for NULL. Non-NULL
        // messages must have a sub. The only NULL messages that can come here
        // are from timeouts detected by the while loop above when running in
        // the dedicated mode. In that case `sub` is already defaulted to the
        // dispatcher's dedicated sub.

        timeout = ((s == NATS_TIMEOUT) || (msg == sub->control->sub.timeout));

        // If we are running as a shared dispatcher, we need to re-lock from the
        // dispatcher queue to the sub. When running in the dedicated mode, it's
        // the same lock and queue, so no need to do anything.
        if (shared)
        {
            nats_unlockDispatcher(d);
            natsSub_Lock(sub);
            sub->ownDispatcher.queue.msgs--;
            sub->ownDispatcher.queue.bytes -= natsMsg_dataAndHdrLen(msg);
        }

        // Check the limits.
        if (userMsg)
        {
            if (sub->max > 0)
            {
                overLimit = (sub->delivered == sub->max);
                lastMessageInSub = (sub->delivered == (sub->max - 1));
            }

            if (fetch)
            {
                bool overMaxBytes = ((fetch->lifetime.MaxBytes > 0) && ((fetch->deliveredBytes) > fetch->lifetime.MaxBytes));
                bool overMaxFetch = ((fetch->deliveredMsgs >= fetch->lifetime.Batch) || overMaxBytes);
                
                lastMessageInFetch = (fetch->deliveredMsgs == (fetch->lifetime.Batch - 1) || overMaxBytes);

                // See if we want to override fetch status based on our own data.
                if (fetchStatus == NATS_OK)
                {
                    if (lastMessageInFetch || overMaxFetch)
                    {
                        fetchStatus = NATS_MAX_DELIVERED_MSGS;
                    }
                    if (overMaxBytes)
                    {
                        fetchStatus = NATS_LIMIT_REACHED;
                    }
                }
                overLimit = (overLimit || overMaxFetch || overMaxBytes);
                lastMessageInSub = (lastMessageInSub || lastMessageInFetch);
            }

            if (!overLimit)
            {
                sub->delivered++;
                if (fetch)
                {
                    fetch->deliveredMsgs++;
                    fetch->deliveredBytes += natsMsg_dataAndHdrLen(msg);
                }
            }
        }

        // Update the sub state while under lock.
        timerNeedReset = false;
        if (timeout || userMsg)
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

        // Extract values from the sub while under lock.
        nc = sub->conn;
        messageCB = sub->msgCb;
        messageClosure = sub->msgCbClosure;
        completeCB = sub->onCompleteCB;
        completeCBClosure = sub->onCompleteCBClosure;
        ctrl = sub->control;
        closed = sub->closed;

        // Check for flow control response and update the sub while under lock.
        // We will publish this in the end of the dispatch loop. (Control
        // messages can't have a an fcReply).
        //
        // IMPORTANT: we rely on this to set the "active" state of the sub, for
        // the heartbeat timer to check later.
        fcReply = NULL;
        if ((sub != NULL) && (jsi != NULL))
            fcReply = jsSub_checkForFlowControlResponse(sub);

        // Completeley unlock the dispatcher queue/sub. From here down, let the
        // natsSub_... methods lock it as needed.
        if (shared)
            natsSub_Unlock(sub);
        else
            nats_unlockDispatcher(d);

        // --- All locks released, handle the message ---

        // --- Handle control messages first. ---
        if (timeout)
        {
            // FIXME: if timeout while draining, remove the sub. See main/sub/deliverMsg

            // Call the user's callback with a NULL message to indicate a
            // timeout.
            messageCB(nc, sub, NULL, messageClosure);

            if (shared)
                _resetSubTimeoutTimer(sub, &timerNeedReset);
            continue;
        }
        else if (msg == ctrl->sub.close)
        {
            natsSub_setDrainCompleteState(sub);

            if (completeCB != NULL) // for the sub
                completeCB(completeCBClosure);

            // Need to check before we release the sub, since d may be part of
            // it and freed by release.
            if (d->dedicatedTo != NULL)
            {
                natsSub_release(sub);
                natsLib_Release();
                return;
            }
            else
            {
                natsSub_release(sub);
                continue;
            }
        }
        else if (msg == ctrl->sub.drain)
        {
            // Subscription is draining, we are past the last message,
            // remove the subscription. This will schedule another
            // control message for the close.
            natsSub_setDrainCompleteState(sub);
            natsConn_removeSubscription(nc, sub);
            continue;
        }
        else if ((fetchStatus != NATS_OK) && !lastMessageInFetch)
        {
            // If last message in fetch, will call the callback after the
            // message is delievered.

            // FIXME: do we care to send an async error, if serious?
            // FIXME: handling missed heartbeat: user option on what to do?
            // FIXME: ignore missed heartbeat if the sub is reconnecting? Or should this be done in the readloop?

            if (fetch->completeCB != NULL)
                fetch->completeCB(nc, sub, fetchStatus, fetch->completeCBClosure);

            // Call this blindly, it will be a no-op if the subscription
            // was not draining.
            natsSub_setDrainCompleteState(sub);
            natsConn_removeSubscription(nc, sub);
            natsMsg_Destroy(msg);
            continue;
        }
        else if ((fetchStatus == NATS_OK) && !userMsg)
        {
            // Do nothing. The active bit for the heartbeat timer (to skip the
            // next tick) has already been set.
            natsMsg_Destroy(msg);
            continue;
        }

        // --- User messages (user or flow control/status) ---
        else if (closed)
        {
            natsMsg_Destroy(msg);
            continue;
        }

        else if ((msg->sub == NULL) || (msg->subject == NULL) || (strcmp(msg->subject, "") == 0))
        {
            // invalid state.
            natsMsg_Destroy(msg);
            continue;
        }

        // --- Handle USER messages ---
        else if (overLimit)
        {
            // Extraneous message, discard it.
            natsMsg_Destroy(msg);
            if (fcReply != NULL)
            {
                natsConnection_Publish(nc, fcReply, NULL, 0);
                NATS_FREE(fcReply);
            }
            continue;
        }
        else
        {
            // If we are fetching, see if we need to ask the server for more.
            if (fetch != NULL)
                js_maybeFetchMore(sub, fetch);

            // Deliver the message to the user's callback(s).
            messageCB(nc, sub, msg, messageClosure);

            if (lastMessageInFetch)
            {
                if (fetch->completeCB != NULL)
                    fetch->completeCB(nc, sub, fetchStatus, fetch->completeCBClosure);
            }

            // If we have reached the sub's message max, we need to remove
            // the sub. These calls re-lock the sub, so do it while not
            // locking anything.
            if (lastMessageInSub)
            {
                // Call this blindly, it will be a no-op if the subscription
                // was not draining.
                natsSub_setDrainCompleteState(sub);
                natsConn_removeSubscription(nc, sub);
            }

            if (shared)
                _resetSubTimeoutTimer(sub, &timerNeedReset);

            if (fcReply != NULL)
            {
                natsConnection_Publish(nc, fcReply, NULL, 0);
                NATS_FREE(fcReply);
            }
            continue;
        }

        // unreachable
    }
}
