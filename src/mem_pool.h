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

#ifndef MEM_POOL_H_
#define MEM_POOL_H_

#include "opts.h"

//----------------------------------------------------------------------------
//  natsPool - memory pool.
//
//  - Uses a linked lists of natsSmall for small memory allocations. Each
//    heap-allocated chunk is 1-page NATS_DEFAULT_MEM_PAGE_SIZE) sized.
//  - Maximum small allocation size is NATS_DEFAULT_MEM_PAGE_SIZE -
//    sizeof(natsSmall).
//  - Uses a linked list of natsLarge for large HEAP memory allocations. The
//    list elements are allocated in the (small) pool.
//  - natsPool_Destroy() will free all memory allocated in the pool.
//
//  +->+---------------+ 0       +------------->+---------------+
//  |  | natsSmall     |         |              | natsSmall     |
//  |  |  - next       |---------+              |  - next       |
//  |  |---------------+                        |---------------|
//  |  | natsPool      |                        |               |
//  +--|  - small      |                        |               |
//     |               |                        | used          |
//     |  - large      |---                     | memory        |
//     |               |                        |               |
//     | (most recent) |                        |               |
//     |---------------|                        |---------------| len
//     | used          |                        | free          |
//     | memory        |                        | memory        |
//     |               |                        |               |
//     |               |                        |               |
//  +->+---------------|                        |               |
//  |  | natsLarge #1  |                        |               |
//  |  |  - prev       |                        |               |
//  |  |  - mem  ======|=> HEAP                 |               |
//  |  |---------------|                        |               |
//  |  | natsLarge #2  |                        |               |
//  +--|  - prev       |                        |               |
//     |  - mem  ======|=> HEAP                 |               |
//     |---------------|                        |               |
//     | more          |                        |               |
//     | used          |                        |               |
//     | memory        |                        |               |
//     | ...           |                        |               |
//     |---------------| len                    |               |
//     | free          |                        |               |
//     | memory        |                        |               |
//     |               |                        |               |
//     +---------------+ page size              +---------------+ page size

struct __natsSmall_s
{
    struct __natsSmall_s *next;
    size_t len;
};

struct __natsLarge_s
{
    struct __natsLarge_s *prev;
    uint8_t *data;
};

struct __natsReadBuffer_s
{
    natsString buf; // must be first, so natsReadBuf* is also a natsString*
    struct __natsReadBuffer_s *next;
    uint8_t *readFrom;
};

struct __natsBuf_s
{
    natsString buf; // must be first, so natsBuf* is also a natsString*

    size_t cap;
    natsPool *pool;
    natsSmall *small;
    natsLarge *large;
    bool isFixedSize;
};

// natsReadChain provides read buffer(s) to nats_ProcessReadEvent.
// While reading, we allocate or recycle the opPool every time a new operation
// is detected. The last read buffer from the previous operation may get
// recycled as the first read buffer of the new operation, including any
// leftover data in it.
struct __natsReadChain_s
{
    struct __natsReadBuffer_s *head;
    struct __natsReadBuffer_s *tail;
};

struct __natsPool_s
{
    int refs;
    natsMemOptions *opts;

    // small head is the first chunk allocated since that is where we attempt to
    // allocate first.
    natsSmall *small;

    // large head is the most recent large allocation, for simplicity.
    natsLarge *large;

    natsReadChain *readChain;

    const char *name; // stored as a pointer, not copied.
};

natsStatus nats_log_createPool(natsPool **newPool, natsMemOptions *opts, const char *name DEV_MODE_ARGS);

#ifdef DEV_MODE_MEM_POOL
#define POOLTRACEx(module, fmt, ...) DEVLOGx(DEV_MODE_TRACE, module, file, line, func, fmt, __VA_ARGS__)
#define POOLDEBUGf(fmt, ...) DEVDEBUGf("POOL", fmt, __VA_ARGS__)
#define POOLERRORf(fmt, ...) DEVERRORx("POOL", file, line, func, fmt, __VA_ARGS__)

#if defined(DEV_MODE) && !defined(MEM_POOL_C_)
// avoid using public functions internally, they don't pass the log context
#define nats_ReleasePool(_p) USE_nats_releasePool_INSTEAD
#define nats_CreatePool(_p, _h) USE_nats_createPool_INSTEAD
#define nats_Palloc(_p, _s) USE_natsPool_alloc_INSTEAD
#endif

#else // DEV_MODE_MEM_POOL

#define POOLTRACEx(module, fmt, ...)
#define POOLDEBUGf(fmt, ...)
#define POOLERRORf(fmt, ...)

#endif // DEV_MODE_MEM_POOL

