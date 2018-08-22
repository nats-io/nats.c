// Copyright 2018 The NATS Authors
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

#include "conn.h"
#include "sub.h"
#include "sopts.h"
#include "msg.h"

#include "../conn.h"
#include "../sub.h"
#include "../buf.h"

#ifdef DEV_MODE
// For type safety
void stanSub_Lock(stanSubscription *sub)   { natsMutex_Lock(sub->mu);   }
void stanSub_Unlock(stanSubscription *sub) { natsMutex_Unlock(sub->mu); }
#endif // DEV_MODE

static void
_freeStanSub(stanSubscription *sub)
{
    if (sub == NULL)
        return;

    NATS_FREE(sub->ackInbox);
    NATS_FREE(sub->channel);
    NATS_FREE(sub->inbox);
    NATS_FREE(sub->qgroup);
    NATS_FREE(sub->ackBuf);
    natsSubscription_Destroy(sub->inboxSub);
    stanSubOptions_Destroy(sub->opts);
    natsPBufAllocator_Destroy(sub->allocator);
    natsMutex_Destroy(sub->mu);

    NATS_FREE(sub);
}

void
stanSub_retain(stanSubscription *sub)
{
    if (sub == NULL)
        return;

    stanSub_Lock(sub);
    sub->refs++;
    stanSub_Unlock(sub);
}

void
stanSub_release(stanSubscription *sub)
{
    int refs = 0;

    if (sub == NULL)
        return;

    stanSub_Lock(sub);
    refs = --(sub->refs);
    stanSub_Unlock(sub);

    if (refs == 0)
        _freeStanSub(sub);
}

static void
_stanProcessMsg(natsConnection *nc, natsSubscription *ignored, natsMsg *msg, void *closure)
{
    natsStatus          s       = NATS_OK;
    stanSubscription    *sub    = (stanSubscription*) closure;
    Pb__MsgProto        *pbMsg  = NULL;
    stanMsg             *sMsg   = NULL;
    ProtobufCAllocator  *alloc  = (ProtobufCAllocator*) sub->allocator;

    natsPBufAllocator_Prepare(sub->allocator, msg->dataLen);

    pbMsg = pb__msg_proto__unpack(alloc, (size_t) msg->dataLen, (const uint8_t*) msg->data);
    if (pbMsg == NULL)
    {
        natsMsg_Destroy(msg);
        return;
    }

    s = stanMsg_create(&sMsg, sub, pbMsg);
    if (s == NATS_OK)
    {
        stanMsgHandler  cb          = NULL;
        void            *cbClosure  = NULL;
        stanConnection  *sc         = NULL;
        char            *channel    = NULL;
        bool            sendAck     = false;
        char            *ackSubject = NULL;
        bool            flush       = false;
        char            *ackBuf     = NULL;
        int             ackSize     = 0;
        Pb__Ack         ack;

        stanSub_Lock(sub);
        if (sub->closed)
            s = NATS_INVALID_SUBSCRIPTION;
        if (s == NATS_OK)
        {
            sc = sub->sc;
            cb = sub->cb;
            cbClosure = sub->cbClosure;
            channel = sub->channel;
            sendAck = sub->opts->manualAcks == false;
            ackSubject = sub->ackInbox;
            // Prepare buf for ack
            if (sendAck)
            {
                if (++sub->msgs == sub->opts->maxInflight)
                {
                    sub->msgs = 0;
                    flush = true;
                }
                pb__ack__init(&ack);
                ack.subject = channel;
                ack.sequence = sMsg->seq;

                ackSize = (int) pb__ack__get_packed_size(&ack);
                if (ackSize > sub->ackBufCap)
                    s = expandBuf(&sub->ackBuf, &sub->ackBufCap, 2*ackSize);

                if (s == NATS_OK)
                    ackBuf = sub->ackBuf;
            }
        }
        stanSub_Unlock(sub);

        if (s == NATS_OK)
        {
            (*cb)(sc, sub, channel, sMsg, cbClosure);

            if (sendAck)
            {
                int packedSize = 0;

                packedSize = (int) pb__ack__pack(&ack, (uint8_t*) ackBuf);
                if (ackSize == packedSize)
                    natsConn_publish(nc, ackSubject, NULL, (const void*) ackBuf, ackSize, flush);
            }
        }
        else
        {
            // Since we didn't pass to callback, need to destroy.
            stanMsg_Destroy(sMsg);
        }
    }

    natsMsg_Destroy(msg);

    pb__msg_proto__free_unpacked(pbMsg, alloc);
}

