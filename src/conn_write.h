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

#ifndef CONN_WRITE_H_
#define CONN_WRITE_H_

typedef struct __natsWriteBuffer_s natsWriteBuffer;
typedef struct __natsWriteQueue_s natsWriteQueue;

typedef void (*natsOnWrittenF)(natsConnection *nc, uint8_t *buffer, void *closure);

struct __natsWriteBuffer_s
{
    natsString buf; // must be first, so natsWrieBuf* is also a natsString*
    size_t written;
    natsOnWrittenF done;
    void *userData;
};

struct __natsWriteQueue_s
{
    natsMemOptions *opts;
    size_t capacity;
    natsWriteBuffer *chain; // an allocated array of 'capacity'
    size_t start;
    size_t end;
};

natsStatus natsConn_asyncWrite(natsConnection *nc, const natsString *buf, natsOnWrittenF donef, void *doneUserData);

natsStatus natsWriteChain_init(natsWriteQueue *w, natsMemOptions *opts);
natsStatus natsWriteChain_add(natsWriteQueue *w, const natsString *buf, natsOnWrittenF donef, void *doneUserData);
natsWriteBuffer *natsWriteChain_get(natsWriteQueue *w);
natsStatus natsWriteChain_done(natsConnection *nc, natsWriteQueue *w);

#define natsWriteChain_cap(_ch) ((_ch)->capacity)
#define natsWriteChain_startPos(_ch) ((_ch)->start % (_ch)->capacity)
#define natsWriteChain_endPos(_ch) ((_ch)->end % (_ch)->capacity)
#define natsWriteChain_len(_w) ((_w)->end - (_w)->start)
#define natsWriteChain_isWrapped(_ch) (natsWriteChain_endPos(_ch) < natsWriteChain_startPos(_ch))
#define natsWriteChain_isEmpty(_ch) ((_ch)->start == (_ch)->end)
#define natsWriteChain_isFull(_ch) ((natsWriteChain_len(_ch) + 1) == natsWriteChain_cap(_ch))

#endif /* CONN_WRITE_H_ */
