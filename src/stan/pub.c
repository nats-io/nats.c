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

#include "pub.h"
#include "conn.h"

#include "../conn.h"

static void
_pubAckFree(_pubAck *pa)
{
    if ((pa->error != NULL) && !pa->dontFreeError)
        NATS_FREE(pa->error);
    NATS_FREE(pa->guid);
    NATS_FREE(pa);
}

static void
_pubAckRemoveFromList(stanConnection *sc, _pubAck *pa)
{
    if ((pa->prev != NULL) || (pa->next != NULL))
    {
        if (pa->prev != NULL)
            pa->prev->next = pa->next;
        if (pa->next != NULL)
            pa->next->prev = pa->prev;
    }
    if (pa == sc->pubAckHead)
        sc->pubAckHead = pa->next;
    if (pa == sc->pubAckTail)
        sc->pubAckTail = pa->prev;

    pa->prev = NULL;
    pa->next = NULL;
}

static void
_stanPossiblyReleasePublishCall(stanConnection *sc)
{
    if (natsStrHash_Count(sc->pubAckMap) < sc->pubAckMaxInflightThreshold)
        natsCondition_Broadcast(sc->pubAckMaxInflightCond);
}

void
stanProcessPubAck(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    stanConnection  *sc     = (stanConnection*) closure;
    uint8_t         *data   = NULL;
    size_t          dataLen = 0;

    data    = (uint8_t*) natsMsg_GetData(msg);
    dataLen = (size_t) natsMsg_GetDataLength(msg);

    if (dataLen > 0)
    {
        ProtobufCAllocator  *alloc = (ProtobufCAllocator*) sc->pubAckAllocator;
        Pb__PubAck          *pubAck;

        natsPBufAllocator_Prepare(sc->pubAckAllocator, (int) dataLen);

        pubAck = pb__pub_ack__unpack(alloc, dataLen, data);
        if (pubAck != NULL)
        {
            _pubAck *pa;
            char    *error = NULL;
            bool    ic = false;

            if ((pubAck->error != NULL) && (pubAck->error[0] != '\0'))
                error = pubAck->error;

            natsMutex_Lock(sc->pubAckMu);
            pa = natsStrHash_Remove(sc->pubAckMap, pubAck->guid);
            // It could have been removed by the publish calls.
            if (pa != NULL)
            {
                // For sync publish, we need to update `pa`
                // and wake up caller.
                if (pa->isSync)
                {
                    // Mark that the pub ack was received
                    pa->received = true;
                    // Get the error if any
                    if (error != NULL)
                    {
                        pa->error = NATS_STRDUP(error);
                        if (pa->error == NULL)
                        {
                            pa->error = (char*) "no memory copying error";
                            pa->dontFreeError = true;
                        }
                    }
                    // Wake up caller if needed.
                    if (sc->pubAckInWait > 0)
                        natsCondition_Broadcast(sc->pubAckCond);
                }
                else
                {
                    // Remove from list
                    _pubAckRemoveFromList(sc, pa);

                    // Indicate that we need to invoke the callback
                    ic = true;
                }

                // Check for possible blocked publish call and release if needed
                if (sc->pubAckMaxInflightInWait)
                    _stanPossiblyReleasePublishCall(sc);
            }
            natsMutex_Unlock(sc->pubAckMu);

            // Asynchronous publish calls only..
            // We don't check for ((pa != NULL) && !pa->isSync)) here
            // because for sync calls, `pa` may have been freed by
            // now in stanConnection_Publish().
            if (ic)
            {
                // If handler specified, invoke here.
                if (pa->ah != NULL)
                    (*pa->ah)(pubAck->guid, error, pa->ahClosure);

                // Done with `pa`.
                _pubAckFree(pa);
            }

            pb__pub_ack__free_unpacked(pubAck, alloc);
        }
    }

    natsMsg_Destroy(msg);
}

