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

#ifndef NATS_MEM_H_
#define NATS_MEM_H_

#include "nats_base.h"

// Strings

typedef struct __natsString_s
{
    size_t len;
    uint8_t *data;
} natsString;

static inline natsString nats_ToString(const char *str)
{
    natsString s;

    s.len = (str == NULL) ? 0 : strlen(str);
    s.data = (uint8_t *)str;

    return s;
}

static inline bool nats_IsStringEmpty(const natsString *str)
{
    return (str == NULL) || (str->len == 0);
}

static inline size_t nats_StringLen(const natsString *str)
{
    return nats_IsStringEmpty(str) ? 0 : str->len;
}

// Heap (implementation is provided by adapters, like nats/adapters/c_heap.h)

#define NATS_MEM_ZERO_OUT true
#define NATS_MEM_LEAVE_UNINITIALIZED false

typedef struct __natsHeap_s natsHeap;

typedef void *(*natsHeapAllocF)(natsHeap *h, size_t size, bool zero);
typedef void (*natsHeapDestroyF)(natsHeap *h);
typedef char *(*natsHeapStrdupF)(natsHeap *h, const char *s);
typedef void (*natsHeapFreeF)(natsHeap *h, void *ptr);
typedef void *(*natsHeapReallocF)(natsHeap *h, void *ptr, size_t);

struct __natsHeap_s
{
    natsHeapAllocF alloc;
    natsHeapReallocF realloc;
    natsHeapFreeF free;
    natsHeapStrdupF strdup;
    natsHeapDestroyF destroy;
};

// Pool

typedef struct __natsPool_s natsPool;

NATS_EXTERN natsStatus nats_CreatePool(natsPool **newPool, natsOptions *opts);
NATS_EXTERN void nats_RetainPool(natsPool *pool);
NATS_EXTERN void nats_ReleasePool(natsPool *pool);
NATS_EXTERN void *nats_Palloc(natsPool *pool, size_t size);

#endif /* NATS_MEM_H_ */
