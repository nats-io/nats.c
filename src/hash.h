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

#ifndef HASH_H_
#define HASH_H_

typedef struct __natsStrHashEntry
{
    uint32_t hk;
    char *key;
    void *data;
    struct __natsStrHashEntry *next;
} natsStrHashEntry;

struct __natsStrHash
{
    natsPool *pool;
    struct __natsStrHashEntry **bkts;
    int numBkts;
    int mask;
    int used;
    bool canResize;
};

struct __natsStrHashIter
{
    struct __natsStrHash *hash;
    struct __natsStrHashEntry *current;
    struct __natsStrHashEntry *next;
    int currBkt;
    bool started;
};

#define natsStrHash_Count(h) ((h)->used)

//
// Hash with char* as the key, created in a memory pool.
//
natsStatus
natsStrHash_Create(natsStrHash **newHash, natsPool *pool, int initialSize);

uint32_t
natsStrHash_Hash(const char *data, int dataLen);

natsStatus
natsStrHash_Set(natsStrHash *hash, char *key, void *data);

void *
natsStrHash_Get(natsStrHash *hash, char *key, int keyLen);

//
// Iterator for Hash char*
//
void natsStrHashIter_Init(natsStrHashIter *iter, natsStrHash *hash);

bool natsStrHashIter_Next(natsStrHashIter *iter, char **key, void **value);

void natsStrHashIter_Done(natsStrHashIter *iter);

#endif /* HASH_H_ */