static void
_pubAckTimerCB(natsTimer *timer, void* closure)
{
    stanConnection      *sc     = (stanConnection*) closure;
    _pubAck             *pa     = NULL;
    bool                ic      = false;
    const char          *err    = NULL;
    bool                closed  = false;
    bool                done    = false;

    for (;!done;)
    {
        ic = false;

        natsMutex_Lock(sc->pubAckMu);
        closed = sc->pubAckClosed;
        if (sc->pubAckHead != NULL)
        {
            int64_t now = nats_Now();

            pa = sc->pubAckHead;

            // Check that we are at or past the deadline
            if (closed || (now >= pa->deadline))
            {
                natsStrHash_Remove(sc->pubAckMap, pa->guid);
                // This will update the head
                _pubAckRemoveFromList(sc, pa);
                // Check for possible blocked publish call and release if needed
                if (!sc->pubAckClosed && sc->pubAckMaxInflightInWait)
                    _stanPossiblyReleasePublishCall(sc);
                // We should invoke the callback
                ic = true;
            }

            if (!closed)
            {
                // Reset timer, either with current head but new timeout
                // or to the new head's deadline.
                if (sc->pubAckHead != NULL)
                {
                    // If next deadline is really close, consider that
                    // it will expire in this iteration. Set its deadline
                    // to now and don't reset timer yet.
                    if (sc->pubAckHead->deadline-now <= 5)
                    {
                        sc->pubAckHead->deadline = now;
                    }
                    else
                    {
                        natsTimer_Reset(sc->pubAckTimer, sc->pubAckHead->deadline-now);
                        // Stop the 'for' loop
                        done = true;
                    }
                }
                else
                {
                    // Set to an hour, but mark that this need a reset when
                    // a new async message is published.
                    natsTimer_Reset(sc->pubAckTimer, 60*60*1000);
                    sc->pubAckTimerNeedReset = true;
                    // Stop the 'for' loop
                    done = true;
                }
            }
        }
        else
        {
            done = true;
        }
        natsMutex_Unlock(sc->pubAckMu);

        if (ic && (pa != NULL))
        {
            // Handler may not be set.
            if (pa->ah != NULL)
            {
                if (closed)
                    err = natsStatus_GetText(NATS_CONNECTION_CLOSED);
                else
                    err = STAN_ERR_PUB_ACK_TIMEOUT;
                (*pa->ah)(pa->guid, (char*) err, pa->ahClosure);
            }
            // Done with `pa`.
            _pubAckFree(pa);
        }
    }

    if (closed)
    {
        natsMutex_Lock(sc->mu);
        natsTimer_Stop(sc->pubAckTimer);
        natsMutex_Unlock(sc->mu);
    }
}

static void
_pubAckTimerStopCB(natsTimer *timer, void* closure)
{
    stanConnection *sc = (stanConnection*) closure;
    stanConn_release(sc);
}

#define GUID_LEN   (23)

