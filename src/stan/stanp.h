// Copyright 2018 The NATS Authors
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

#ifndef STANP_H_
#define STANP_H_

#include "../natsp.h"
#include "../mem.h"
#include "protocol.pb-c.h"
#include "protobuf-c/protobuf-c.h"

extern bool testAllowMillisecInPings;

struct __stanConnOptions
{
    natsMutex                   *mu;

    // URL to connect to, unless ncOpts is not NULL.
    char                        *url;

    // Low level NATS connection options to use to create the NATS connection.
    natsOptions                 *ncOpts;

    // Discovery prefix. The connect request is sent to that + "." + name of cluster.
    char                        *discoveryPrefix;

    // Connection create/close request timeout (in milliseconds).
    int64_t                     connTimeout;

    // How long (in milliseconds) to wait for a published message ack.
    int64_t                     pubAckTimeout;
    // Max number of messages that can be sent without receiving corresponding ack from server.
    int                         maxPubAcksInflight;
    // Percentage of above value to decide when to release a blocked publish call.
    float                       maxPubAcksInFlightPercentage;

    // Ping interval (number of seconds, except in tests where it can be interpreted as milliseconds)
    int                         pingInterval;
    // Max number of PINGs without receiving any PONG
    int                         pingMaxOut;

    // Callback and closure to invoke when connection is permanently lost.
    stanConnectionLostHandler   connectionLostCB;
    void                        *connectionLostCBClosure;
};

struct __stanSubOptions
{
    natsMutex                   *mu;

    // DurableName, if set will survive client restarts.
    char                        *durableName;

    // Controls the number of messages the cluster will have inflight without an ACK.
    int                         maxInflight;

    // Controls the time the cluster will wait for an ACK for a given message.
    // This is in milliseconds.
    int64_t                     ackWait;

    // StartPosition enum from proto.
    Pb__StartPosition           startAt;

    // Optional start sequence number.
    uint64_t                    startSequence;

    // Optional start time (expressed in milliseconds)
    int64_t                     startTime;

    // Option to do Manual Acks
    bool                        manualAcks;
};


typedef struct __pubAck
{
    char                *guid;
    int64_t             deadline;
    stanPubAckHandler   ah;
    void                *ahClosure;
    char                *error;
    bool                dontFreeError;
    bool                received;
    bool                isSync;
    struct __pubAck     *prev;
    struct __pubAck     *next;

} _pubAck;

typedef struct __natsPBufAllocator
{
    ProtobufCAllocator  base;

    char                *buf;
    int                 cap;
    int                 used;
    int                 remaining;
    int                 protoSize;
    int                 overhead;

} natsPBufAllocator;

struct __stanConnection
{
    natsMutex           *mu;
    int                 refs;

    stanConnOptions     *opts;

    natsConnection      *nc;

    char                *clientID;
    char                *connID;
    int                 connIDLen;

    char                *pubPrefix;
    char                *subRequests;
    char                *unsubRequests;
    char                *subCloseRequests;
    char                *closeRequests;

    char                *ackSubject;
    natsSubscription    *ackSubscription;

    natsInbox           *hbInbox;
    natsSubscription    *hbSubscription;

    natsMutex           *pubAckMu;
    natsStrHash         *pubAckMap;
    natsCondition       *pubAckCond;
    int                 pubAckInWait;
    natsCondition       *pubAckMaxInflightCond;
    int                 pubAckMaxInflightThreshold;
    bool                pubAckMaxInflightInWait;
    bool                pubAckClosed;

    _pubAck             *pubAckHead;
    _pubAck             *pubAckTail;
    natsTimer           *pubAckTimer;
    bool                pubAckTimerNeedReset;
    natsPBufAllocator   *pubAckAllocator;

    char                *pubMsgBuf;
    int                 pubMsgBufCap;

    int                 pubPrefixLen;
    char                *pubSubjBuf;
    int                 pubSubjBufCap;

    natsMutex           *pingMu;
    natsSubscription    *pingSub;
    natsTimer           *pingTimer;
    char                *pingBytes;
    int                 pingBytesLen;
    char                *pingRequests;
    char                *pingInbox;
    int                 pingOut;

    char                *connLostErrTxt;

    bool                closed;
};

struct __stanMsg
{
    natsGCItem          gc;

    // The message is allocated as a single memory block that contains
    // this structure and enough space for the payload. The msg payload
    // starts after the 'next' pointer.
    uint64_t            seq;
    int64_t             timestamp;
    const char          *data;
    int                 dataLen;
    stanSubscription    *sub;
    bool                redelivered;

    // Must be last field!
    struct __natsMsg    *next;

    // Nothing after this: the message payload goes there.
};

struct __stanSubscription
{
    natsMutex           *mu;
    int                 refs;

    stanSubOptions      *opts;

    stanConnection      *sc;

    char                *channel;

    char                *qgroup;

    char                *inbox;
    natsSubscription    *inboxSub;

    char                *ackInbox;

    stanMsgHandler      cb;
    void                *cbClosure;

    // count messages received, and use it compared to MaxInflight
    // to cause a low level buffer flush when sending ACK.
    int                 msgs;

    char                *ackBuf;
    int                 ackBufCap;

    natsPBufAllocator   *allocator;

    bool                closed;
};

#endif /* STANP_H_ */
