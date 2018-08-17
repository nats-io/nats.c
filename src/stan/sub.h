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

#ifndef STAN_SUB_H_
#define STAN_SUB_H_

#include "stanp.h"

#ifdef DEV_MODE
// For type safety

void stanSub_Lock(stanSubscription *sub);
void stanSub_Unlock(stanSubscription *sub);

#else
// We know what we are doing :-)

#define stanSub_Lock(c)    (natsMutex_Lock((c)->mu))
#define stanSub_Unlock(c)  (natsMutex_Unlock((c)->mu))

#endif // DEV_MODE

#define STAN_ERR_SUBSCRIBE_REQUEST_TIMEOUT      "subscribe request timeout"
#define STAN_ERR_UNSUBSCRIBE_REQUEST_TIMEOUT    "unsubscribe request timeout"
#define STAN_ERR_MANUAL_ACK                     "cannot manually ack in auto-ack mode"
#define STAN_ERR_SUB_CLOSE_NOT_SUPPORTED        "server does not support subscription close"

void
stanSub_retain(stanSubscription *sub);

void
stanSub_release(stanSubscription *sub);


#endif /* STAN_SUB_H_ */
