// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <string.h>
#include <stdio.h>

#include "mem.h"
#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "util.h"

#ifdef DEV_MODE

static void _retain(natsSubscription *sub)   { sub->refs++; }
static void _release(natsSubscription *sub)  { sub->refs--; }

void natsSub_Lock(natsSubscription *sub)     { natsMutex_Lock(sub->mu);   }
void natsSub_Unlock(natsSubscription *sub)   { natsMutex_Unlock(sub->mu); }

#else

#define _retain(s)  ((s)->refs++)
#define _release(s) ((s)->refs--)

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

    if (sub->deliverMsgsThread != NULL)
    {
        natsThread_Detach(sub->deliverMsgsThread);
        natsThread_Destroy(sub->deliverMsgsThread);
    }
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

    // This just servers as a barrier for the creation of this thread.
    natsConn_Lock(nc);
    natsConn_Unlock(nc);

    while (true)
    {
        natsSub_Lock(sub);

        while ((sub->msgList.msgs == 0) && !(sub->closed))
        {
            sub->inWait++;
            natsCondition_Wait(sub->cond, sub->mu);
            sub->inWait--;
        }

        if (sub->closed)
        {
            natsSub_Unlock(sub);
            break;
        }

        msg = sub->msgList.head;

        // Should not happen, but reported by code analysis otherwise.
        if (msg == NULL)
        {
            natsSub_Unlock(sub);
            continue;
        }

        delivered = ++(sub->delivered);

        sub->msgList.head = msg->next;

        if (sub->msgList.tail == msg)
            sub->msgList.tail = NULL;

        sub->msgList.msgs--;
        sub->msgList.bytes -= msg->dataLen;

        msg->next = NULL;

        // Capture this under lock.
        max = sub->max;

        natsSub_Unlock(sub);

        if ((max == 0) || (delivered <= max))
        {
           (*mcb)(nc, sub, msg, mcbClosure);
        }

        // Don't do 'else' because we need to remove when we have hit
        // the max (after the callback returns).
        if ((max > 0) && (delivered >= max))
        {
            // If we have hit the max for delivered msgs, remove sub.
            natsConn_removeSubscription(nc, sub, true);
            break;
        }
    }

    natsSub_release(sub);
}

void
natsSub_close(natsSubscription *sub, bool connectionClosed)
{
    natsSub_Lock(sub);

    sub->closed = true;
    sub->connClosed = connectionClosed;
    natsCondition_Broadcast(sub->cond);

    natsSub_Unlock(sub);
}

natsStatus
natsSub_create(natsSubscription **newSub, natsConnection *nc, const char *subj,
               const char *queueGroup, natsMsgHandler cb, void *cbClosure)
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
    sub->msgCb          = cb;
    sub->msgCbClosure   = cbClosure;
    sub->msgsLimit      = nc->opts->maxPendingMsgs;
    sub->bytesLimit     = sub->msgsLimit * 1024;

    if (sub->bytesLimit <= 0)
        return nats_setError(NATS_INVALID_ARG, "Invalid bytes limit of %d", sub->bytesLimit);

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
    return natsConn_subscribe(sub, nc, subject, NULL, cb, cbClosure);
}

/*
 * natsSubscribeSync is syntactic sugar for natsSubscribe(&sub, nc, subject, NULL).
 */
natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc, const char *subject)
{
    natsStatus s;

    s = natsConn_subscribe(sub, nc, subject, NULL, NULL, NULL);

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

    s = natsConn_subscribe(sub, nc, subject, queueGroup, cb, cbClosure);

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

    s = natsConn_subscribe(sub, nc, subject, queueGroup, NULL, NULL);

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

    nc = sub->conn;

    if (timeout > 0)
    {
        sub->inWait++;

        while ((sub->msgList.msgs == 0)
               && (s != NATS_TIMEOUT)
               && !(sub->closed))
        {
            if (target == 0)
                target = nats_Now() + timeout;

            s = natsCondition_AbsoluteTimedWait(sub->cond, sub->mu, target);
            if (s != NATS_OK)
                s = nats_setDefaultError(s);
        }

        sub->inWait--;

        if (sub->closed)
            s = nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }
    else
    {
        s = (sub->msgList.msgs == 0 ? NATS_TIMEOUT : NATS_OK);
        if (s != NATS_OK)
            s = nats_setDefaultError(s);
    }

    if (s == NATS_OK)
    {
        msg = sub->msgList.head;

        sub->msgList.head = msg->next;

        if (sub->msgList.tail == msg)
            sub->msgList.tail = NULL;

        sub->msgList.msgs--;
        sub->msgList.bytes -= msg->dataLen;

        msg->next = NULL;

        sub->delivered++;
        if (sub->max > 0)
        {
            if (sub->delivered > sub->max)
                s = nats_setDefaultError(NATS_MAX_DELIVERED_MSGS);
            else if (sub->delivered == sub->max)
                removeSub = true;
        }
    }
    if (s == NATS_OK)
        *nextMsg = msg;

    natsSub_Unlock(sub);

    if (removeSub)
        natsConn_removeSubscription(nc, sub, true);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unsubscribe(natsSubscription *sub, int max)
{
    natsStatus      s   = NATS_OK;
    natsConnection  *nc = NULL;

    if (sub == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);

    if (sub->connClosed)
        s = NATS_CONNECTION_CLOSED;
    else if (sub->closed)
        s = NATS_INVALID_SUBSCRIPTION;

    if (s != NATS_OK)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(s);
    }

    nc = sub->conn;
    _retain(sub);

    natsSub_Unlock(sub);

    s = natsConn_unsubscribe(nc, sub, max);

    natsSub_release(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Removes interest on the subject. Asynchronous subscription may still have
 * a callback in progress, in that case, the subscription will still be valid
 * until the callback returns.
 */
natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub)
{
    natsStatus s = _unsubscribe(sub, 0);
    return NATS_UPDATE_ERR_STACK(s);
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
    natsStatus s = _unsubscribe(sub, max);
    return NATS_UPDATE_ERR_STACK(s);
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

    if (msgs != NULL)
        *msgs = sub->msgList.msgs;

    if (bytes != NULL)
        *bytes = sub->msgList.bytes;

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

    sub->msgsLimit = msgLimit;
    sub->bytesLimit = bytesLimit;

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

    if (msgLimit != NULL)
        *msgLimit = sub->msgsLimit;

    if (bytesLimit != NULL)
        *bytesLimit = sub->bytesLimit;

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

    *msgs = (int64_t) sub->delivered;

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

    *msgs = sub->dropped;

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

    if (msgs != NULL)
        *msgs = sub->msgsMax;

    if (bytes != NULL)
        *bytes = sub->bytesMax;

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

    sub->msgsMax = 0;
    sub->bytesMax = 0;

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

    natsSub_Unlock(sub);

    if (doUnsub)
        (void) natsSubscription_Unsubscribe(sub);

    natsSub_release(sub);
}
