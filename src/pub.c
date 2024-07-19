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

#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "nuid.h"
#include "mem.h"

static const char *digits = "0123456789";

#define _publishMsg(n, m, r) natsConn_publish((n), (m), (r), false)

#define GETBYTES_SIZE(len, b, i) {\
    if ((len) > 0)\
    {\
        int l;\
        for (l = (len); l > 0; l /= 10)\
        {\
            (i) -= 1;\
            (b)[(i)] = digits[l%10];\
        }\
    }\
    else\
    {\
        (i) -= 1;\
        (b)[(i)] = digits[0];\
    }\
}

// This represents the maximum size of a byte array containing the
// string representation of a hdr/msg size. See GETBYTES_SIZE.
#define BYTES_SIZE_MAX (12)

// _publish is the internal function to publish messages to a nats server.
// Sends a protocol data message by queueing into the bufio writer
// and kicking the flusher thread. These writes should be protected.
natsStatus
natsConn_publish(natsConnection *nc, natsMsg *msg, const char *reply, bool directFlush)
{
    natsStatus  s               = NATS_OK;
    int         msgHdSize       = 0;
    char        dlb[BYTES_SIZE_MAX];
    int         dli             = BYTES_SIZE_MAX;
    int         dlSize          = 0;
    char        hlb[BYTES_SIZE_MAX];
    int         hli             = BYTES_SIZE_MAX;
    int         hlSize          = 0;
    int         subjLen         = 0;
    int         replyLen        = 0;
    bool        reconnecting    = false;
    int         ppo             = 1; // pub proto offset
    int         hdrl            = 0;
    int         totalLen        = 0;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((msg->subject == NULL)
        || ((subjLen = (int) strlen(msg->subject)) == 0))
    {
        return nats_setDefaultError(NATS_INVALID_SUBJECT);
    }

    // If a reply is provided through params, use that one,
    // otherwise fallback to msg->reply.
    if (reply == NULL)
        reply = msg->reply;

    replyLen = ((reply != NULL) ? (int) strlen(reply) : 0);

    natsConn_Lock(nc);

    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);

        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    if (natsConn_isDrainingPubs(nc))
    {
        natsConn_Unlock(nc);

        return nats_setDefaultError(NATS_DRAINING);
    }

    // We can have headers NULL but needsLift which means we are in special
    // situation where a message was received and is sent back without the user
    // accessing the headers. It should still be considered having headers.
    if ((msg->headers != NULL) || natsMsg_needsLift(msg))
    {
        // Do the check for server's headers support only after we have completed
        // the initial connect (we could be here with initc true - that is, initial
        // connect in progress - when using natsOptions_SetRetryOnFailedConnect
        // option).
        if (!nc->initc && !nc->info.headers)
        {
            natsConn_Unlock(nc);

            return nats_setDefaultError(NATS_NO_SERVER_SUPPORT);
        }

        hdrl = natsMsgHeader_encodedLen(msg);
        if (hdrl > 0)
        {
            GETBYTES_SIZE(hdrl, hlb, hli)
            hlSize = (BYTES_SIZE_MAX - hli);
            ppo = 0;
            totalLen = hdrl;
        }
    }
    // This will represent headers + data
    totalLen += msg->dataLen;

    if (!nc->initc && ((int64_t) totalLen > nc->info.maxPayload))
    {
        natsConn_Unlock(nc);

        return nats_setError(NATS_MAX_PAYLOAD,
                             "Payload %d greater than maximum allowed: %" PRId64,
                             totalLen, nc->info.maxPayload);
    }

    // Check if we are reconnecting, and if so check if
    // we have exceeded our reconnect outbound buffer limits.
    if ((reconnecting = natsConn_isReconnecting(nc)))
    {
        // Check if we are over
        if (natsBuf_Len(nc->pending) >= nc->opts->reconnectBufSize)
        {
            natsConn_Unlock(nc);
            return nats_setDefaultError(NATS_INSUFFICIENT_BUFFER);
        }
    }

    GETBYTES_SIZE(totalLen, dlb, dli)
    dlSize = (BYTES_SIZE_MAX - dli);

    // We include the NATS headers in the message header scratch.
    msgHdSize = (_HPUB_P_LEN_ - ppo)
                + subjLen + 1
                + (replyLen > 0 ? replyLen + 1 : 0)
                + (hdrl > 0 ? hlSize + 1 + hdrl : 0)
                + dlSize + _CRLF_LEN_;

    natsBuf_MoveTo(nc->scratch, _HPUB_P_LEN_);

    if (natsBuf_Capacity(nc->scratch) < msgHdSize)
    {
        // Although natsBuf_Append() would make sure that the buffer
        // grows, it is better to make sure that the buffer is big
        // enough for the pre-calculated size. We make it even a bit bigger.
        s = natsBuf_Expand(nc->scratch, (int) ((float)msgHdSize * 1.1));
    }

    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, msg->subject, subjLen);
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, _SPC_, _SPC_LEN_);
    if ((s == NATS_OK) && (reply != NULL))
    {
        s = natsBuf_Append(nc->scratch, reply, replyLen);
        if (s == NATS_OK)
            s = natsBuf_Append(nc->scratch, _SPC_, _SPC_LEN_);
    }
    if ((s == NATS_OK) && (hdrl > 0))
    {
        s = natsBuf_Append(nc->scratch, (hlb+hli), hlSize);
        if (s == NATS_OK)
            s = natsBuf_Append(nc->scratch, _SPC_, _SPC_LEN_);
    }
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, (dlb+dli), dlSize);
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, _CRLF_, _CRLF_LEN_);
    if ((s == NATS_OK) && hdrl > 0)
        s = natsMsgHeader_encode(nc->scratch, msg);

    if (s == NATS_OK)
    {
        int pos = 0;

        if (reconnecting)
            pos = natsBuf_Len(nc->pending);
        else
            SET_WRITE_DEADLINE(nc);

        s = natsConn_bufferWrite(nc, natsBuf_Data(nc->scratch)+ppo, msgHdSize);

        if (s == NATS_OK)
            s = natsConn_bufferWrite(nc, msg->data, msg->dataLen);

        if (s == NATS_OK)
            s = natsConn_bufferWrite(nc, _CRLF_, _CRLF_LEN_);

        if ((s != NATS_OK) && reconnecting)
            natsBuf_MoveTo(nc->pending, pos);
    }

    if ((s == NATS_OK) && !reconnecting)
    {
        if (directFlush)
            s = natsConn_bufferFlush(nc);
        else
            s = natsConn_flushOrKickFlusher(nc);
    }

    if (s == NATS_OK)
    {
        nc->stats.outMsgs  += 1;
        nc->stats.outBytes += totalLen;
    }

    natsConn_Unlock(nc);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Publishes the data argument to the given subject. The data argument is left
 * untouched and needs to be correctly interpreted on the receiver.
 */
