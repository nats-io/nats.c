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

#ifndef OPTS_H_
#define OPTS_H_

#include "natsp.h"

#define LOCK_AND_CHECK_OPTIONS(o, c) \
    if (((o) == NULL) || ((c))) \
        return nats_setDefaultError(NATS_INVALID_ARG); \
    natsMutex_Lock((o)->mu);

#define UNLOCK_OPTS(o) natsMutex_Unlock((o)->mu)


#define NATS_OPTS_DEFAULT_MAX_RECONNECT       (60)
#define NATS_OPTS_DEFAULT_TIMEOUT             (2 * 1000)          // 2 seconds
#define NATS_OPTS_DEFAULT_RECONNECT_WAIT      (2 * 1000)          // 2 seconds
#define NATS_OPTS_DEFAULT_PING_INTERVAL       (2 * 60 * 1000)     // 2 minutes
#define NATS_OPTS_DEFAULT_MAX_PING_OUT        (2)
#define NATS_OPTS_DEFAULT_MAX_PENDING_MSGS    (65536)
#define NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE  (8 * 1024 * 1024)   // 8 MB

natsOptions*
natsOptions_clone(natsOptions *opts);

#endif /* OPTS_H_ */
