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

#ifndef SOPTS_H_
#define SOPTS_H_

#include "stanp.h"

#define STAN_SUB_OPTS_DEFAULT_MAX_INFLIGHT       (1024)
#define STAN_SUB_OPTS_DEFAULT_ACK_WAIT           (30 * 1000) // 30 seconds

natsStatus
stanSubOptions_clone(stanSubOptions **clonedOpts, stanSubOptions *opts);

#endif /* SOPTS_H_ */
