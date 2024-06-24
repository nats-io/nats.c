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

#define MEM_POOL_C_

#include "natsp.h"

static inline size_t _smallMax(natsPool *pool) { return pool->opts->heapPageSize - sizeof(natsSmall); }
static inline size_t _smallCap(natsPool *pool, natsSmall *small) { return pool->opts->heapPageSize - small->len; }

static inline void *_smallGrab(natsSmall *small, size_t size)
{
    void *mem = (uint8_t *)small + small->len;
    small->len += size;
    return mem;
}

static inline natsPool *
_initializePool(void *mempage, natsMemOptions *opts, const char *name)
{
    natsSmall *small = mempage;
    _smallGrab(small, sizeof(natsSmall)); // mark itself allocated
    natsPool *pool = _smallGrab(small, sizeof(natsPool));
    pool->small = small;
    pool->opts = opts;
    pool->refs = 1;
    pool->name = nats_pstrdupC(pool, name);
    return pool;
}

static uint64_t _userPoolC = 0;

natsStatus
nats_CreatePool(natsPool **newPool, natsOptions *opts)
{
    char name[64];
    // Public API, so we need to check that we're initialized.
    natsStatus s = nats_open();
    if (s != NATS_OK)
        return s;

    natsMemOptions *memOpts = &nats_defaultMemOptions;
    if (opts != NULL)
        memOpts = &opts->mem;

    snprintf(name, sizeof(name), "user-pool-%" PRIu64, ++_userPoolC);
    return nats_log_createPool(newPool, memOpts, name DEV_MODE_CTX);
}

static void _free(natsPool *pool DEV_MODE_ARGS)
{
    if (pool == NULL)
        return;

    for (natsLarge *l = pool->large; l != NULL; l = l->prev)
    {
        POOLTRACEx("-l  ", "%s: freeing large, ptr:%p", pool->name, (void *)l->data);
        nats_free(l->data);
    }

    natsSmall *next = NULL;
    int i = 0;
#ifdef DEV_MODE_MEM_POOL
    void *mem = pool->small;
    char namebuf[128];
    strncpy(namebuf, pool->name, sizeof(namebuf));
#endif

    for (natsSmall *small = pool->small; small != NULL; small = next, i++)
    {
        next = small->next;
        if (i != 0)
            POOLTRACEx("-s- ", "%s#%d: freeing small, ptr:%p", namebuf, i, (void *)small);
        nats_free(small);
    }

    // The pool itself is allocated in the first natsSmall, so no need to free
    // it.
    POOLTRACEx("POOL", "%s: destroyed pool, heap: %p", namebuf, (void *)(mem));
}

void nats_log_releasePool(natsPool *pool DEV_MODE_ARGS)
{
    if (pool == NULL)
        return;
    pool->refs--;
    if (pool->refs == 0)
        _free(pool DEV_MODE_PASSARGS);
}

void nats_ReleasePool(natsPool *pool)
{
    nats_log_releasePool(pool DEV_MODE_CTX);
}

void nats_RetainPool(natsPool *pool)
{
    pool->refs++;
}

void *_allocSmall(natsSmall **newOrFound, natsPool *pool, size_t size DEV_MODE_ARGS)
{
    natsSmall *last = pool->small;
    natsSmall *small = pool->small;
    int i = 0;
    void *mem = NULL;

    for (small = pool->small; small != NULL; small = small->next, i++)
    {
        if (size > _smallCap(pool, small))
        {
            last = small;
            continue;
        }

        mem = _smallGrab(small, size);

        if (newOrFound != NULL)
            *newOrFound = small;
        // POOLTRACEx("+s",
        //            "%s#%d: allocated in small: bytes:%zu, remaining:%zu, ptr:%p", pool->name, i, size, _smallCap(pool, small), mem);
        return mem;
    }

    small = nats_alloc(pool->opts->heapPageSize, NATS_MEM_ZERO_OUT); // greater than sizeof(natsSmall)
    if (small == NULL)
    {
        POOLERRORf("%s: failed to allocate a new small page: %zu bytes", pool->name, pool->opts->heapPageSize);
        return NULL;
    }
    _smallGrab(small, sizeof(natsSmall)); // mark itself allocated
    mem = _smallGrab(small, size);

    // Link it to the end of the chain.
    last->next = small;

    if (newOrFound != NULL)
        *newOrFound = small;
    POOLTRACEx("+s+", "%s#%d: (new) allocated in small bytes:%zu, remaining:%zu, ptr:%p", pool->name, i, size, _smallCap(pool, small), mem);
    return mem;
}

