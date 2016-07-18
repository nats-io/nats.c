// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <string.h>

#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "nuid.h"

static const char *digits = "0123456789";

#define _publish(n, s, r, d, l) _publishEx((n), (s), (r), (d), (l), false)

// _publish is the internal function to publish messages to a nats server.
// Sends a protocol data message by queueing into the bufio writer
// and kicking the flusher thread. These writes should be protected.
static natsStatus
_publishEx(natsConnection *nc, const char *subj,
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

    // Pro-actively reject dataLen over the threshold set by server.
    if ((int64_t) dataLen > nc->info.maxPayload)
    {
        natsConn_Unlock(nc);

        return nats_setError(NATS_MAX_PAYLOAD,
                             "Payload %d greater than maximum allowed: %" PRId64,
                             dataLen, nc->info.maxPayload);

    }

    if ((s == NATS_OK) && natsConn_isClosed(nc))
    {
        s = nats_setDefaultError(NATS_CONNECTION_CLOSED);
    }

    // Check if we are reconnecting, and if so check if
    // we have exceeded our reconnect outbound buffer limits.
    if ((s == NATS_OK) && natsConn_isReconnecting(nc))
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

    if (s == NATS_OK)
    {
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
            natsConn_kickFlusher(nc);
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

/*
 * Creates an inbox and performs a natsPublishRequest() call with the reply
 * set to that inbox. Returns the first reply received.
 * This is optimized for the case of multiple responses.
 */
natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout)
{
    natsStatus          s       = NATS_OK;
    natsSubscription    *sub    = NULL;
    char                inbox[NATS_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1];

    if (replyMsg == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsInbox_init(inbox, sizeof(inbox));
    if (s == NATS_OK)
        s = natsConn_subscribe(&sub, nc, inbox, NULL, NULL, NULL);
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 1);
    if (s == NATS_OK)
        s = _publishEx(nc, subj, inbox, data, dataLen, true);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(replyMsg, sub, timeout);

    natsSubscription_Destroy(sub);

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
