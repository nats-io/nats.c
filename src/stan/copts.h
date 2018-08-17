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

#ifndef STAN_CONN_OPTS_H_
#define STAN_CONN_OPTS_H_

#include "stanp.h"

#define STAN_CONN_OPTS_DEFAULT_DISCOVERY_PREFIX                   "_STAN.discover"
#define STAN_CONN_OPTS_DEFAULT_PUB_ACK_TIMEOUT                    (30 * 1000)       // 30 seconds
#define STAN_CONN_OPTS_DEFAULT_CONN_TIMEOUT                       (2 * 1000)        // 2 seconds
#define STAN_CONN_OPTS_DEFAULT_MAX_PUB_ACKS_INFLIGHT              (16384)
#define STAN_CONN_OPTS_DEFAULT_MAX_PUB_ACKS_INFLIGHT_PERCENTAGE   (float) (0.5)     // 50% of MaxPubAcksInflight
#define STAN_CONN_OPTS_DEFAULT_PING_INTERVAL                      (5)               // 5 seconds
#define STAN_CONN_OPTS_DEFAULT_PING_MAX_OUT                       (3)

natsStatus
stanConnOptions_clone(stanConnOptions **clonedOpts, stanConnOptions *opts);

#endif /* STAN_CONN_OPTS_H_ */