static void *
_allocLarge(natsPool *pool, size_t size, natsLarge **newLarge DEV_MODE_ARGS)
{
    natsLarge *large = NULL;
    if (newLarge != NULL)
        *newLarge = NULL;

    large = nats_palloc(pool, sizeof(natsLarge));
    if (large == NULL)
        return NULL;

    large->data = nats_alloc(pool->opts->heapPageSize, NATS_MEM_ZERO_OUT);
    if (large->data == NULL)
    {
        POOLERRORf("%s: failed to alloc large", pool->name);
        return NULL;
    }
    memset(large->data, 0, size);
    large->prev = pool->large;
    pool->large = large;

    if (newLarge != NULL)
        *newLarge = large;

    POOLTRACEx("+l", "%s: allocated %zu, ptr:%p ", pool->name, size, (void *)large->data);
    return large->data;
}

void *natsPool_nolog_alloc(natsPool *pool, size_t size)
{
    if (size > _smallMax(pool))
        return _allocLarge(pool, size, NULL DEV_MODE_CTX);
    else
        return _allocSmall(NULL, pool, size DEV_MODE_CTX);
}

void *natsPool_Alloc(natsPool *pool, size_t size)
{
    return natsPool_nolog_alloc(pool, size);
}

natsStatus natsPool_getReadBuffer(natsReadBuffer **out, natsPool *pool)
{
    if (pool->readChain == NULL)
    {
        pool->readChain = nats_palloc(pool, sizeof(natsReadChain));
        if (pool->readChain == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
    }
    natsReadChain *chain = pool->readChain;

    // If we have a current chain and it has enough space, return it.
    if ((chain->tail != NULL) && natsReadBuffer_available(pool->opts, chain->tail) >= pool->opts->readBufferMin)
    {
        *out = chain->tail;
        return NATS_OK;
    }

    // Don't have a chain or the current one is full, allocate a new one.
    natsReadBuffer *rbuf = nats_palloc(pool, sizeof(natsReadBuffer));
    if (rbuf == NULL)
        return NATS_NO_MEMORY;

    rbuf->buf.data = nats_alloc(pool->opts->readBufferSize, NATS_MEM_LEAVE_UNINITIALIZED);
    if (rbuf->buf.data == NULL)
        return NATS_NO_MEMORY;
    POOLDEBUGf("%s: allocated read buffer %zu bytes, heap: %p)", pool->name, pool->opts->readBufferSize, (void *)rbuf->buf.data);
    rbuf->readFrom = rbuf->buf.data;
    rbuf->buf.len = 0;

    if (chain->tail == NULL)
    {
        // First buffer
        chain->head = rbuf;
        chain->tail = rbuf;
    }
    else
    {
        // Link it to the end of the chain.
        chain->tail->next = rbuf;
        chain->tail = rbuf;
    }

    *out = rbuf;
    return NATS_OK;
}

static void natsPool_log_recycleReadChain(natsReadBuffer *clone, natsPool *pool DEV_MODE_ARGS)
{
    natsReadChain *rc = pool->readChain;
    natsReadBuffer *rbuf;

    // Free the buffers, except the last one.
    for (rbuf = rc->head; (rbuf != NULL); rbuf = rbuf->next)
    {
        if (rbuf == rc->tail)
        {
            POOLTRACEx("recycle", "%s: NOT freeing last read buffer, heap: %p)", pool->name, (void *)rbuf->buf.data);
            break;
        }
        POOLTRACEx("-r", "%s: freeing read buffer, heap: %p", pool->name, (void *)rbuf->buf.data);
        nats_free(rbuf->buf.data);
    }

    memset(clone, 0, sizeof(natsReadBuffer));
    if (rbuf == NULL)
        return;

    *clone = *rbuf;
    clone->next = NULL;
    if (natsReadBuffer_unreadLen(clone) == 0)
    {
        // No need to zero out the memory, just reset the pointers.
        clone->readFrom = clone->buf.data;
        clone->buf.len = 0;
    }
    return;
}

natsStatus nats_log_recyclePool(natsPool **poolPtr, natsReadBuffer **outRbuf DEV_MODE_ARGS)
{
    char namebuf[64] = "";
    natsReadBuffer lastToKeep;
    natsReadBuffer *rbuf = NULL;
    natsPool *pool;

    if (poolPtr == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);
    pool = *poolPtr;
    *poolPtr = NULL; // in case we fail

    if (pool == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);
    if (pool->refs > 1)
    {
        POOLDEBUGf("%s: refs:%d, NOT recyclable, release", pool->name, pool->refs);
        nats_releasePool(pool);
        goto CREATE_NEW_POOL;
    }

    // need to keep a copy of the name since we will zero out the previous memory.
    if (pool->name != NULL)
        strncpy(namebuf, pool->name, sizeof(namebuf));

    // Recycle the chain, keep a copy of the last ReadBuffer so we can append it
    // to the recycled pool's read chain.
    natsPool_log_recycleReadChain(&lastToKeep, pool DEV_MODE_CTX);

    // Free all Large's.
    for (natsLarge *l = pool->large; l != NULL; l = l->prev)
    {
        POOLTRACEx("-l  ", "%s: freeing large, ptr:%p", namebuf, (void *)l->data);
        nats_free(l->data);
    }
    pool->large = NULL;

    // Free all Smalls, except the very first two, they'll be re-used. 2 because
    // parseOp will likely want a little memory and a natsBuf that will take a
    // Small to itself. pool->small can never be NULL.
    natsSmall *s = pool->small;
    natsSmall *next = NULL;
    for (int i = 0; s != NULL; s = next, i++)
    {
        if (i < 2)
        {
            next = s->next;
        }
        else
        {
            POOLTRACEx("-s", "%s: freeing small #%d, ptr: %p", namebuf, i, (void *)s);
            next = s->next;
            nats_free(s);
        }
    }

    // Re-initialize the (up to) 2 preserved smalls.
    next = NULL; // redunant, but makes it clear.
    if (pool->small != NULL)
    {
        void *mem = pool->small;
        natsMemOptions *opts = pool->opts;
        next = pool->small->next;
        memset(mem, 0, opts->heapPageSize);
        pool = _initializePool(mem, opts, namebuf);
        pool->small->next = next;
        POOLDEBUGf("%s: recycled small #0, remaining:%zu, heap: %p", namebuf, _smallCap(pool, pool->small), (void *)pool->small);
    }
    if (next != NULL)
    {
        memset(next, 0, pool->opts->heapPageSize);
        next->len = sizeof(natsSmall);
        POOLDEBUGf("%s: recycled small #1, remaining:%zu, heap: %p", namebuf, _smallCap(pool, next), (void *)next);
    }

    // If there was allocated memory in the last ReadBuffer, use it. If it holds
    // unread bytes, use it as is, otherwise reset to an empty buffer.
    if (lastToKeep.buf.data != NULL)
    {
        POOLTRACEx("    ", "%s: keeping the last read buffer, heap: %p)", pool->name, (void *)lastToKeep.buf.data);
        pool->readChain = nats_palloc(pool, sizeof(natsReadChain));
        if (pool->readChain != NULL)
            rbuf = nats_palloc(pool, sizeof(natsReadBuffer));
        if (pool->readChain == NULL || rbuf == NULL)
            return NATS_NO_MEMORY; // the caller will release the pool.

        *rbuf = lastToKeep;
        pool->readChain->head = rbuf;
        pool->readChain->tail = rbuf;
    }

    POOLDEBUGf("%s: recycled, heap: %p", namebuf, (void *)(pool->small));
    *poolPtr = pool;
    return NATS_OK;

CREATE_NEW_POOL:
    if (pool != NULL)
        nats_releasePool(pool);
    if (outRbuf != NULL)
        *outRbuf = NULL;
    return nats_createPool(poolPtr, pool->opts, pool->name);
}

static inline size_t _newLargeBufSize(natsPool *pool, size_t current, size_t required)
{
    size_t newCap = (2 * current < required) ? required : 2 * current;
    newCap = nats_pageAlignedSize(pool->opts, newCap);
    return newCap;
}

static natsStatus
_expandBuf(natsBuf *buf, size_t capacity)
{
    uint8_t *data = NULL;
    size_t prevCap = buf->cap;
    natsSmall *prevSmall = buf->small;
    bool copy = true;
    size_t newCap = 0;
    natsPool *pool = buf->pool;

    if ((capacity < buf->buf.len) || (pool == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);
    if (capacity >= 0x7FFFFFFF)
        return nats_setDefaultError(NATS_NO_MEMORY);
    if (capacity <= buf->cap)
        return NATS_OK;

    // Only resize a fixed buffer once.
    if ((buf->isFixedSize) && (buf->cap != 0))
        return nats_setDefaultError(NATS_INSUFFICIENT_BUFFER);

    if (capacity <= buf->cap)
        return NATS_OK;

    // If the buffer was already allocated in a "large" chunk, use realloc(),
    // it's most efficient.
    if (buf->large != NULL)
    {
        newCap = _newLargeBufSize(pool, buf->cap, capacity);
        buf->large->data = nats_realloc(buf->large->data, newCap);
        if (buf->large->data == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
        buf->buf.data = buf->large->data;
        buf->cap = newCap;
        copy = false;
        return NATS_OK;
    }

    if (capacity > _smallMax(pool))
    {
        // We don't fit in a small, allocate a large.
        natsLarge *newLarge = NULL;
        newCap = _newLargeBufSize(pool, buf->cap, capacity);
        data = _allocLarge(pool, newCap, &newLarge DEV_MODE_CTX);
        if (data == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
        buf->small = NULL;
        buf->large = newLarge;
    }
    else
    {
        // If a fixed size buffer, treat it as a normal pool allocation.
        if (buf->isFixedSize)
        {
            data = _allocSmall(NULL, pool, capacity DEV_MODE_CTX);
            if (data == NULL)
                return nats_setDefaultError(NATS_NO_MEMORY);
            newCap = capacity;
            buf->small = NULL;
        }
        else
        {
            // Take up a whole page since we may be expanding anyway. Save the
            // 'small' pointer in case we need to return the memory page to the
            // pool.
            newCap = _smallMax(pool);
            data = _allocSmall(&buf->small, pool, newCap DEV_MODE_CTX);
            if (data == NULL)
                return nats_setDefaultError(NATS_NO_MEMORY);
            buf->large = NULL;
        }
    }

    if (copy && !nats_IsStringEmpty(&buf->buf))
        memcpy(data, buf->buf.data, buf->buf.len);
    buf->buf.data = data;
    buf->cap = newCap;

    // If we were previously in a small, return the space to the pool, and zero
    // it out.
    if (prevSmall != NULL)
    {
        prevSmall->len -= prevCap;
        memset((uint8_t *)prevSmall + prevSmall->len, 0, prevCap);
    }

    return NATS_OK;
}

natsStatus
natsBuf_Reset(natsBuf *buf)
{
    if (buf == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);
    buf->buf.len = 0;
    return NATS_OK;
}

static natsStatus
_createBuf(natsBuf **newBuf, natsPool *pool, size_t capacity, bool fixedSize)
{
    natsBuf *buf = nats_palloc(pool, sizeof(natsBuf));
    if (buf == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);
    buf->pool = pool;
    buf->isFixedSize = fixedSize;

    if (capacity == 0)
    {
        if (fixedSize)
            return nats_setDefaultError(NATS_INVALID_ARG);
        else
            capacity = 1;
    }

    natsStatus s = _expandBuf(buf, capacity);
    if (s != NATS_OK)
        return s;

    *newBuf = buf;
    return NATS_OK;
}

natsStatus natsPool_getFixedBuf(natsBuf **newBuf, natsPool *pool, size_t capacity)
{
    return _createBuf(newBuf, pool, capacity, true);
}

natsStatus natsPool_getGrowableBuf(natsBuf **newBuf, natsPool *pool, size_t capacity)
{
    return _createBuf(newBuf, pool, capacity, false);
}

void natsPool_log_recycleBuf(natsBuf *buf DEV_MODE_ARGS)
{
    if (buf == NULL)
        return;

    if (buf->large != NULL)
    {
        POOLTRACEx("-l  ", "recycling large from natsBuf, ptr:%p", (void *)buf->large->data);
        nats_free(buf->large->data);
    }
    else if (buf->small != NULL)
    {
        buf->small->len -= buf->cap;
        memset((uint8_t *)buf->small + buf->small->len, 0, buf->cap);
        POOLTRACEx("-s  ", "recycling small from natsBuf, ptr:%p", (void *)buf->small);
    }

    memset(buf, 0, sizeof(natsBuf));
}

natsStatus
natsBuf_addBB(natsBuf *buf, const uint8_t *data, size_t len)
{
    natsStatus s = NATS_OK;
    size_t n = buf->buf.len + len;

    if (n > buf->cap)
        s = _expandBuf(buf, n);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    memcpy(buf->buf.data + buf->buf.len, data, len);
    buf->buf.len += len;

    return NATS_OK;
}

natsStatus
natsBuf_addB(natsBuf *buf, uint8_t b)
{
    natsStatus s = NATS_OK;
    size_t n;

    if ((n = buf->buf.len + 1) > buf->cap)
        s = _expandBuf(buf, n);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (STILL_OK(s))
    {
        buf->buf.data[buf->buf.len] = b;
        buf->buf.len++;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

void *nats_log_palloc(natsPool *pool, size_t size DEV_MODE_ARGS)
{
    void *mem;

    if (size > _smallMax(pool))
        mem = _allocLarge(pool, size, NULL DEV_MODE_PASSARGS);
    else
        mem = _allocSmall(NULL, pool, size DEV_MODE_PASSARGS);

#ifdef DEV_MODE
    uint8_t *ptr = (uint8_t *)mem;
    for (size_t i = 0; i < size; i++)
        if (ptr[i] != 0)
            POOLDEBUGf("<>/<> %s: allocated memory not zeroed out, size:%zu, ptr:%p\n", pool->name, size, mem);
#endif

    return mem;
}

natsStatus nats_log_createPool(natsPool **newPool, natsMemOptions *opts, const char *name DEV_MODE_ARGS)
{
    const size_t required = sizeof(natsPool) + sizeof(natsSmall);
    if ((newPool == NULL) || (opts == NULL) || (opts->heapPageSize < required))
        return nats_setDefaultError(NATS_INVALID_ARG);

    void *mempage = NULL;
    if (required > opts->heapPageSize)
        return nats_setError(NATS_INVALID_ARG, "page size %zu too small, need at least %zu", opts->heapPageSize, required);
    mempage = nats_alloc(opts->heapPageSize, NATS_MEM_ZERO_OUT);
    if (mempage == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    *newPool = _initializePool(mempage, opts, name);

    POOLTRACEx("POOL", "%s: created, pageSize:%zu, heap: %p", name, (*newPool)->opts->heapPageSize, (void *)((*newPool)->small));
    return NATS_OK;
}