#define nats_createPool(_p, _opts, _n) nats_log_createPool((_p), (_opts), (_n)DEV_MODE_CTX)
#define nats_releasePool(_p) nats_log_releasePool((_p)DEV_MODE_CTX)
#define nats_pstrdupnC(_p, _str, _len) nats_log_pdupnC((_p), (_str), (_len)DEV_MODE_CTX)
#define nats_pstrdupn(_p, _str, _len) nats_log_pdupn((_p), (_str), (_len)DEV_MODE_CTX)
#define nats_palloc(_p, _s) nats_log_palloc((_p), _s DEV_MODE_CTX)
#define nats_recyclePool(_pptr, _rbufptr) nats_log_recyclePool((_pptr), (_rbufptr)DEV_MODE_CTX)

#define nats_pstrdupC(_p, _str) nats_pstrdupnC((_p), (const uint8_t *)(_str), nats_strlen(_str) + 1)
#define nats_pstrdupS(_p, _str) (natsString_isEmpty(_str) ? NULL : nats_pstrdupn((_p), (_str)->data, (_str)->len))
#define nats_pstrdupU(_p, _s) (nats_pstrdupn((_p), (const uint8_t *)(_s), nats_strlen(_s)))

void *nats_log_palloc(natsPool *pool, size_t size DEV_MODE_ARGS);
void nats_log_releasePool(natsPool *pool DEV_MODE_ARGS);
natsStatus nats_log_recyclePool(natsPool **pool, natsReadBuffer **rbuf DEV_MODE_ARGS);

static inline char *nats_log_pdupnC(natsPool *pool, const uint8_t *data, size_t len DEV_MODE_ARGS)
{
    if ((data == NULL) || (len == 0))
        return NULL;
    char *dup = nats_log_palloc(pool, len DEV_MODE_PASSARGS);
    if (dup == NULL)
        return NULL;
    memcpy(dup, data, len);
    POOLTRACEx("!!!", "%s: allocated string '%s'", pool->name, natsString_debugPrintableN(data, len, 64));
    return dup;
}

static inline natsString *nats_log_pdupn(natsPool *pool, const uint8_t *data, size_t len DEV_MODE_ARGS)
{
    if ((data == NULL) || (len == 0))
        return NULL;
    natsString *dup = nats_log_palloc(pool, sizeof(natsString) DEV_MODE_PASSARGS);
    if (dup == NULL)
        return NULL;
    dup->data = nats_log_palloc(pool, len DEV_MODE_PASSARGS);
    if (dup->data == NULL)
        return NULL;
    memcpy(dup->data, data, len);
    dup->len = len;
    POOLTRACEx("", "%s: allocated string '%s'", pool->name, natsString_debugPrintableN(data, len, 64));
    return dup;
}

#define natsReadBuf_capacity() nats_memReadBufferSize
#define natsReadBuf_data(_rbuf) ((_rbuf)->buf.data)
#define natsReadBuf_len(_rbuf) ((_rbuf)->buf.len)
#define natsReadBuffer_available(_memopts, _rbuf) ((_memopts)->heapPageSize - (_rbuf)->buf.len)
#define natsReadBuffer_end(_rbuf) ((_rbuf)->buf.data + (_rbuf)->buf.len)
#define natsReadBuffer_unreadLen(_rbuf) (natsReadBuffer_end(_rbuf) - (_rbuf)->readFrom)

natsStatus natsPool_getReadBuffer(natsReadBuffer **rbuf, natsPool *pool);

#define natsBuf_available(b) ((b)->cap - (b)->buf.len)
#define natsBuf_capacity(b) ((b)->cap)
#define natsBuf_data(b) ((b)->buf.data)
#define natsBuf_len(b) ((b)->buf.len)
#define natsBuf_string(b) ((natsString *)(b))

natsStatus natsPool_getFixedBuf(natsBuf **newBuf, natsPool *pool, size_t cap);
natsStatus natsPool_getGrowableBuf(natsBuf **newBuf, natsPool *pool, size_t initialCap);

natsStatus natsBuf_Reset(natsBuf *buf);
natsStatus natsBuf_addBB(natsBuf *buf, const uint8_t *data, size_t len);
natsStatus natsBuf_addB(natsBuf *buf, uint8_t b);

// Does NOT add the terminating 0!
static inline natsStatus
natsBuf_addCString(natsBuf *buf, const char *str)
{
    if (nats_isCStringEmpty(str))
        return NATS_OK;
    return natsBuf_addBB(buf, (const uint8_t *)str, strlen(str)); // don't use nats_strlen, no need.
}

static inline natsStatus
natsBuf_addString(natsBuf *buf, const natsString *str)
{
    return natsBuf_addBB(buf, str->data, str->len);
}

#endif /* MEM_POOL_H_ */