natsStatus
natsConnection_Publish(natsConnection *nc, const char *subj,
                       const void *data, int dataLen)
{
    natsStatus s;
    natsMsg    msg;

    natsMsg_init(&msg, subj, (const char*) data, dataLen);
    s = _publishMsg(nc, &msg, NULL);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Convenient function to publish a string. This call is equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsPublish(nc, subj, (const void*) myString, (int) strlen(myString));
 */
natsStatus
natsConnection_PublishString(natsConnection *nc, const char *subj,
                             const char *str)
{
    natsStatus s;
    natsMsg    msg;
    int        dataLen = 0;

    if (str != NULL)
        dataLen = (int) strlen(str);

    natsMsg_init(&msg, subj, str, dataLen);
    s = _publishMsg(nc, &msg, NULL);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Publishes the natsMsg structure, which includes the subject, an optional
 * reply and optional data.
 */
natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg)
{
    const char *reply = (msg != NULL ? msg->reply : NULL);
    natsStatus s;

    s = _publishMsg(nc, msg, reply);
    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Publishes the data argument to the given subject expecting a response on
 * the reply subject. Use natsConnection_Request() for automatically waiting for a
 * response inline.
 */
natsStatus
natsConnection_PublishRequest(natsConnection *nc, const char *subj,
                              const char *reply, const void *data, int dataLen)
{
    natsStatus s;
    natsMsg    msg;

    if ((reply == NULL) || (strlen(reply) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsMsg_init(&msg, subj, (const char*) data, dataLen);
    s = _publishMsg(nc, &msg, reply);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Convenient function to publish a request as a string. This call is
 * equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsPublishRequest(nc, subj, reply, (const void*) myString,
 *                    (int) strlen(myString));
 */
natsStatus
natsConnection_PublishRequestString(natsConnection *nc, const char *subj,
                                    const char *reply, const char *str)
{
    natsStatus s;
    natsMsg    msg;
    int        dataLen = 0;

    if ((reply == NULL) || (strlen(reply) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (str != NULL)
        dataLen = (int) strlen(str);

    natsMsg_init(&msg, subj, str, dataLen);
    s = _publishMsg(nc, &msg, reply);

    return NATS_UPDATE_ERR_STACK(s);
}

// Old way of sending a request...
static natsStatus
_oldRequestMsg(natsMsg **replyMsg, natsConnection *nc,
               natsMsg *requestMsg, int64_t timeout)
{
    natsStatus          s       = NATS_OK;
    natsSubscription    *sub    = NULL;
    char                inboxBuf[32 + NUID_BUFFER_LEN + 1];
    char                *inbox  = NULL;
    bool                freeIbx = false;

    s = natsConn_initInbox(nc, inboxBuf, sizeof(inboxBuf), &inbox, &freeIbx);
    if (s == NATS_OK)
        s = natsConn_subscribeSyncNoPool(&sub, nc, inbox);
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 1);
    if (s == NATS_OK)
        s = natsConn_publish(nc, requestMsg, (const char*) inbox, true);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(replyMsg, sub, timeout);

    if (freeIbx)
        NATS_FREE(inbox);
    natsSubscription_Destroy(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_respHandler(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    char        *rt   = NULL;
    const char  *subj = NULL;
    respInfo    *resp = NULL;
    bool        dmsg  = true;

    natsConn_Lock(nc);
    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);
        natsMsg_Destroy(msg);
        return;
    }
    subj = natsMsg_GetSubject(msg);
    // We look for the reply token by first checking that the message subject
    // prefix matches the subscription's subject (without the last '*').
    // It is possible that it does not due to subject rewrite (JetStream).
    if (((int) strlen(subj) > nc->reqIdOffset)
        && (memcmp((const void*) sub->subject, (const void*) subj, strlen(sub->subject) - 1) == 0))
    {
        rt = (char*) (natsMsg_GetSubject(msg) + nc->reqIdOffset);
        resp = (respInfo*) natsStrHash_Remove(nc->respMap, rt);
    }
    else if (natsStrHash_Count(nc->respMap) == 1)
    {
        // Only if the subject is completely different, we assume that it
        // could be the server that has rewritten the subject and so if there
        // is a single entry, use that.
        void *value = NULL;
        natsStrHash_RemoveSingle(nc->respMap, NULL, &value);
        resp = (respInfo*) value;
    }
    if (resp != NULL)
    {
        natsMutex_Lock(resp->mu);
        // Check for the race where the requestor has already timed-out.
        // If so, resp->removed will be true, in which case simply discard
        // the message.
        if (!resp->removed)
        {
            // Do not destroy the message since it is being used.
            dmsg = false;
            resp->msg = msg;
            resp->removed = true;
            natsCondition_Signal(resp->cond);
        }
        natsMutex_Unlock(resp->mu);
    }
    natsConn_Unlock(nc);

    if (dmsg)
        natsMsg_Destroy(msg);
}

/*
 * Sends a request and waits for the first reply, up to the provided timeout.
 * This is optimized for the case of multiple responses.
 */
natsStatus
natsConnection_RequestMsg(natsMsg **replyMsg, natsConnection *nc,
                          natsMsg *m, int64_t timeout)
{
    natsStatus          s           = NATS_OK;
    respInfo            *resp       = NULL;
    bool                needsRemoval= true;
    char                respInboxBuf[32 + NUID_BUFFER_LEN + NATS_MAX_REQ_ID_LEN + 1]; // <inbox prefix>.<nuid>.<reqId>
    char                *respInbox = respInboxBuf;

    if ((replyMsg == NULL) || (nc == NULL) || (m == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    *replyMsg = NULL;

    natsConn_Lock(nc);
    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }
    if (nc->opts->useOldRequestStyle)
    {
        natsConn_Unlock(nc);
        return _oldRequestMsg(replyMsg, nc, m, timeout);
    }

    // If the custom inbox prefix is more than the reserved 32 characters
    // in respInboxBuf, then we need to allocate...
    if (nc->inboxPfxLen > 32)
    {
        respInbox = NATS_MALLOC(nc->inboxPfxLen + NUID_BUFFER_LEN + NATS_MAX_REQ_ID_LEN + 1);
        if (respInbox == NULL)
        {
            natsConn_Unlock(nc);
            return nats_setDefaultError(NATS_NO_MEMORY);
        }
    }

    // Since we are going to release the lock and connection
    // may be closed while we wait for reply, we need to retain
    // the connection object.
    natsConn_retain(nc);

    // Setup only once (but could be more if natsConn_initResp() returns != OK)
    if (nc->respMux == NULL)
        s = natsConn_initResp(nc, _respHandler);
    if (s == NATS_OK)
        s = natsConn_addRespInfo(&resp, nc, respInbox, sizeof(respInbox));

    natsConn_Unlock(nc);

    if (s == NATS_OK)
    {
        s = natsConn_publish(nc, m, (const char*) respInbox, true);
        if (s == NATS_OK)
        {
            natsMutex_Lock(resp->mu);
            while ((s != NATS_TIMEOUT) && (resp->msg == NULL) && !resp->closed)
                s = natsCondition_TimedWait(resp->cond, resp->mu, timeout);

            // If we have a message, deliver it.
            if (resp->msg != NULL)
            {
                // In case of race where s != NATS_OK but we got the message,
                // we need to override status and set it to OK.
                s = NATS_OK;

                // For servers that support it, we may receive an empty message
                // with a 503 status header. If that is the case, return NULL
                // message and NATS_NO_RESPONDERS error.
                if (natsMsg_IsNoResponders(resp->msg))
                {
                    natsMsg_Destroy(resp->msg);
                    s = NATS_NO_RESPONDERS;
                }
                else
                    *replyMsg = resp->msg;
            }
            else
            {
                // Set the correct error status that we return to the user
                if (resp->closed)
                    s = resp->closedSts;
                else
                    s = NATS_TIMEOUT;
            }
            resp->msg = NULL;
            needsRemoval = !resp->removed;
            // Signal to _respHandler that we are no longer interested.
            resp->removed = true;
            natsMutex_Unlock(resp->mu);
        }
    }
    // Common to success or if we failed to create the sub, send the request...
    if (needsRemoval)
    {
        natsConn_Lock(nc);
        if (nc->respMap != NULL)
            natsStrHash_Remove(nc->respMap, respInbox+nc->reqIdOffset);
        natsConn_Unlock(nc);
    }
    natsConn_disposeRespInfo(nc, resp, true);

    natsConn_release(nc);

    if (respInbox != respInboxBuf)
        NATS_FREE(respInbox);

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Convenient function to send a request as a string. This call is
 * equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsConnection_Request(nc, subj, reply, (const void*) myString,
 *                        (int) strlen(myString));
 */
natsStatus
natsConnection_RequestString(natsMsg **replyMsg, natsConnection *nc,
                             const char *subj, const char *str,
                             int64_t timeout)
{
    natsStatus s;
    natsMsg    msg;

    natsMsg_init(&msg, subj, str, (str == NULL ? 0 : (int) strlen(str)));
    s = natsConnection_RequestMsg(replyMsg, nc, &msg, timeout);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout)
{
    natsStatus s;
    natsMsg    msg;

    natsMsg_init(&msg, subj, (const char*) data, dataLen);
    s = natsConnection_RequestMsg(replyMsg, nc, &msg, timeout);

    return NATS_UPDATE_ERR_STACK(s);
}
