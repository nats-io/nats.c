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

#ifndef STAN_CONN_H_
#define STAN_CONN_H_

#include "stanp.h"

#ifdef DEV_MODE
// For type safety

void stanConn_Lock(stanConnection *sc);
void stanConn_Unlock(stanConnection *sc);

#else
// We know what we are doing :-)

#define stanConn_Lock(c)    (natsMutex_Lock((c)->mu))
#define stanConn_Unlock(c)  (natsMutex_Unlock((c)->mu))

#endif // DEV_MODE

#define STAN_ERR_CONNECT_REQUEST_TIMEOUT    "connect request timeout"
#define STAN_ERR_CLOSE_REQUEST_TIMEOUT      "close request timeout"
#define STAN_ERR_MAX_PINGS                  "connection lost due to PING failure"

void
stanConn_retain(stanConnection *nc);

void
stanConn_release(stanConnection *nc);

natsStatus
stanConnClose(stanConnection *sc, bool sendProto);

natsStatus
expandBuf(char **buf, int *cap, int newcap);

natsStatus
natsPBufAllocator_Create(natsPBufAllocator **newAllocator, int protoSize, int overhead);

void
natsPBufAllocator_Prepare(natsPBufAllocator *allocator, int bufSize);

void
natsPBufAllocator_Destroy(natsPBufAllocator *allocator);

#endif /* STAN_CONN_H_ */
