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

#ifndef MEM_H_
#define MEM_H_

#include <stddef.h>

// GNU C Library version 2.25 or later.
#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#define HAVE_EXPLICIT_BZERO 1
#endif

// Newlib
#if defined(__NEWLIB__)
#define HAVE_EXPLICIT_BZERO 1
#endif

// FreeBSD version 11.0 or later.
#if defined(__FreeBSD__) && __FreeBSD_version >= 1100037
#define HAVE_EXPLICIT_BZERO 1
#endif

// OpenBSD version 5.5 or later.
#if defined(__OpenBSD__) && OpenBSD >= 201405
#define HAVE_EXPLICIT_BZERO 1
#endif

// NetBSD version 7.2 or later.
#if defined(__NetBSD__) && __NetBSD_Version__ >= 702000000
#define HAVE_EXPLICIT_MEMSET 1
#endif

typedef struct __natsBuf_s natsBuf;
typedef struct __natsGrowable_s natsGrowable;
typedef struct __natsLarge_s natsLarge;
typedef struct __natsReadBuffer_s natsReadBuffer;
typedef struct __natsReadChain_s natsReadChain;
typedef struct __natsSmall_s natsSmall;

#define nats_numPages(_memopts, _c) (((_c) + (_memopts)->heapPageSize - 1) / (_memopts)->heapPageSize)
#define nats_pageAlignedSize(_memopts, _c) (nats_numPages(_memopts, _c) * (_memopts)->heapPageSize)

#ifdef DEV_MODE_MEM_HEAP
#define HEAPTRACEf(fmt, ...) DEVTRACEx("HEAP", file, line, func, fmt, __VA_ARGS__)

#define nats_alloc(_c, _z) nats_log_alloc(nats_globalHeap(), _c, _z DEV_MODE_CTX)
#define nats_realloc(_p, _c) nats_log_realloc(nats_globalHeap(), _p, _c DEV_MODE_CTX)
#define nats_dupCString(_s) nats_log_dupCString(nats_globalHeap(), _s DEV_MODE_CTX)
#define nats_free(_p) nats_log_free(nats_globalHeap(), _p DEV_MODE_CTX)
#define nats_destroyHeap(_h) nats_log_destroy(nats_globalHeap() DEV_MODE_CTX)
#else // DEV_MODE_MEM_HEAP
#define HEAPTRACEf(fmt, ...)

#define nats_alloc(_c, _z) (nats_globalHeap())->alloc(nats_globalHeap(), _c, _z)
#define nats_realloc(_p, _c) (nats_globalHeap())->realloc(nats_globalHeap(), _p, _c)
#define nats_dupCString(_s) (nats_globalHeap())->dupCString(nats_globalHeap(), _s)
#define nats_free(_p) (nats_globalHeap())->free(nats_globalHeap(), _p)
#define nats_destroyHeap() (nats_globalHeap())->destroy(nats_globalHeap())
#endif // DEV_MODE_MEM_HEAP

static inline void *nats_log_alloc(natsHeap *h, size_t size, bool zero DEV_MODE_ARGS)
{
    void *mem = h->alloc(h, size, zero);
    if (mem != NULL)
        HEAPTRACEf("allocated bytes:%zu, ptr:%p", size, mem);
    return mem;
}

static inline void *nats_log_realloc(natsHeap *h, void *ptr, size_t size DEV_MODE_ARGS)
{
    void *mem = h->realloc(h, ptr, size);
    if (mem != NULL)
        HEAPTRACEf("reallocated bytes:%zu, from ptr:%p to ptr:%p", size, ptr, mem);
    return mem;
}

static inline void *nats_log_dupCString(natsHeap *h, const char *s DEV_MODE_ARGS)
{
    void *mem = h->strdup(h, s);
    if (mem != NULL)
        HEAPTRACEf("duplicated string:%s, bytes:%zu, ptr:%p", s, strlen(s)+1, mem);
    return mem;
}

static inline void nats_log_free(natsHeap *h, void *ptr DEV_MODE_ARGS)
{
    if (ptr != NULL)
    {
        HEAPTRACEf("freeing ptr:%p", ptr);
        h->free(h, ptr);
    }
}

static inline void nats_log_destroy(natsHeap *h DEV_MODE_ARGS)
{
    HEAPTRACEf("destroying heap:%p", (void*)h);
    h->destroy(h);
}

#include "mem_string.h"
#include "mem_pool.h"

#endif /* MEM_H_ */