natsStatus
stanSubscription_AckMsg(stanSubscription *sub, stanMsg *msg)
{
    natsStatus      s       = NATS_OK;
    natsConnection  *nc     = NULL;
    char            *ackBuf = NULL;
    bool            flush   = false;
    char            *ackSub = NULL;
    int             ackSize = 0;
    Pb__Ack         ack;

    if (sub == NULL)
        return NATS_OK;

    if (msg == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    stanSub_Lock(sub);
    if (sub->closed)
    {
        stanSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }
    if (!sub->opts->manualAcks)
    {
        stanSub_Unlock(sub);
        return nats_setError(NATS_ERR, "%s", STAN_ERR_MANUAL_ACK);
    }
    if (msg->sub != sub)
    {
        stanSub_Unlock(sub);
        return nats_setError(NATS_ILLEGAL_STATE, "%s", STAN_ERR_SUB_NOT_OWNER);
    }

    if (++sub->msgs == sub->opts->maxInflight)
    {
        sub->msgs = 0;
        flush = true;
    }

    nc     = sub->sc->nc;
    ackSub = sub->ackInbox;

    pb__ack__init(&ack);
    ack.subject = sub->channel;
    ack.sequence = msg->seq;

    stanSub_Unlock(sub);

    ackSize = (int) pb__ack__get_packed_size(&ack);
    if (ackSize == 0)
    {
        s = nats_setError(NATS_ERR, "%s", "message acknowledgment protocol packed size is 0");
    }
    else
    {
        char    ackBuf[1024];
        char    *ackBytes  = NULL;
        int     packedSize = 0;
        bool    needFree   = false;

        if (ackSize > (int) sizeof(ackBuf))
        {
            ackBytes = NATS_MALLOC(ackSize);
            if (ackBytes == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
                needFree = true;
        }
        else
        {
            ackBytes = (char*) ackBuf;
        }
        if (s == NATS_OK)
        {
            packedSize = (int) pb__ack__pack(&ack, (uint8_t*) ackBuf);
            if (ackSize != packedSize)
                s = nats_setError(NATS_ERR, "message acknowledgment protocol computed packed size is %d, got %d",
                        ackSize, packedSize);
            else
                s = natsConn_publish(nc, ackSub, NULL, (const void*) ackBuf, ackSize, flush);

            if (needFree)
                NATS_FREE(ackBytes);
        }
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_releaseStanSubCB(void *closure)
{
    stanSubscription *sub = (stanSubscription*) closure;
    stanConnection   *sc  = NULL;
    int              refs;

    stanSub_Lock(sub);
    sc = sub->sc;
    refs = --sub->refs;
    stanSub_Unlock(sub);

    if (refs == 0)
        _freeStanSub(sub);

    stanConn_release(sc);
}

static natsStatus
stanConn_subscribe(stanSubscription **newSub, stanConnection *sc,
        const char *channel, const char *queue,
        stanMsgHandler cb, void *cbClosure,
        stanSubOptions *opts)
{
    natsStatus          s      = NATS_OK;
    stanSubscription    *sub   = NULL;
    natsConnection      *nc    = NULL;
    char                *cid   = NULL;
    char                *rSubj = NULL;
    int64_t             timeout= 0;

    if ((newSub == NULL)
            || (sc == NULL)
            || (channel == NULL)
            || (cb == NULL))
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }

    stanConn_Lock(sc);
    if (sc->closed)
    {
        stanConn_Unlock(sc);
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    sub = NATS_CALLOC(1, sizeof(stanSubscription));
    if (sub == NULL)
    {
        stanConn_Unlock(sc);
        return nats_setDefaultError(NATS_NO_MEMORY);
    }

    s = natsMutex_Create(&sub->mu);
    if (s != NATS_OK)
    {
        stanConn_Unlock(sc);
        NATS_FREE(sub);
        return NATS_UPDATE_ERR_STACK(s);
    }

    // Retain the connection until we have fully setup the subscription
    // since we will release the lock at one point.
    sc->refs++;

    // Capture some stan connection fields. We know they will be valid
    // even if the connection is closed, because we have retained the
    // object.
    nc = sc->nc;
    cid = sc->clientID;
    rSubj = sc->subRequests;
    timeout = sc->opts->connTimeout;

    stanConn_Unlock(sc);

    // Lock the subscription while we set it up.
    stanSub_Lock(sub);

    sub->refs = 1;
    sub->sc = sc;
    sub->cb = cb;
    sub->cbClosure = cbClosure;

    if (opts != NULL)
        s = stanSubOptions_clone(&sub->opts, opts);
    else
        s = stanSubOptions_Create(&sub->opts);

    IF_OK_DUP_STRING(s, sub->channel, channel);
    if ((s == NATS_OK) && queue != NULL)
        DUP_STRING(s, sub->qgroup, queue);
    if (s == NATS_OK)
        s = natsPBufAllocator_Create(&sub->allocator, sizeof(Pb__MsgProto), 2);
    if (s == NATS_OK)
        s = natsInbox_Create((natsInbox**) &sub->inbox);

    if (s == NATS_OK)
    {
        s = natsConnection_Subscribe(&sub->inboxSub, nc, sub->inbox, _stanProcessMsg, (void*) sub);
        if (s == NATS_OK)
        {
            natsSubscription_SetPendingLimits(sub->inboxSub, -1, -1);
            // Retain both sub and sc
            sub->refs++;
            stanConn_retain(sc);
            s = natsSub_setOnCompleteCB(sub->inboxSub, _releaseStanSubCB, (void*) sub);
            if (s != NATS_OK)
            {
                sub->refs--;
                stanConn_release(sc);
            }
        }
        if (s == NATS_OK)
        {
            Pb__SubscriptionRequest subReq;
            int                     reqSize   = 0;
            char                    *reqBytes = NULL;
            natsMsg                 *replyMsg = NULL;

            pb__subscription_request__init(&subReq);
            subReq.clientid = cid;
            subReq.subject = sub->channel;
            subReq.qgroup = sub->qgroup;
            subReq.inbox = sub->inbox;
            subReq.maxinflight = sub->opts->maxInflight;
            subReq.ackwaitinsecs = (int32_t)(sub->opts->ackWait / 1000);
            subReq.startposition = sub->opts->startAt;
            subReq.durablename = sub->opts->durableName;

            if (subReq.startposition == PB__START_POSITION__TimeDeltaStart)
            {
                subReq.starttimedelta = (nats_Now() - sub->opts->startTime) * (int64_t) 1000000;
            }
            else if (subReq.startposition == PB__START_POSITION__SequenceStart)
            {
                subReq.startsequence = sub->opts->startSequence;
            }

            reqSize = (int) pb__subscription_request__get_packed_size(&subReq);
            if (reqSize == 0)
            {
                s = nats_setError(NATS_ERR, "%s", "subscription request protocol packed size is 0");
            }
            else
            {
                reqBytes = NATS_MALLOC(reqSize);
                if (reqBytes == NULL)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            if (s == NATS_OK)
            {
                int packedSize = (int) pb__subscription_request__pack(&subReq, (uint8_t*) reqBytes);
                if (reqSize != packedSize)
                {
                    s = nats_setError(NATS_ERR, "subscription request protocol computed packed size is %d, got %d",
                            reqSize, packedSize);
                }
                else
                {
                    s = natsConnection_Request(&replyMsg, nc, rSubj, (const void*) reqBytes, reqSize, timeout);
                    if (s == NATS_TIMEOUT)
                        NATS_UPDATE_ERR_TXT("%s", STAN_ERR_SUBSCRIBE_REQUEST_TIMEOUT);
                }

                NATS_FREE(reqBytes);
            }
            if (s == NATS_OK)
            {
                Pb__SubscriptionResponse *subResp = NULL;

                subResp = pb__subscription_response__unpack(NULL,
                        (size_t) natsMsg_GetDataLength(replyMsg),
                        (const uint8_t*) natsMsg_GetData(replyMsg));
                if (subResp == NULL)
                    s = nats_setError(NATS_ERR, "%s", "unable to decode subscription response");

                if ((s == NATS_OK) && (strlen(subResp->error) > 0))
                    s = nats_setError(NATS_ERR, "%s", subResp->error);

                IF_OK_DUP_STRING(s, sub->ackInbox, subResp->ackinbox);

                pb__subscription_response__free_unpacked(subResp, NULL);

                natsMsg_Destroy(replyMsg);
            }

            // If there was an error, need to unsub.
            if (s != NATS_OK)
                natsSubscription_Unsubscribe(sub->inboxSub);
        }
    }
    stanSub_Unlock(sub);

    if (s == NATS_OK)
        *newSub = sub;
    else
        stanSub_release(sub);

    stanConn_release(sc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanConnection_Subscribe(stanSubscription **newSub, stanConnection *sc,
        const char *channel,
        stanMsgHandler cb, void *cbClosure,
        stanSubOptions *opts)
{
    natsStatus s;

    s = stanConn_subscribe(newSub, sc, channel, NULL, cb, cbClosure, opts);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanConnection_QueueSubscribe(stanSubscription **newSub, stanConnection *sc,
        const char *channel, const char *queueGroup,
        stanMsgHandler cb, void *cbClosure,
        stanSubOptions *opts)
{
    natsStatus s;

    s = stanConn_subscribe(newSub, sc, channel, queueGroup, cb, cbClosure, opts);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_closeOrUnsubscribeStanSub(stanSubscription *sub, bool doClose)
{
    natsStatus              s           = NATS_OK;
    stanConnection          *sc         = NULL;
    natsConnection          *nc         = NULL;
    char                    *reqSubj    = NULL;
    char                    *cid        = NULL;
    char                    *subj       = NULL;
    char                    *ackInbox   = NULL;
    int64_t                 timeout     = 0;
    Pb__UnsubscribeRequest  usr;
    int                     usrSize     = 0;

    stanSub_Lock(sub);
    if (sub->closed)
    {
        stanSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }
    sub->closed = true;
    natsSubscription_Unsubscribe(sub->inboxSub);
    sc          = sub->sc;
    ackInbox    = sub->ackInbox;
    subj        = sub->channel;
    stanSub_Unlock(sub);

    stanConn_Lock(sc);
    if (sc->closed)
    {
        stanConn_Unlock(sc);
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }
    reqSubj = sc->unsubRequests;
    if (doClose)
    {
        reqSubj = sc->subCloseRequests;
        if (reqSubj == NULL)
        {
            stanConn_Unlock(sc);
            s = nats_setError(NATS_NO_SERVER_SUPPORT, "%s", STAN_ERR_SUB_CLOSE_NOT_SUPPORTED);
            return s;
        }
    }
    nc      = sc->nc;
    cid     = sc->clientID;
    timeout = sc->opts->connTimeout;
    stanConn_Unlock(sc);

    pb__unsubscribe_request__init(&usr);
    usr.clientid = cid;
    usr.subject  = subj;
    usr.inbox    = ackInbox;

    usrSize = (int) pb__unsubscribe_request__get_packed_size(&usr);
    if (usrSize == 0)
    {
        s = nats_setError(NATS_ERR, "%s subscription request protocol packed size is 0",
                (doClose ? "close" : "unsubscribe"));
    }
    else
    {
        natsMsg *replyMsg = NULL;
        char    *usrBytes = NATS_MALLOC(usrSize);

        if (usrBytes == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (s == NATS_OK)
        {
            int packedSize = (int) pb__unsubscribe_request__pack(&usr, (uint8_t*) usrBytes);
            if (usrSize != packedSize)
            {
                s = nats_setError(NATS_ERR, "%s subscription protocol computed packed size is %v, got %v",
                        (doClose ? "close" : "unsubscribe"), usrSize, packedSize);
            }
            else
            {
                s = natsConnection_Request(&replyMsg, nc, reqSubj, (const void*) usrBytes, usrSize, timeout);
                if (s == NATS_TIMEOUT)
                    NATS_UPDATE_ERR_TXT("%s", (doClose ? STAN_ERR_CLOSE_REQUEST_TIMEOUT : STAN_ERR_UNSUBSCRIBE_REQUEST_TIMEOUT));
            }

            NATS_FREE(usrBytes);

            if (s == NATS_OK)
            {
                Pb__SubscriptionResponse *resp = NULL;

                resp = pb__subscription_response__unpack(NULL,
                        (size_t) natsMsg_GetDataLength(replyMsg),
                        (const uint8_t*) natsMsg_GetData(replyMsg));

                if (resp == NULL)
                    s = nats_setError(NATS_ERR, "%s", "unable to decode subscription response");

                if ((s == NATS_OK) && (strlen(resp->error) > 0))
                    s = nats_setError(NATS_ERR, "%s", resp->error);


                pb__subscription_response__free_unpacked(resp, NULL);

                natsMsg_Destroy(replyMsg);
            }
        }
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanSubscription_Unsubscribe(stanSubscription *sub)
{
    natsStatus s;

    if (sub == NULL)
        return NATS_OK;

    s = _closeOrUnsubscribeStanSub(sub, false);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanSubscription_Close(stanSubscription *sub)
{
    natsStatus s;

    if (sub == NULL)
        return NATS_OK;

    s = _closeOrUnsubscribeStanSub(sub, true);
    return NATS_UPDATE_ERR_STACK(s);
}

void
stanSubscription_Destroy(stanSubscription *sub)
{
    if (sub == NULL)
        return;

    _closeOrUnsubscribeStanSub(sub, true);
    stanSub_release(sub);
}
