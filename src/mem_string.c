// Copyright 2024 The NATS Authors
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

#include <ctype.h>

void nats_strlow(uint8_t *dst, uint8_t *src, size_t n)
{
    while (n)
    {
        *dst = nats_toLower(*src);
        dst++;
        src++;
        n--;
    }
}

size_t
nats_strnlen(uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {

        if (p[i] == '\0')
        {
            return i;
        }
    }

    return n;
}

uint8_t *
nats_cpystrn(uint8_t *dst, uint8_t *src, size_t n)
{
    if (n == 0)
    {
        return dst;
    }

    while (--n)
    {
        *dst = *src;

        if (*dst == '\0')
        {
            return dst;
        }

        dst++;
        src++;
    }

    *dst = '\0';

    return dst;
}

#ifdef DEV_MODE

static char _printbuf[128];

const char *_debugPrintable(const uint8_t *data, size_t len, char *out, size_t outCap, size_t limit)
{
    if (data == NULL)
        return "<null>";
    if (outCap < 5)
        return "<n/a>";
    if (limit == 0)
        limit = len;
    if (len > limit)
        len = limit;
    const uint8_t *end = data + len;

    const size_t maxbuf = outCap - 1;
    size_t i = 0;
    for (const uint8_t *p = data; (p < end) && (i < maxbuf); p++)
    {
        if (isprint(*p))
        {
            out[i++] = *p;
        }
        else if (*p == '\n')
        {
            out[i++] = '\\';
            out[i++] = 'n';
        }
        else if (*p == '\r')
        {
            out[i++] = '\\';
            out[i++] = 'r';
        }
        else
        {
            out[i++] = '?';
        }

        if (i >= maxbuf - 3)
        {
            out[i++] = '.';
            out[i++] = '.';
            out[i++] = '.';
        }
    }
    out[i] = '\0';
    return out;
}

static size_t _debugPrintableLen(natsString *buf)
{
    if (buf == NULL)
    {
        return 6;
    }
    uint8_t *end = buf->data + buf->len;

    size_t l = 0;
    for (uint8_t *p = buf->data; p < end; p++, l++)
    {
        if ((*p == '\n') || (*p == '\r'))
            l++;
    }
    return l + 1;
}

const char *natsString_debugPrintable(natsString *buf, size_t limit)
{
    if (buf == NULL)
        return "<null>";
    return _debugPrintable(buf->data, buf->len, _printbuf, sizeof(_printbuf), limit);
}

const char *natsString_debugPrintableN(const uint8_t *data, size_t len, size_t limit)
{
    return _debugPrintable(data, len, _printbuf, sizeof(_printbuf), limit);
}

const char *natsString_debugPrintableC(const char *buf, size_t limit)
{
    if (buf == NULL)
        return "<null>";
    return natsString_debugPrintableN((const uint8_t *)buf, strlen(buf), limit);
}

// const char *natsPool_debugPrintable(natsString *buf, natsPool *pool, size_t limit)
// {
//     if (buf == NULL)
//         return "<null>";
//     size_t cap = _debugPrintableLen(buf);
//     char *s = natsPool_alloc(pool, cap);
//     return (s != NULL) ? nats_debugPrintableN(buf->data, buf->len, s, cap, limit) : "n/a - out of memory";
// }

#endif // DEV_MODE
