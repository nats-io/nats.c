// Copyright 2015 Apcera Inc. All rights reserved.

#include <string.h>
#include <stdio.h>

#include "natsp.h"
#include "mem.h"
#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "util.h"

#ifdef DEV_MODE

static void _retain(natsSubscription *sub)   { sub->refs++; }

void natsSub_Lock(natsSubscription *sub)     { natsMutex_Lock(sub->mu);   }
void natsSub_Unlock(natsSubscription *sub)   { natsMutex_Unlock(sub->mu); }

#else

#define _retain(s)  ((s)->refs++)

#endif // DEV_MODE

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

    natsThread_Destroy(sub->deliverMsgsThread);
    natsCondition_Destroy(sub->cond);
    natsMutex_Destroy(sub->mu);

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

    natsConn_Lock(nc);

    max = sub->max;

    natsConn_Unlock(nc);

    while (true)
    {
        natsSub_Lock(sub);

        while ((sub->msgList.count == 0)
               && !(sub->signaled)
               && !(sub->closed))
        {
            natsCondition_Wait(sub->cond, sub->mu);
        }

        sub->signaled = false;

        if (sub->closed)
        {
            natsSub_Unlock(sub);
            break;
        }

        delivered = ++(sub->delivered);

        msg = sub->msgList.head;

        sub->msgList.head = msg->next;

        if (sub->msgList.tail == msg)
            sub->msgList.tail = NULL;

        sub->msgList.count--;

        msg->next = NULL;

        natsSub_Unlock(sub);

        if ((max == 0) || (delivered <= max))
        {
           (*mcb)(nc, sub, msg, mcbClosure);
        }
        else
        {
            // If we have hit the max for delivered msgs, remove sub.
            natsConn_removeSubscription(nc, sub, true);
            break;
        }
    }

    natsSub_release(sub);
}

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, natsMsgHandler cb, void *cbClosure)
{
    natsStatus          s = NATS_OK;
    natsSubscription    *sub = NULL;

    sub = (natsSubscription*) NATS_CALLOC(1, sizeof(natsSubscription));
    if (sub == NULL)
        return NATS_NO_MEMORY;

    natsConn_retain(nc);

    sub->refs           = 1;
    sub->conn           = nc;
    sub->msgCb          = cb;
    sub->msgCbClosure   = cbClosure;

    s = natsMutex_Create(&(sub->mu));
    if (s == NATS_OK)
    {
        sub->subject = NATS_STRDUP(subj);
        if (sub->subject == NULL)
            s = NATS_NO_MEMORY;
    }
    if ((s == NATS_OK) && (queueGroup != NULL) && (strlen(queueGroup) > 0))
    {
        sub->queue = NATS_STRDUP(queueGroup);
        if (sub->queue == NULL)
            s = NATS_NO_MEMORY;
    }
    if (s == NATS_OK)
        s = natsCondition_Create(&(sub->cond));
    if ((s == NATS_OK) && (cb != NULL))
    {
        // If we have an async callback, start up a sub specific
        // thread to deliver the messages.
        s = natsThread_Create(&(sub->deliverMsgsThread), natsSub_deliverMsgs,
                              (void*) sub);
        if (s == NATS_OK)
        {
            // If the thread above was created ok, we need a retain, since the
            // thread will do a release on exit. It is safe to do the retain
            // after the create because the thread needs a lock held by the
            // caller.
            _retain(sub);
        }
    }

    if (s == NATS_OK)
        *newSub = sub;
    else
        _freeSubscription(sub);

    return s;
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
    return natsConn_subscribe(sub, nc, subject, NULL, cb, cbClosure);
}

/*
 * natsSubscribeSync is syntactic sugar for natsSubscribe(&sub, nc, subject, NULL).
 */
natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc, const char *subject)
{
    return natsConn_subscribe(sub, nc, subject, NULL, NULL, NULL);
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
    if ((queueGroup == NULL) || (strlen(queueGroup) == 0) || (cb == NULL))
        return NATS_INVALID_ARG;

    return natsConn_subscribe(sub, nc, subject, queueGroup, cb, cbClosure);
}

/*
 * Similar to natsQueueSubscribe except that the subscription is synchronous.
 */
natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                       const char *subject, const char *queueGroup)
{
    if ((queueGroup == NULL) || (strlen(queueGroup) == 0))
        return NATS_INVALID_ARG;

    return natsConn_subscribe(sub, nc, subject, queueGroup, NULL, NULL);
}

/*
 * Return the next message available to a synchronous subscriber or block until
 * one is available. A timeout can be used to return when no message has been
 * delivered.
 */
natsStatus
natsSubscription_NextMsg(natsMsg **nextMsg, natsSubscription *sub, int64_t timeout)
{
    natsStatus      s    = NATS_OK;
    natsConnection  *nc  = NULL;
    natsMsg         *msg = NULL;
    bool            removeSub = false;
    int64_t         target    = 0;

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);

        return NATS_CONNECTION_CLOSED;
    }
    if (sub->msgCb != NULL)
    {
        natsSub_Unlock(sub);

        return NATS_ILLEGAL_STATE;
    }
    if (sub->slowConsumer)
    {
        sub->slowConsumer = false;
        natsSub_Unlock(sub);

        return NATS_SLOW_CONSUMER;
    }

    nc = sub->conn;

    if (timeout > 0)
    {
        while ((sub->msgList.count == 0)
               && !(sub->signaled)
               && (s != NATS_TIMEOUT)
               && !(sub->closed))
        {
            if (target == 0)
                target = nats_Now() + timeout;

            s = natsCondition_AbsoluteTimedWait(sub->cond, sub->mu, target);
        }

        sub->signaled = false;

        if (sub->closed)
            s = NATS_INVALID_SUBSCRIPTION;
    }
    else
    {
        s = (sub->msgList.count == 0 ? NATS_TIMEOUT : NATS_OK);
    }

    if (s == NATS_OK)
    {
        sub->delivered++;
        if (sub->max > 0)
        {
            if (sub->delivered > sub->max)
                s = NATS_MAX_DELIVERED_MSGS;
            else if (sub->delivered == sub->max)
                removeSub = true;
        }
    }
    if (s == NATS_OK)
    {
        msg = sub->msgList.head;

        sub->msgList.head = msg->next;

        if (sub->msgList.tail == msg)
            sub->msgList.tail = NULL;

        sub->msgList.count--;

        msg->next = NULL;

        *nextMsg = msg;
    }

    natsSub_Unlock(sub);

    if (removeSub)
        natsConn_removeSubscription(nc, sub, true);

    return s;
}

static natsStatus
_unsubscribe(natsSubscription *sub, int max)
{
    natsStatus      s   = NATS_OK;
    natsConnection  *nc = NULL;

    if (sub == NULL)
        return NATS_INVALID_ARG;

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);

        return NATS_CONNECTION_CLOSED;
    }

    nc = sub->conn;
    _retain(sub);

    natsSub_Unlock(sub);

    s = natsConn_unsubscribe(nc, sub, max);

    natsSub_release(sub);

    return s;
}

/*
 * Removes interest on the subject. Asynchronous subscription may still have
 * a callback in progress, in that case, the subscription will still be valid
 * until the callback returns.
 */
natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub)
{
    return _unsubscribe(sub, 0);
}

/*
 * This call issues an automatic natsSubscription_Unsubscribe that is
 * processed by the server when 'max' messages have been received.
 * This can be useful when sending a request to an unknown number
 * of subscribers.
 */
natsStatus
natsSubscription_AutoUnsubscribe(natsSubscription *sub, int max)
{
    return _unsubscribe(sub, max);
}

/*
 * Returns the number of queued messages in the client for this subscription.
 */
natsStatus
natsSubscription_QueuedMsgs(natsSubscription *sub, uint64_t *queuedMsgs)
{
    if (sub == NULL)
        return NATS_INVALID_ARG;

    natsSub_Lock(sub);

    if (sub->closed)
    {
        natsSub_Unlock(sub);

        return NATS_INVALID_SUBSCRIPTION;
    }

    *queuedMsgs = (uint64_t) sub->msgList.count;

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
    if (sub == NULL)
        return;

    (void) natsSubscription_Unsubscribe(sub);

    natsSub_release(sub);
}
