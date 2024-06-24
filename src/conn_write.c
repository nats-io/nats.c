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

#include "conn.h"
#include "opts.h"
#include "servers.h"
#include "json.h"

natsStatus natsConn_asyncWrite(natsConnection *nc, const natsString *buf,
                               natsOnWrittenF donef, void *doneUserData)
{
    natsStatus s = NATS_OK;

    if (nc->sockCtx.fd == NATS_SOCK_INVALID)
        return NATS_CONNECTION_CLOSED;

    s = natsWriteChain_add(&(nc->writeChain), buf, donef, doneUserData);

    // The ev write method will schedule this event if not already active.
    IFOK(s, nc->ev.write(nc->evState, NATS_EV_ADD));

    return NATS_UPDATE_ERR_STACK(s);
}

void nats_ProcessWriteEvent(natsConnection *nc)
{
    natsStatus s = NATS_OK;

    if (nc->sockCtx.fd == NATS_SOCK_INVALID)
        return;

    while (natsWriteChain_len(&nc->writeChain) > 0)
    {
        natsWriteBuffer *wbuf = natsWriteChain_get(&nc->writeChain);
        natsString written = {
            .data = wbuf->buf.data + wbuf->written,
            .len = wbuf->buf.len - wbuf->written
        };

        s = natsSock_Write(&(nc->sockCtx), &written, &written.len);
        if (s != NATS_OK)
        {
            natsConn_processOpError(nc, s);
            return;
        }
        CONNTRACE_out(&written);

        wbuf->written += written.len;
        if (wbuf->written >= wbuf->buf.len)
        {
            natsWriteChain_done(nc, &nc->writeChain);
        }
    }
}

static natsStatus _grow(natsWriteQueue *w, size_t cap)
{
    if (natsWriteChain_cap(w) >= cap)
        return NATS_OK;
    if (natsWriteChain_cap(w) == w->opts->writeQueueMaxBuffers)
        return nats_setError(
            NATS_INSUFFICIENT_BUFFER,
            "write chain has already reached the maximum capacity: %zu", w->opts->writeQueueMaxBuffers);

    size_t allocSize = nats_pageAlignedSize(w->opts, cap * sizeof(natsWriteBuffer));
    cap = allocSize / sizeof(natsWriteBuffer);
    if (cap > w->opts->writeQueueMaxBuffers)
        cap = w->opts->writeQueueMaxBuffers;

    natsHeap *heap = nats_globalHeap();
    natsWriteBuffer *newChain = heap->realloc(heap, w->chain, allocSize);
    if (newChain == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (natsWriteChain_isWrapped(w))
    {
        size_t toCopy = natsWriteChain_endPos(w) * sizeof(natsWriteBuffer);
        if (toCopy > 0)
        {
            memcpy(
                newChain + natsWriteChain_cap(w),                    // The end of the old buffer
                newChain,                                            // from the beginning
                natsWriteChain_endPos(w) * sizeof(natsWriteBuffer)); // to the end of the wrapped around part.
        }
    }

    size_t len = natsWriteChain_len(w);
    w->start = natsWriteChain_startPos(w); // use the old capacity to mod the start position.
    w->end = w->start + len;
    w->capacity = cap;
    w->chain = newChain;
    return NATS_OK;
}

natsStatus natsWriteChain_init(natsWriteQueue *queueAllocaledByCaller, natsMemOptions *opts)
{
    natsWriteQueue *w = queueAllocaledByCaller;
    if (w == NULL)
        return NATS_UPDATE_ERR_STACK(NATS_INVALID_ARG);

    memset(w, 0, sizeof(natsWriteQueue));
    w->start = 0;
    w->end = 0;
    w->opts = opts;

    return _grow(w, opts->writeQueueBuffers);
}

// TODO <>/<> donef should process errors????
natsStatus natsWriteChain_add(natsWriteQueue *w, const natsString *buffer,
                              natsOnWrittenF donef, void *doneUserData)
{
    // if we are full, attempt to grow the buffers queue.
    if (natsWriteChain_isFull(w))
    {
        natsStatus s = _grow(w, natsWriteChain_cap(w) * 2);
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    size_t pos = natsWriteChain_endPos(w);
    w->chain[pos].buf.data = buffer->data;
    w->chain[pos].buf.len = buffer->len;
    w->chain[pos].written = 0;
    w->chain[pos].done = donef;
    w->chain[pos].userData = doneUserData;
    w->end++;
    return NATS_OK;
}

natsWriteBuffer *natsWriteChain_get(natsWriteQueue *w)
{
    if (natsWriteChain_isEmpty(w))
        return NULL;
    return &w->chain[natsWriteChain_startPos(w)];
}

natsStatus natsWriteChain_done(natsConnection *nc, natsWriteQueue *w)
{
    natsWriteBuffer *wbuf = natsWriteChain_get(w);

    if (wbuf == NULL)
        return nats_setError(NATS_ERR, "%s", "no current write buffer");

    if (wbuf->done != NULL)
    {
        wbuf->done(nc, wbuf->buf.data, wbuf->userData);
    }

    w->start++;
    return NATS_OK;
}

static natsStatus
_connectProto(natsString **proto, natsConnection *nc)
{
    natsOptions *opts = nc->opts;
    const char *token = NULL;
    const char *user = NULL;
    const char *pwd = NULL;

    // Check if NoEcho is set and we have a server that supports it.
    if (opts->proto.noEcho && (nc->info->proto < 1))
        return NATS_NO_SERVER_SUPPORT;

    if (nc->cur->url->username != NULL)
        user = nc->cur->url->username;
    if (nc->cur->url->password != NULL)
        pwd = nc->cur->url->password;
    if ((user != NULL) && (pwd == NULL))
    {
        token = user;
        user = NULL;
    }
    if ((user == NULL) && (token == NULL))
    {
        // Take from options (possibly all NULL)
        user = opts->user;
        pwd = opts->password;
        token = opts->token;

        // Options take precedence for an implicit URL. If above is still
        // empty, we will check if we have saved a user from an explicit
        // URL in the server pool.
        if (nats_isCStringEmpty(user) && nats_isCStringEmpty(token) && (nc->servers->user != NULL))
        {
            user = nc->servers->user;
            pwd = nc->servers->pwd;
            // Again, if there is no password, assume username is token.
            if (pwd == NULL)
            {
                token = user;
                user = NULL;
            }
        }
    }

    return nats_marshalConnect(proto, nc, user, pwd, token,
                               nc->opts->name, nc->info->headers,
                               (nc->info->headers && !nc->opts->proto.disableNoResponders));
}

natsStatus
natsConn_sendPing(natsConnection *nc)
{
    natsStatus s = natsConn_asyncWrite(nc, &nats_PING_CRLF, NULL, NULL);
    if (STILL_OK(s))
        nc->pingsOut++;
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsConn_sendConnect(natsConnection *nc)
{
    natsStatus s = NATS_OK;
    natsString *proto;

    // Allocates the memory in the connect pool.
    s = _connectProto(&proto, nc);
    IFOK(s, natsConn_asyncWrite(nc, proto, NULL, NULL));
    return NATS_UPDATE_ERR_STACK(s);
}