static natsStatus
_stanPublish(stanConnection *sc, const char *channel, const void *data, int dataLen,
             bool isSync, int64_t *deadline, _pubAck *pa)
{
    natsStatus  s = NATS_OK;
    int64_t     ackTimeout = 0;

    if (sc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (channel == NULL)
        return nats_setDefaultError(NATS_INVALID_SUBJECT);

    stanConn_Lock(sc);
    if (sc->closed)
    {
        stanConn_Unlock(sc);
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    pa->guid = NATS_MALLOC(GUID_LEN);
    if (pa->guid == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if (s == NATS_OK)
        s = natsNUID_Next(pa->guid, GUID_LEN);
    if (s == NATS_OK)
    {
        Pb__PubMsg  pubReq;
        const void  *pubBytes = NULL;
        int         pubSize   = 0;

        pb__pub_msg__init(&pubReq);
        pubReq.clientid = sc->clientID;
        pubReq.connid.data = (uint8_t*) sc->connID;
        pubReq.connid.len = sc->connIDLen;
        pubReq.subject = (char*) channel;
        pubReq.guid = pa->guid;
        pubReq.data.data = (uint8_t*) data;
        pubReq.data.len = dataLen;

        pubSize = (int) pb__pub_msg__get_packed_size(&pubReq);
        if (pubSize == 0)
        {
            s = nats_setError(NATS_ERR, "%s", "publish message protocol packed size is 0");
        }
        else
        {
            int chanLen = (int) strlen(channel);
            int subjLen = sc->pubPrefixLen + 1 + chanLen + 1;

            if (subjLen > sc->pubSubjBufCap)
                s = expandBuf(&sc->pubSubjBuf, &sc->pubSubjBufCap, subjLen);

            if (pubSize > sc->pubMsgBufCap)
                s = expandBuf(&sc->pubMsgBuf, &sc->pubMsgBufCap, (int)((float)pubSize * 1.1));

            if (s == NATS_OK)
            {
                char *ptr;

                ptr = sc->pubSubjBuf;
                // We know the buffer is big enough, so copy directly
                memcpy(ptr, sc->pubPrefix, sc->pubPrefixLen);
                ptr += sc->pubPrefixLen;
                ptr[0]='.';
                ptr++;
                memcpy(ptr, channel, chanLen);
                ptr += chanLen;
                ptr[0]='\0';
            }
            if (s == NATS_OK)
            {
                natsMutex_Lock(sc->pubAckMu);

                // If calling Close() while stuck in the condition wait below, this
                // flag will be set to true (under pubAckMu) by Close() to kick us
                // out and make sure we don't go back right at it.

                // Check if we should block due to maxInflight. Since we are under
                // connection's lock, there can be at most one Publis[Async]() call
                // blocked here (the other would be blocked at top of function trying
                // to grab the connection's lock).
                while (!sc->pubAckClosed && (natsStrHash_Count(sc->pubAckMap) == sc->opts->maxPubAcksInflight))
                {
                    sc->pubAckMaxInflightInWait = true;
                    natsCondition_Wait(sc->pubAckMaxInflightCond, sc->pubAckMu);
                    sc->pubAckMaxInflightInWait = false;
                }

                // We could be closing, but stanConnection_Close() is waiting for
                // sc->mu to be released. Still, we can fail this publish call.
                if (sc->pubAckClosed)
                    s = nats_setDefaultError(NATS_CONNECTION_CLOSED);

                if (s == NATS_OK)
                {
                    // Compute absolute time based on current time and the pub ack timeout.
                    ackTimeout = nats_Now() + sc->opts->pubAckTimeout;

                    // For Publish() calls, store in map, no need to copy keep since it is in pa.
                    if (isSync)
                    {
                        s = natsStrHash_Set(sc->pubAckMap, pa->guid, false, (void*) pa, NULL);
                    }
                    else
                    {
                        pa->deadline = ackTimeout;

                        s = natsStrHash_Set(sc->pubAckMap, pa->guid, false, (void*) pa, NULL);

                        // Add to list and create timer if needed
                        if (s == NATS_OK)
                        {
                            // If timer was never created, create now
                            if (sc->pubAckTimer == NULL)
                            {
                                s = natsTimer_Create(&sc->pubAckTimer, _pubAckTimerCB, _pubAckTimerStopCB, sc->opts->pubAckTimeout, (void*) sc);
                                if (s == NATS_OK)
                                    sc->refs++;
                            }
                            else if (sc->pubAckTimerNeedReset)
                            {
                                natsTimer_Reset(sc->pubAckTimer, sc->opts->pubAckTimeout);
                                sc->pubAckTimerNeedReset = false;
                            }
                            if (s == NATS_OK)
                            {
                                // Add to list
                                if (sc->pubAckTail != NULL)
                                {
                                    pa->prev = sc->pubAckTail;
                                    sc->pubAckTail->next = pa;
                                }
                                else
                                {
                                    sc->pubAckHead = pa;
                                }
                                sc->pubAckTail = pa;
                            }
                            else
                            {
                                natsStrHash_Remove(sc->pubAckMap, pa->guid);
                            }
                        }
                    }
                }
                natsMutex_Unlock(sc->pubAckMu);
            }
            if (s == NATS_OK)
            {
                int packedSize;

                packedSize = (int) pb__pub_msg__pack(&pubReq, (uint8_t*) sc->pubMsgBuf);
                if (pubSize != packedSize)
                {
                    s = nats_setError(NATS_ERR, "publish message protocol computed packed size is %d, got %d",
                            pubSize, packedSize);
                }
                else
                {
                    // Use private function to cause flush of buffer in place if sync call
                    s = natsConn_publish(sc->nc, sc->pubSubjBuf, sc->ackSubject, sc->pubMsgBuf, pubSize, isSync);
                }
                if ((s != NATS_OK) && (pa != NULL))
                {
                    // Since we may not have sent the message, remove the pa from map
                    natsMutex_Lock(sc->pubAckMu);
                    natsStrHash_Remove(sc->pubAckMap, pa->guid);
                    // Only PublishAsync() calls add `pa` to the list
                    if (!isSync)
                        _pubAckRemoveFromList(sc, pa);
                    natsMutex_Unlock(sc->pubAckMu);
                }
            }
        }
    }
    // On success, retain for sync calls.
    if ((s == NATS_OK) && isSync)
        sc->refs++;
    stanConn_Unlock(sc);

    if ((s == NATS_OK) && isSync)
        *deadline = ackTimeout;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanConnection_Publish(stanConnection *sc, const char *channel, const void *data, int dataLen)
{
    natsStatus  s;
    int64_t     deadline = 0;
    _pubAck     *pa = NULL;
    bool        releaseConn = false;

    pa = NATS_CALLOC(1, sizeof(_pubAck));
    if (pa == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    pa->isSync = true;

    s = _stanPublish(sc, channel, data, dataLen, true, &deadline, pa);
    if (s == NATS_OK)
    {
        bool received = false;
        bool closed   = false;

        // On _stanPublish success, we need to release connection at the end
        // of this function.
        releaseConn = true;

        while ((s != NATS_TIMEOUT) && !received && !closed)
        {
            natsMutex_Lock(sc->pubAckMu);
            received = pa->received;
            closed = sc->pubAckClosed;
            if (!closed && !received)
            {
                sc->pubAckInWait++;
                s = natsCondition_AbsoluteTimedWait(sc->pubAckCond, sc->pubAckMu, deadline);
                sc->pubAckInWait--;
            }
            natsMutex_Unlock(sc->pubAckMu);
        }
        if ((s == NATS_OK) && !received && closed)
            s = nats_setDefaultError(NATS_CONNECTION_CLOSED);
        if (s != NATS_OK)
        {
            // Regardless the error, we need to remove from map
            natsMutex_Lock(sc->pubAckMu);
            // If we cannot remove, it means we just received the ack
            // and need to proceed with "success" branch.
            if (natsStrHash_Remove(sc->pubAckMap, pa->guid) != NULL)
            {
                // For timeout, augment the error text
                if (s == NATS_TIMEOUT)
                    NATS_UPDATE_ERR_TXT("%s", STAN_ERR_PUB_ACK_TIMEOUT);
            }
            else
            {
                s = NATS_OK;
                // Error was set in condition wait, so clear.
                nats_clearLastError();
            }
            natsMutex_Unlock(sc->pubAckMu);
        }
        if (s == NATS_OK)
        {
            // PubAck was received, if error report that error.
            if (pa->error != NULL)
                s = nats_setError(NATS_ERR, "%s", pa->error);
        }
    }
    // Done with `pa`.
    _pubAckFree(pa);

    if (releaseConn)
        stanConn_release(sc);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
stanConnection_PublishAsync(stanConnection *sc, const char *channel,
                            const void *data, int dataLen,
                            stanPubAckHandler ah, void *ahClosure)
{
    natsStatus  s;
    _pubAck     *pa = NULL;

    pa = NATS_CALLOC(1, sizeof(_pubAck));
    if (pa == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // These are possibly NULL.
    pa->ah        = ah;
    pa->ahClosure = ahClosure;

    s = _stanPublish(sc, channel, data, dataLen, false, NULL, pa);
    // If error, `pa` has not been stored (or stored, but then removed).
    // It is our responsibility to free it here.
    if (s != NATS_OK)
        _pubAckFree(pa);

    return NATS_UPDATE_ERR_STACK(s);
}
