// Copyright 2015-2022 The NATS Authors
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

#ifndef MSG_H_
#define MSG_H_

static const natsString nats_NATS10 = NATS_STR("NATS/1.0");

#define STATUS_HDR "Status"
#define DESCRIPTION_HDR "Description"
#define NO_RESP_STATUS "503"
#define NOT_FOUND_STATUS "404"
#define REQ_TIMEOUT "408"
#define CTRL_STATUS "100"
#define HDR_STATUS_LEN (3)

#define natsMessage_setNeedsLift(m) ((m)->flags |= (1 << 0))
#define natsMessage_needsLift(m) (((m)->flags & (1 << 0)) != 0)
#define natsMessage_clearNeedsLift(m) ((m)->flags &= ~(1 << 0))

#define natsMessage_setAcked(m) ((m)->flags |= (1 << 1))
#define natsMessage_isAcked(m) (((m)->flags & (1 << 1)) != 0)
#define natsMessage_clearAcked(m) ((m)->flags &= ~(1 << 1))

#define natsMessage_setTimeout(m) ((m)->flags |= (1 << 2))
#define natsMessage_isTimeout(m) (((m)->flags & (1 << 2)) != 0)
#define natsMessage_clearTimeout(m) ((m)->flags &= ~(1 << 2))

#define natsMessage_setOutgoing(m) ((m)->flags |= (1 << 3))
#define natsMessage_setIncoming(m) ((m)->flags &= ~(1 << 3))
#define natsMessage_isOutgoing(m) (((m)->flags & (1 << 3)) != 0)

struct __natsMessage
{
    natsString *subject;

    natsPool *pool;
    natsString *reply;
    natsStrHash *headers;

    natsString *out;
    natsReadBuffer *in;

    natsOnMessagePublishedF donef;
    void *doneClosure;

    void (*freef)(void *);
    void *freeClosure;

    int flags;
    int64_t time;
};

struct __natsHeaderValue;

typedef struct __natsHeaderValue
{
    char *value;
    bool needFree;
    struct __natsHeaderValue *next;

} natsHeaderValue;

int natsMessageHeader_encodedLen(natsMessage *msg);
natsStatus natsMessageHeader_encode(natsBuf *buf, natsMessage *msg);

#endif /* MSG_H_ */
