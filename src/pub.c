// Copyright 2015-2018 The NATS Authors
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

#define _publish(n, s, r, d, l) natsConn_publish((n), (s), (r), (d), (l), false)

// _publish is the internal function to publish messages to a nats server.
// Sends a protocol data message by queueing into the bufio writer
// and kicking the flusher thread. These writes should be protected.
natsStatus
natsConn_publish(natsConnection *nc, const char *subj,
         const char *reply, const void *data, int dataLen,
         bool directFlush)
{
    natsStatus  s = NATS_OK;
    int         msgHdSize = 0;
    char        b[12];
    int         bSize = sizeof(b);
    int         i = bSize;
    int         subjLen = 0;
    int         replyLen = 0;
    int         sizeSize = 0;

    if (nc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((subj == NULL)
        || ((subjLen = (int) strlen(subj)) == 0))
    {
        return nats_setDefaultError(NATS_INVALID_SUBJECT);
    }

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

    if (!nc->initc && ((int64_t) dataLen > nc->info.maxPayload))
    {
        natsConn_Unlock(nc);

        return nats_setError(NATS_MAX_PAYLOAD,
                             "Payload %d greater than maximum allowed: %" PRId64,
                             dataLen, nc->info.maxPayload);
    }

    // Check if we are reconnecting, and if so check if
    // we have exceeded our reconnect outbound buffer limits.
    if (natsConn_isReconnecting(nc))
    {
        // Flush to underlying buffer.
        natsConn_bufferFlush(nc);

        // Check if we are over
        if (natsBuf_Len(nc->pending) >= nc->opts->reconnectBufSize)
        {
            natsConn_Unlock(nc);
            return NATS_INSUFFICIENT_BUFFER;
        }
    }

    if (dataLen > 0)
    {
        int l;

        for (l = dataLen; l > 0; l /= 10)
        {
            i -= 1;
            b[i] = digits[l%10];
        }
    }
    else
    {
        i -= 1;
        b[i] = digits[0];
    }

    sizeSize = (bSize - i);

    msgHdSize = _PUB_P_LEN_
                + subjLen + 1
                + (replyLen > 0 ? replyLen + 1 : 0)
                + sizeSize + _CRLF_LEN_;

    natsBuf_RewindTo(nc->scratch, _PUB_P_LEN_);

    if (natsBuf_Capacity(nc->scratch) < msgHdSize)
    {
        // Although natsBuf_Append() would make sure that the buffer
        // grows, it is better to make sure that the buffer is big
        // enough for the pre-calculated size. We make it even a bit bigger.
        s = natsBuf_Expand(nc->scratch, (int) ((float)msgHdSize * 1.1));
    }

    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, subj, subjLen);
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, _SPC_, _SPC_LEN_);
    if ((s == NATS_OK) && (reply != NULL))
    {
        s = natsBuf_Append(nc->scratch, reply, replyLen);
        if (s == NATS_OK)
            s = natsBuf_Append(nc->scratch, _SPC_, _SPC_LEN_);
    }
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, (b+i), sizeSize);
    if (s == NATS_OK)
        s = natsBuf_Append(nc->scratch, _CRLF_, _CRLF_LEN_);

    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, natsBuf_Data(nc->scratch), msgHdSize);

    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, data, dataLen);

    if (s == NATS_OK)
        s = natsConn_bufferWrite(nc, _CRLF_, _CRLF_LEN_);

    if (s == NATS_OK)
    {
        if (directFlush)
            s = natsConn_bufferFlush(nc);
        else
            s = natsConn_flushOrKickFlusher(nc);
    }

    if (s == NATS_OK)
    {
        nc->stats.outMsgs  += 1;
        nc->stats.outBytes += dataLen;
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
    natsStatus s = _publish(nc, subj, NULL, data, dataLen);

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
    natsStatus s = _publish(nc, subj, NULL, (const void*) str,
                            (str != NULL ? (int) strlen(str) : 0));

    return NATS_UPDATE_ERR_STACK(s);
}

/*
 * Publishes the natsMsg structure, which includes the subject, an optional
 * reply and optional data.
 */
natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg)
{
    natsStatus s = _publish(nc, msg->subject, msg->reply,
                            msg->data, msg->dataLen);

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

    if ((reply == NULL) || (strlen(reply) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _publish(nc, subj, reply, data, dataLen);

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

    if ((reply == NULL) || (strlen(reply) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _publish(nc, subj, reply, (const void*) str, (int) strlen(str));

    return NATS_UPDATE_ERR_STACK(s);
}

// Old way of sending a request...
static natsStatus
_oldRequest(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                    const void *data, int dataLen, int64_t timeout)
{
    natsStatus          s       = NATS_OK;
    natsSubscription    *sub    = NULL;
    char                inbox[NATS_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1];

    s = natsInbox_init(inbox, sizeof(inbox));
    if (s == NATS_OK)
        s = natsConn_subscribe(&sub, nc, inbox, NULL, 0, NULL, NULL);
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 1);
    if (s == NATS_OK)
        s = natsConn_publish(nc, subj, inbox, data, dataLen, true);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(replyMsg, sub, timeout);

    natsSubscription_Destroy(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_respHandler(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    char     *rt   = (char *) (natsMsg_GetSubject(msg) + NATS_REQ_ID_OFFSET);
    respInfo *resp = NULL;

    if (rt == NULL)
        return;

    natsConn_Lock(nc);
    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);
        return;
    }
    resp = (respInfo*) natsStrHash_Remove(nc->respMap, rt);
    if (resp != NULL)
    {
        natsMutex_Lock(resp->mu);
        resp->msg = msg;
        resp->removed = true;
        natsCondition_Signal(resp->cond);
        natsMutex_Unlock(resp->mu);
    }
    natsConn_Unlock(nc);
}

/*
 * Sends a request and waits for the first reply, up to the provided timeout.
 * This is optimized for the case of multiple responses.
 */
natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout)
{
    natsStatus          s           = NATS_OK;
    natsSubscription    *sub        = NULL;
    respInfo            *resp       = NULL;
    bool                createSub   = false;
    bool                needsRemoval= true;
    bool                waitForSub  = false;
    char                ginbox[NATS_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1 + 1 + 1]; // _INBOX.<nuid>.*
    char                respInbox[NATS_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1 + NATS_MAX_REQ_ID_LEN + 1]; // _INBOX.<nuid>.<reqId>

    if ((replyMsg == NULL) || (nc == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsConn_Lock(nc);
    if (natsConn_isClosed(nc))
    {
        natsConn_Unlock(nc);
        return NATS_CONNECTION_CLOSED;
    }
    if (nc->opts->useOldRequestStyle)
    {
        natsConn_Unlock(nc);
        return _oldRequest(replyMsg, nc, subj, data, dataLen, timeout);
    }

    // Since we are going to release the lock and connection
    // may be closed while we wait for reply, we need to retain
    // the connection object.
    natsConn_retain(nc);

    // Setup only once
    if (nc->respReady == NULL)
    {
        s = natsConn_initResp(nc, ginbox, sizeof(ginbox));
        createSub = (s == NATS_OK);
    }
    if (s == NATS_OK)
        s = natsConn_addRespInfo(&resp, nc, respInbox, sizeof(respInbox));

    // If multiple requests are performed in parallel, only
    // one will create the wildcard subscriptions, but the
    // others need to wait for the subscription to be setup
    // before publishing the message.
    if (s == NATS_OK)
        waitForSub = (nc->respMux == NULL);

    natsConn_Unlock(nc);

    if ((s == NATS_OK) && createSub)
        s = natsConn_createRespMux(nc, ginbox, _respHandler);
    else if ((s == NATS_OK) && waitForSub)
        s = natsConn_waitForRespMux(nc);

    if (s == NATS_OK)
    {
        s = natsConn_publish(nc, subj, respInbox, data, dataLen, true);
        if (s == NATS_OK)
        {
            natsMutex_Lock(resp->mu);
            while ((s != NATS_TIMEOUT) && (resp->msg == NULL) && !resp->closed)
                s = natsCondition_TimedWait(resp->cond, resp->mu, timeout);

            // If we have a message, deliver it.
            if (resp->msg != NULL)
            {
                *replyMsg = resp->msg;
                s = NATS_OK;
            }
            else
            {
                // Set the correct error status that we return to the user
                if (resp->closed)
                    s = NATS_CONNECTION_CLOSED;
                else
                    s = NATS_TIMEOUT;
            }
            resp->msg = NULL;
            needsRemoval = !resp->removed;
            natsMutex_Unlock(resp->mu);
        }
    }
    // Common to success or if we failed to create the sub, send the request...
    if (needsRemoval)
    {
        natsConn_Lock(nc);
        if (nc->respMap != NULL)
            natsStrHash_Remove(nc->respMap, respInbox+NATS_REQ_ID_OFFSET);
        natsConn_Unlock(nc);
    }
    natsConn_disposeRespInfo(nc, resp, true);

    natsConn_release(nc);

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

    s = natsConnection_Request(replyMsg, nc, subj, (const void*) str,
                               (str == NULL ? 0 : (int) strlen(str)),
                               timeout);

    return NATS_UPDATE_ERR_STACK(s);
}
