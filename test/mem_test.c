// Copyright 2015-2023 The NATS Authors
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
#include "test.h"
#include "mem.h"

void Test_MemAlignment(void)
{
    natsMemOptions *opts = &nats_defaultMemOptions;
    test("Check memPages");
    testCond((nats_numPages(opts, 0) == 0) &&
             (nats_numPages(opts, 1) == 1) &&
             (nats_numPages(opts, opts->heapPageSize) == 1) &&
             (nats_numPages(opts, opts->heapPageSize + 1) == 2));

    test("Check memPageAlignedSize");
    testCond((nats_pageAlignedSize(opts, 0) == 0) &&
             (nats_pageAlignedSize(opts, 1) == opts->heapPageSize) &&
             (nats_pageAlignedSize(opts, opts->heapPageSize - 1) == opts->heapPageSize) &&
             (nats_pageAlignedSize(opts, opts->heapPageSize) == opts->heapPageSize) &&
             (nats_pageAlignedSize(opts, opts->heapPageSize + 1) == 2 * opts->heapPageSize));
}

void Test_MemPoolAlloc(void)
{
    natsPool *pool = NULL;
    natsMemOptions opts = nats_defaultMemOptions;
    size_t expectedCurrentFreeBlockLen = 0;

    test("Set page size to 1024 bytes");
    opts.heapPageSize = 1024;
    testCond(true);

    char name[] = "mem-test";
    test("Create pool");
    natsStatus s = nats_createPool(&pool, &opts, name);
    size_t expectedLength = sizeof(natsPool) + sizeof(natsSmall) + strlen(name) + 1;
    testCond((STILL_OK(s)) &&
             (pool != NULL) &&
             (pool->small != NULL) &&
             (pool->small->len == expectedLength));

    // ------------------------------------------------
    // Small allocations.

    test("Allocate some small blocks in the first chunk");
    uint8_t *ptr1 = nats_palloc(pool, 10);
    uint8_t *ptr2 = nats_palloc(pool, 20);
    uint8_t *ptr3 = nats_palloc(pool, 30);
    size_t prevLen = expectedLength;
    expectedLength += 10 + 20 + 30;
    testCond((ptr1 != NULL) && (ptr2 != NULL) && (ptr3 != NULL) &&
             (pool->small->next == NULL) &&
             (pool->small->len == expectedLength) &&
             (uint8_t *)pool->small + prevLen == ptr1 &&
             ptr2 == ptr1 + 10 &&
             ptr3 == ptr2 + 20);

    test("Allocate a block that fits exactly in the rest of the first chunk");
    uint8_t *ptr4 = nats_palloc(pool, opts.heapPageSize - expectedLength);
    expectedLength = opts.heapPageSize;
    testCond((ptr4 != NULL) &&
             (pool->small->next == NULL) &&
             (pool->small->len == expectedLength) &&
             ptr4 == ptr3 + 30);

    test("Allocate one more byte and see it make a new chunk");
    uint8_t *ptr5 = nats_palloc(pool, 1);
    expectedLength = sizeof(natsSmall) + 1;
    testCond((ptr5 != NULL) &&
             (pool->small->next != NULL) &&
             (pool->small->len == opts.heapPageSize) &&
             (pool->small->next->next == NULL) &&
             (pool->small->next->len == expectedLength) &&
             ptr5 == (uint8_t *)pool->small->next + sizeof(natsSmall));
    expectedCurrentFreeBlockLen = expectedLength;

    // ------------------------------------------------
    // natsBuf.

    test("Make a natsBuf and see it take another chunk");
    natsBuf *buf = NULL;
    s = natsPool_getGrowableBuf(&buf, pool, 10);
    expectedLength = opts.heapPageSize;
    testCond((STILL_OK(s)) &&
             (buf != NULL) &&
             (pool->small->next != NULL) &&
             (pool->small->next->next != NULL) &&
             (pool->small->next->next->next == NULL) &&
             (pool->small->next->next->len == expectedLength) &&
             (buf->buf.data == (uint8_t *)pool->small->next->next + sizeof(natsSmall)));

    test("Check that natsBuf struct is allocated in the second chunk");
    expectedCurrentFreeBlockLen += sizeof(natsBuf);
    testCond(pool->small->next->len == expectedCurrentFreeBlockLen);

    test("Fill up the second chunk");
    uint8_t *ptr6 = nats_palloc(pool, opts.heapPageSize - expectedCurrentFreeBlockLen);
    expectedLength = opts.heapPageSize;
    testCond((ptr6 != NULL) &&
             (pool->small->next->len == expectedLength) &&
             ptr6 == (uint8_t *)pool->small->next + expectedCurrentFreeBlockLen);

    test("Allocate more, to force another, 4th chunk");
    uint8_t *ptr7 = nats_palloc(pool, 10);
    expectedLength = sizeof(natsSmall) + 10;
    testCond((ptr7 != NULL) &&
             (pool->small->next->next->next != NULL) &&
             (pool->small->next->next->next->next == NULL) &&
             (pool->small->next->next->next->len == expectedLength) &&
             ptr7 == (uint8_t *)pool->small->next->next->next + sizeof(natsSmall));

    test("Expand natsBuf into the heap, and allocate again, in the 3rd chunk that's returned");
    uint8_t aLotOfGarbage[2031];
    s = natsBuf_addBB(buf, aLotOfGarbage, sizeof(aLotOfGarbage));
    testCond((STILL_OK(s)) &&
             (pool->small->next->next->len == sizeof(natsSmall)) &&
             (pool->large != NULL) && (pool->large->prev == NULL) &&
             (buf->buf.data == pool->large->data) &&
             (memcmp(pool->large->data, aLotOfGarbage, sizeof(aLotOfGarbage)) == 0) &&
             (buf->buf.len == sizeof(aLotOfGarbage)) &&
             (buf->cap == nats_pageAlignedSize(&opts, sizeof(aLotOfGarbage))));

    // ------------------------------------------------
    // Large allocations.

    test("Allocate 2 large blocks");
    uint8_t *ptr8 = nats_palloc(pool, opts.heapPageSize + 1);
    uint8_t *ptr9 = nats_palloc(pool, opts.heapPageSize + 2);
    testCond((ptr8 != NULL) && (ptr9 != NULL) &&
             (pool->large != NULL) &&
             (pool->large->data == ptr9) &&
             (pool->large->prev != NULL) &&
             (pool->large->prev->data == ptr8) &&
             (pool->large->prev->prev != NULL) && // natsBuf
             (pool->large->prev->prev->prev == NULL));
    nats_releasePool(pool);

    // ------------------------------------------------
    // TODO: <>/<> Test ReadChain.

    // ------------------------------------------------
    // TODO: <>/<> Test recycle.

    // ------------------------------------------------
    // Error cases

    test("Set page size to 2 bytes");
    opts.heapPageSize = 2;
    testCond(true);

    test("Fail to create pool");
    s = nats_createPool(&pool, &opts, "mem-test");
    testCond((s == NATS_INVALID_ARG));
}

