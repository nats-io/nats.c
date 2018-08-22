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

#ifndef ASYNCCB_H_
#define ASYNCCB_H_

#include "status.h"

typedef enum
{
    ASYNC_CLOSED          = 0,
    ASYNC_DISCONNECTED,
    ASYNC_RECONNECTED,
    ASYNC_ERROR,
    ASYNC_DISCOVERED_SERVERS,
    ASYNC_CONNECTED,

#if defined(NATS_HAS_STREAMING)
    ASYNC_STAN_CONN_LOST
#endif

} natsAsyncCbType;

struct __natsConnection;
struct __natsSubscription;
struct __natsAsyncCbInfo;

typedef struct __natsAsyncCbInfo
{
    natsAsyncCbType             type;
    struct __natsConnection     *nc;
    struct __natsSubscription   *sub;
    natsStatus                  err;

#if defined(NATS_HAS_STREAMING)
    struct __stanConnection     *sc;
#endif

    struct __natsAsyncCbInfo    *next;

} natsAsyncCbInfo;

void
natsAsyncCb_PostConnHandler(struct __natsConnection *nc, natsAsyncCbType type);

void
natsAsyncCb_PostErrHandler(struct __natsConnection *nc,
                           struct __natsSubscription *sub, natsStatus err);

#if defined(NATS_HAS_STREAMING)
void
natsAsyncCb_PostStanConnLostHandler(struct __stanConnection *sc);
#endif

void
natsAsyncCb_Destroy(natsAsyncCbInfo *info);

#endif /* ASYNCCB_H_ */
