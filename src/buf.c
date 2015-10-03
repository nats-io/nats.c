// Copyright 2015 Apcera Inc. All rights reserved.

#include <string.h>
#include <assert.h>

#include "mem.h"
#include "buf.h"

static natsStatus
_init(natsBuffer *newBuf, char *data, int len, int capacity)
{
    natsBuffer *buf = newBuf;

    memset(buf, 0, sizeof(natsBuffer));

    buf->doFree = false;

    if (data != NULL)
    {
        buf->data    = data;
        buf->ownData = false;
    }
    else
    {
        buf->data = (char*) NATS_MALLOC(capacity);
        if (buf->data == NULL)
            return NATS_NO_MEMORY;

        buf->ownData = true;
    }

    buf->pos      = buf->data + len;
    buf->len      = len;
    buf->capacity = capacity;

    return NATS_OK;
}

natsStatus
natsBuf_InitWithBackend(natsBuffer *newBuf, char *data, int len, int capacity)
{
    if (data == NULL)
        return NATS_INVALID_ARG;

    return _init(newBuf, data, len, capacity);
}

natsStatus
natsBuf_Init(natsBuffer *buf, int capacity)
{
    return _init(buf, NULL, 0, capacity);
}

static natsStatus
_newBuf(natsBuffer **newBuf, char *data, int len, int capacity)
{
    natsBuffer  *buf;

    buf = (natsBuffer*) NATS_MALLOC(sizeof(natsBuffer));
    if (buf == NULL)
        return NATS_NO_MEMORY;

    if (_init(buf, data, len, capacity) != NATS_OK)
    {
        NATS_FREE(buf);
        return NATS_NO_MEMORY;
    }

    buf->doFree = true;

    *newBuf = buf;

    return NATS_OK;
}

natsStatus
natsBuf_CreateWithBackend(natsBuffer **newBuf, char *data, int len, int capacity)
{
    if (data == NULL)
        return NATS_INVALID_ARG;

    return _newBuf(newBuf, data, len, capacity);
}

natsStatus
natsBuf_Create(natsBuffer **newBuf, int capacity)
{
    return _newBuf(newBuf, NULL, 0, capacity);
}

void
natsBuf_Reset(natsBuffer *buf)
{
    buf->len = 0;
    buf->pos = buf->data;
}

void
natsBuf_RewindTo(natsBuffer *buf, int newPosition)
{
    assert(newPosition < buf->capacity);

    buf->len = newPosition;
    buf->pos = buf->data + newPosition;
}


natsStatus
natsBuf_Expand(natsBuffer *buf, int newSize)
{
    int     offset   = (int) (buf->pos - buf->data);
    char    *newData = NULL;

    if (newSize <= buf->capacity)
        return NATS_INVALID_ARG;

    if (buf->ownData)
    {
        newData = NATS_REALLOC(buf->data, newSize);
        if (newData == NULL)
            return NATS_NO_MEMORY;
    }
    else
    {
        newData = NATS_MALLOC(newSize);
        if (newData == NULL)
            return NATS_NO_MEMORY;

        memcpy(newData, buf->data, buf->len);
        buf->ownData = true;
    }

    if (buf->data != newData)
    {
        buf->data = newData;
        buf->pos  = (char*) (buf->data + offset);
    }

    buf->capacity = newSize;

    return NATS_OK;
}

natsStatus
natsBuf_Append(natsBuffer *buf, const char* data, int dataLen)
{
    natsStatus  s = NATS_OK;
    int         n = buf->len + dataLen;

    if (n > buf->capacity)
        s = natsBuf_Expand(buf, (n + 1) * 2);

    if (s == NATS_OK)
    {
        memcpy(buf->pos, data, dataLen);
        buf->pos += dataLen;
        buf->len += dataLen;
    }

    return s;
}

natsStatus
natsBuf_AppendByte(natsBuffer *buf, char b)
{
    natsStatus s = NATS_OK;

    if (buf->len == buf->capacity)
        s = natsBuf_Expand(buf, buf->capacity * 1.5);

    if (s == NATS_OK)
    {
        *(buf->pos++) = b;
        buf->len++;
    }

    return s;
}

void
natsBuf_Destroy(natsBuffer *buf)
{
    if (buf == NULL)
        return;

    if (buf->ownData)
        NATS_FREE(buf->data);

    if (buf->doFree)
        NATS_FREE(buf);
    else
        memset(buf, 0, sizeof(natsBuffer));
}
