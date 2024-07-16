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

#include "hash.h"

#define _OFF32 (2166136261)
#define _YP32 (709607)

#define _BSZ (8)
#define _WSZ (4)

static int _DWSZ = _WSZ << 1;  // 8
static int _DDWSZ = _WSZ << 2; // 16

static int _MAX_BKT_SIZE = (1 << 30) - 1;

// Jesteress derivative of FNV1A from [http://www.sanmayce.com/Fastest_Hash/]
uint32_t
natsStrHash_Hash(const char *data, int dataLen)
{
    int i = 0;
    int dlen = dataLen;
    uint32_t h32 = (uint32_t)_OFF32;
    uint64_t k1, k2;
    uint32_t k3;

    for (; dlen >= _DDWSZ; dlen -= _DDWSZ)
    {
        memcpy(&k1, &(data[i]), sizeof(k1));
        memcpy(&k2, &(data[i + 4]), sizeof(k2));
        h32 = (uint32_t)((((uint64_t)h32) ^ ((k1 << 5 | k1 >> 27) ^ k2)) * _YP32);
        i += _DDWSZ;
    }

    // Cases: 0,1,2,3,4,5,6,7
    if ((dlen & _DWSZ) != 0)
    {
        memcpy(&k1, &(data[i]), sizeof(k1));
        h32 = (uint32_t)((((uint64_t)h32) ^ k1) * _YP32);
        i += _DWSZ;
    }
    if ((dlen & _WSZ) != 0)
    {
        memcpy(&k3, &(data[i]), sizeof(k3));
        h32 = (uint32_t)((((uint64_t)h32) ^ (uint64_t)k3) * _YP32);
        i += _WSZ;
    }
    if ((dlen & 1) != 0)
    {
        h32 = (h32 ^ (uint32_t)(data[i])) * _YP32;
    }

    return h32 ^ (h32 >> 16);
}

// pool is optional. If provided, all allocations will be done in the (ever
// growing) pool, otherwise NATS_CALLOC/NATS_FREE are used.
natsStatus
natsStrHash_Create(natsStrHash **newHash, natsPool *pool, int initialSize)
{
    natsStrHash *hash = NULL;

    if ((initialSize <= 0) || (initialSize > _MAX_BKT_SIZE) || (pool == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((initialSize & (initialSize - 1)) != 0)
    {
        // Size of buckets must be power of 2
        initialSize--;
        initialSize |= initialSize >> 1;
        initialSize |= initialSize >> 2;
        initialSize |= initialSize >> 4;
        initialSize |= initialSize >> 8;
        initialSize |= initialSize >> 16;
        initialSize++;
    }

    hash = nats_palloc(pool, sizeof(natsStrHash));
    if (hash == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    hash->pool = pool;
    hash->mask = (initialSize - 1);
    hash->numBkts = initialSize;
    hash->canResize = true;
    hash->bkts = (natsStrHashEntry **)nats_palloc(pool, initialSize * sizeof(natsStrHashEntry *));
    if (hash->bkts == NULL)
    {
        return nats_setDefaultError(NATS_NO_MEMORY);
    }

    *newHash = hash;

    return NATS_OK;
}

static natsStatus
_resizeStr(natsStrHash *hash, int newSize)
{
    natsStrHashEntry **bkts = NULL;
    int newMask = newSize - 1;
    natsStrHashEntry *ne;
    natsStrHashEntry *e;
    int k;
    int newIndex;

    bkts = (natsStrHashEntry **)nats_palloc(hash->pool, newSize * sizeof(natsStrHashEntry *) );
    if (bkts == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    for (k = 0; k < hash->numBkts; k++)
    {
        e = hash->bkts[k];
        while (e != NULL)
        {
            ne = e;
            e = e->next;

            newIndex = ne->hk & newMask;
            ne->next = bkts[newIndex];
            bkts[newIndex] = ne;
        }
    }

    hash->bkts = bkts;
    hash->mask = newMask;
    hash->numBkts = newSize;

    return NATS_OK;
}

static natsStatus
_growStr(natsStrHash *hash)
{
    // Can't grow beyond max signed int for now
    if (hash->numBkts >= _MAX_BKT_SIZE)
        return nats_setDefaultError(NATS_NO_MEMORY);

    return _resizeStr(hash, 2 * (hash->numBkts));
}

// Note that it would be invalid to call with copyKey:true and freeKey:false,
// since this would lead to a memory leak.
natsStatus
natsStrHash_Set(natsStrHash *hash, char *key, void *data)
{
    natsStatus s = NATS_OK;
    uint32_t hk = 0;
    int index = 0;
    natsStrHashEntry *newEntry = NULL;
    natsStrHashEntry *e;

    hk = natsStrHash_Hash(key, nats_strlen(key));
    index = hk & hash->mask;

    e = (natsStrHashEntry *)hash->bkts[index];
    while (e != NULL)
    {
        if ((e->hk != hk) || (strcmp(e->key, key) != 0))
        {
            e = e->next;
            continue;
        }
        // Success, replace data field
        e->data = data;
        return NATS_OK;
    }

    // We have a new entry here
    newEntry = (natsStrHashEntry *)nats_palloc(hash->pool, sizeof(natsStrHashEntry));
    if (newEntry == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    newEntry->hk = hk;
    newEntry->key = key;
    newEntry->data = data;
    newEntry->next = hash->bkts[index];
    hash->bkts[index] = newEntry;
    hash->used++;

    // Check for resizing
    if (hash->canResize && (hash->used > hash->numBkts))
        s = _growStr(hash);

    return NATS_UPDATE_ERR_STACK(s);
}

void *
natsStrHash_Get(natsStrHash *hash, char *key, int keyLen)
{
    natsStrHashEntry *e;
    uint32_t hk = natsStrHash_Hash(key, keyLen);

    e = hash->bkts[hk & hash->mask];
    while (e != NULL)
    {
        if ((e->hk == hk) && (strncmp(e->key, key, keyLen) == 0))
        {
            return e->data;
        }

        e = e->next;
    }

    return NULL;
}

void natsStrHashIter_Init(natsStrHashIter *iter, natsStrHash *hash)
{
    memset(iter, 0, sizeof(natsStrHashIter));

    hash->canResize = false;
    iter->hash = hash;
    iter->current = hash->bkts[0];
    iter->next = iter->current;
}

bool natsStrHashIter_Next(natsStrHashIter *iter, char **key, void **value)
{
    if ((iter->started) && (iter->next == NULL))
        return false;

    if (!(iter->started) && (iter->current == NULL))
    {
        while ((iter->next == NULL) && (iter->currBkt < (iter->hash->numBkts - 1)))
        {
            iter->next = iter->hash->bkts[++(iter->currBkt)];
        }

        if (iter->next == NULL)
        {
            iter->started = true;
            return false;
        }
    }

    iter->started = true;

    iter->current = iter->next;
    if (iter->current != NULL)
    {
        if (key != NULL)
            *key = iter->current->key;
        if (value != NULL)
            *value = iter->current->data;

        iter->next = iter->current->next;
    }

    while ((iter->next == NULL) && (iter->currBkt < (iter->hash->numBkts - 1)))
    {
        iter->next = iter->hash->bkts[++(iter->currBkt)];
    }

    return true;
}

void natsStrHashIter_Done(natsStrHashIter *iter)
{
    iter->hash->canResize = true;
}