static natsStatus _allocFilledChunk(natsPool *pool, size_t size, char fill)
{
    uint8_t *ptr = nats_palloc(pool, size);
    if (ptr == NULL)
        return NATS_NO_MEMORY;
    memset(ptr, fill, size);
    return NATS_OK;
}

void Test_MemPoolRecycle(void)
{
    natsPool *pool = NULL;
    natsMemOptions opts = nats_defaultMemOptions;
    void *first, *second;

    test("Set page size to 1024 bytes");
    opts.heapPageSize = 1024;
    testCond(true);

    test("Create pool");
    char name[] = "recycle-test";
    size_t expectedLengthFirst = sizeof(natsSmall) + sizeof(natsPool) + strlen(name) + 1;
     natsStatus s = nats_createPool(&pool, &opts, name);
    testCond(STILL_OK(s) && 
        pool->small->len == expectedLengthFirst);

    test("fill the rest of the first small chunk with 'A's");
    s = _allocFilledChunk(pool, opts.heapPageSize - pool->small->len, 'A');
    testCond(STILL_OK(s) && (pool->small->next == NULL) && (pool->small->len == opts.heapPageSize));
    first = pool->small;

    test("Allocate second small chunk wih B's");
    s = _allocFilledChunk(pool, opts.heapPageSize - sizeof(natsSmall), 'B');
    testCond(STILL_OK(s) &&
             (pool->small->next != NULL) &&
             (pool->small->next->len == opts.heapPageSize));
    second = pool->small->next;

    test("Allocate third small chunk wih C's");
    s = _allocFilledChunk(pool, opts.heapPageSize - sizeof(natsSmall), 'B');
    testCond(STILL_OK(s) &&
             (pool->small->next->next != NULL) &&
             (pool->small->next->next->len == opts.heapPageSize));

    test("Get a read buffer");
    natsReadBuffer *rbuf = NULL;
    s = natsPool_getReadBuffer(&rbuf, pool);
    testCond(STILL_OK(s) &&
             (rbuf != NULL) &&
             (rbuf->buf.len == 0) &&
             (rbuf->readFrom == rbuf->buf.data));

    test("Mark bytes 100:200 as remaining");
    memset(rbuf->buf.data, 'D', opts.readBufferSize);
    rbuf->readFrom = rbuf->buf.data + 100;
    rbuf->buf.len = 200;
    testCond(true);

    test("Recycle pool");
    s = nats_recyclePool(&pool, &rbuf);
    testCond(STILL_OK(s) && (pool != NULL) && (rbuf != NULL));

    test("Check the first small's pointers");
    testCond((pool->small == first) &&
             (pool->small->len == expectedLengthFirst + sizeof(natsReadBuffer) + sizeof(natsReadChain)) &&
             (pool->small->next != NULL));

    test("Check that the first small is zeroed out");
    bool zeroed = true;
    uint8_t *ptr = (uint8_t *)pool->small + pool->small->len;
    for (size_t i = 0; i < opts.heapPageSize - pool->small->len; i++, ptr++)
    {
        if (*ptr != 0)
        {
            printf("First small chunk not zeroed out at %zu %zu\n", i, i);
            zeroed = false;
            break;
        }
    }
    testCond(zeroed);

    test("Check the second small's pointers");
    testCond(  (pool->small->next == second) &&
             (pool->small->next->len == sizeof(natsSmall)) &&
             (pool->small->next->next == NULL));
}
