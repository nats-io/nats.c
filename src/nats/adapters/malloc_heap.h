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

#ifndef NATS_MALLOC_HEAP_H_
#define NATS_MALLOC_HEAP_H_

#include "../nats.h"
#include <string.h>

// ----------------------------------------------------------------------------
// C global heap (malloc) adapter

// dependencies
static void *nats_C_malloc(natsHeap *h, size_t size, bool zero)
{
    return zero ? calloc(1, size) : malloc(size);
}

static void *nats_C_realloc(natsHeap *h, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void nats_C_free(natsHeap *h, void *ptr)
{
    free(ptr);
}

static char * nats_C_strdup(natsHeap *h, const char *src)
{
    return strdup(src);
}

static void nats_C_destroy(natsHeap *h)
{
    free(h);
}

static natsHeap *nats_NewMallocHeap(void)
{
    natsHeap *h = (natsHeap *)malloc(sizeof(natsHeap));
    if (h != NULL)
    {
        h->alloc = nats_C_malloc;
        h->realloc = nats_C_realloc;
        h->free = nats_C_free;
        h->strdup = nats_C_strdup;
        h->destroy = nats_C_destroy;
    }
    return h;
}

#endif /* MEM_HEAP_H_ */
