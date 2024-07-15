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

#ifndef MSG_H_
#define MSG_H_

#include "status.h"
#include "gc.h"

#define HDR_LINE_PRE        "NATS/1.0"
#define HDR_LINE_PRE_LEN    (8)
#define HDR_LINE            HDR_LINE_PRE _CRLF_
#define HDR_LINE_LEN        (10)
#define STATUS_HDR          "Status"
#define DESCRIPTION_HDR     "Description"
#define NO_RESP_STATUS      "503"
#define NOT_FOUND_STATUS    "404"
#define REQ_TIMEOUT         "408"
#define CTRL_STATUS         "100"
#define HDR_STATUS_LEN      (3)

#define natsMsg_setNeedsLift(m)     ((m)->flags  |=  (1 << 0))
#define natsMsg_needsLift(m)        (((m)->flags &   (1 << 0)) != 0)
#define natsMsg_clearNeedsLift(m)   ((m)->flags  &= ~(1 << 0))

#define natsMsg_setAcked(m)         ((m)->flags  |=  (1 << 1))
#define natsMsg_isAcked(m)          (((m)->flags &   (1 << 1)) != 0)
#define natsMsg_clearAcked(m)       ((m)->flags  &= ~(1 << 1))

#define natsMsg_setNoDestroy(m)     ((m)->flags  |=  (1 << 2))
#define natsMsg_isNoDestroy(m)      (((m)->flags &   (1 << 2)) != 0)
#define natsMsg_clearNoDestroy(m)   ((m)->flags  &= ~(1 << 2))
#define natsMsg_noDestroyFlag       (1 << 2)

#define natsMsg_setTimeout(m)       ((m)->flags  |=  (1 << 3))
#define natsMsg_isTimeout(m)        (((m)->flags &   (1 << 3)) != 0)
#define natsMsg_clearTimeout(m)     ((m)->flags  &= ~(1 << 3))

#define natsMsg_dataAndHdrLen(m)    ((m)->dataLen + (m)->hdrLen)

struct __natsMsg
{
    natsGCItem          gc;

    // The message is allocated as a single memory block that contains
    // this structure and enough space for the headers/payload.
    // Headers, if any, start after the 'next' pointer, followed by
    // the message payload.
    const char          *subject;
    const char          *reply;
    char                *hdr;
    natsStrHash         *headers;
    const char          *data;
    int                 dataLen;
    int                 hdrLen;
    int                 wsz;
    int                 flags;
    uint64_t            seq;
    int64_t             time;

    // subscription (needed when delivery done by connection,
    // or for JetStream).
    struct __natsSubscription *sub;

    // Must be last field!
    struct __natsMsg    *next;

    // Nothing after this: the message headers/payload goes there.

};

struct __natsHeaderValue;

typedef struct __natsHeaderValue
{
    char                        *value;
    bool                        needFree;
    struct __natsHeaderValue    *next;

} natsHeaderValue;

int
natsMsgHeader_encodedLen(natsMsg *msg);

natsStatus
natsMsgHeader_encode(natsBuffer *buf, natsMsg *msg);

void
natsMsg_init(natsMsg *msg,
             const char *subject,
             const char *data, int dataLen);

natsStatus
natsMsg_create(natsMsg **newMsg,
               const char *subject, int subjLen,
               const char *reply, int replyLen,
               const char *buf, int bufLen, int hdrLen);

natsStatus
natsMsg_createWithPadding(natsMsg **newMsg,
                          const char *subject, int subjLen,
                          const char *reply, int replyLen,
                          const char *buf, int bufLen, int bufPaddingSize,
                          int hdrLen);

void
natsMsg_freeHeaders(natsMsg *msg);

// This needs to follow the nats_FreeObjectCb prototype (see gc.h)
void
natsMsg_free(void *object);

#endif /* MSG_H_ */
