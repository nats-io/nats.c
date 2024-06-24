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
#include "msg.h"
#include "nuid.h"

static const char *digits = "0123456789";

#define GETBYTES_SIZE(len, b, i)            \
    {                                       \
        if ((len) > 0)                      \
        {                                   \
            int l;                          \
            for (l = (len); l > 0; l /= 10) \
            {                               \
                (i) -= 1;                   \
                (b)[(i)] = digits[l % 10];  \
            }                               \
        }                                   \
        else                                \
        {                                   \
            (i) -= 1;                       \
            (b)[(i)] = digits[0];           \
        }                                   \
    }

// This represents the maximum size of a byte array containing the
// string representation of a hdr/msg size. See GETBYTES_SIZE.
#define BYTES_SIZE_MAX (12)

static void _onMessagePublished(natsConnection *nc, uint8_t *buffer, void *closure)
{
    natsMessage *m = (natsMessage *)closure;
    if (m == NULL)
        return;

    if (m->donef != NULL)
        m->donef(nc, m, m->doneClosure);
    if (m->freef != NULL)
        m->freef(m->freeClosure);

    nats_releasePool(m->pool); // will free the message
}

static natsStatus
nats_asyncPublish(natsConnection *nc, natsMessage *msg, bool copyData)
{
    natsStatus s = NATS_OK;
    size_t totalLen = 0;
    int headerLen = 0;
    const natsString *proto = &nats_PUB;
    natsString headerLenStr = NATS_EMPTY_STR;
    natsString dataLenStr = NATS_EMPTY_STR;
    size_t headerLineLen = 0;
    char dlb[BYTES_SIZE_MAX];
    int dli = BYTES_SIZE_MAX;
    char hlb[BYTES_SIZE_MAX];
    int hli = BYTES_SIZE_MAX;
    natsBuf *scratch = NULL;
    natsString *liftedHeader = NULL;

    if ((nc == NULL) || (msg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);
    if (nats_IsStringEmpty(msg->subject) || !nats_isSubjectValid(msg->subject->data, msg->subject->len, false))
        return nats_setDefaultError(NATS_INVALID_SUBJECT);
    if (natsConn_isClosed(nc))
        return nats_setDefaultError(NATS_CONNECTION_CLOSED);
    if (natsConn_isDrainingPubs(nc))
        return nats_setDefaultError(NATS_DRAINING);

    // If there are explicit headers, we will use them.
    if ((msg->headers != NULL))
    {
        // If we have a hash of headers, we will use that.
        if (natsConn_initialConnectDone(nc) && !nc->info->headers)
            return nats_setDefaultError(NATS_NO_SERVER_SUPPORT);

        headerLen = natsMessageHeader_encodedLen(msg);
        if (headerLen > 0)
        {
            GETBYTES_SIZE(headerLen, hlb, hli)
            headerLenStr.data = (uint8_t *)(hlb + hli);
            headerLenStr.len = (BYTES_SIZE_MAX - hli);
            proto = &nats_HPUB;
        }
    }
    else if (msg->in != NULL)
    {
        // <>/<> FIXME lift headers from msg->in into liftedHeader
        if (natsConn_initialConnectDone(nc) && nc->info->headers)
            return nats_setDefaultError(NATS_NO_SERVER_SUPPORT);
    }

    // This will represent headers + data
    totalLen = headerLen + nats_StringLen(msg->out);

    if (natsConn_initialConnectDone(nc) && ((int64_t)totalLen > nc->info->maxPayload))
        return nats_setError(NATS_MAX_PAYLOAD, "Payload %d greater than maximum allowed: %zu", totalLen, nc->info->maxPayload);

    // <>/<> FIXME make sure we have enough space to queue for writes, against
    // the limits etc. Used to be for reconnection only, but really, on a slow
    // connection we should limit it also.

    GETBYTES_SIZE(totalLen, dlb, dli)
    dataLenStr.data = (uint8_t *)(dlb + dli);
    dataLenStr.len = (BYTES_SIZE_MAX - dli);

    // We include the NATS headers in the message header scratch.
    headerLineLen = proto->len + 1;
    headerLineLen += msg->subject->len + 1;
    if (!nats_IsStringEmpty(msg->reply))
        headerLineLen += msg->reply->len + 1;
    if (headerLen > 0)
        headerLineLen += headerLenStr.len + 1 + headerLen;
    headerLineLen += dataLenStr.len;
    headerLineLen += nats_CRLF.len;

    s = natsPool_getFixedBuf(&scratch, msg->pool, headerLineLen);
    IFOK(s, natsBuf_addString(scratch, proto));
    IFOK(s, natsBuf_addString(scratch, &nats_SPACE));
    IFOK(s, natsBuf_addString(scratch, msg->subject));
    IFOK(s, natsBuf_addString(scratch, &nats_SPACE));
    if (!nats_IsStringEmpty(msg->reply))
    {
        IFOK(s, natsBuf_addString(scratch, msg->reply));
        IFOK(s, natsBuf_addString(scratch, &nats_SPACE));
    }
    if (headerLen > 0)
    {
        IFOK(s, natsBuf_addString(scratch, &headerLenStr));
        IFOK(s, natsBuf_addString(scratch, &nats_SPACE));
    }
    IFOK(s, natsBuf_addString(scratch, &dataLenStr));
    IFOK(s, natsBuf_addString(scratch, &nats_CRLF));
    if (headerLen > 0)
    {
        if (liftedHeader == NULL)
        {
            IFOK(s, natsMessageHeader_encode(scratch, msg));
        }
        else
        {
            IFOK(s, natsBuf_addString(scratch, liftedHeader));
        }
    }

    IFOK(s, natsConn_asyncWrite(nc, natsBuf_string(scratch), NULL, NULL));

    if (!nats_IsStringEmpty(msg->out))
    {
        uint8_t *data = msg->out->data;
        if (copyData)
        {
            IFOK(s, CHECK_NO_MEMORY(data = nats_palloc(msg->pool, msg->out->len)));
            IFOK(s, ALWAYS_OK(memcpy(data, msg->out->data, msg->out->len)));
        }
        IFOK(s, natsConn_asyncWrite(nc, msg->out, NULL, NULL));
    }

    // Final write, add the callback
    IFOK(s, ALWAYS_OK(nats_RetainPool(msg->pool)));
    IFOK(s, natsConn_asyncWrite(nc, &nats_CRLF, _onMessagePublished, msg));

    if (STILL_OK(s))
    {
        nc->stats.outMsgs += 1;
        nc->stats.outBytes += totalLen;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus nats_AsyncPublish(natsConnection *nc, natsMessage *msg)
{
    return nats_asyncPublish(nc, msg, true);
}

natsStatus nats_AsyncPublishNoCopy(natsConnection *nc, natsMessage *msg)
{
    return nats_asyncPublish(nc, msg, false);
}
