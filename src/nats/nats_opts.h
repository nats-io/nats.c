// Copyright 2015-2023 The NATS Authors
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

#ifndef NATS_OPTS_H_
#define NATS_OPTS_H_

#include "nats_base.h"

/** \def NATS_EXTERN
 *  \brief Needed for shared library.
 *
 *  Based on the platform this is compiled on, it will resolve to
 *  the appropriate instruction so that objects are properly exported
 *  when building the shared library.
 */

#define NATS_DEFAULT_URL "nats://localhost:4222"

NATS_EXTERN natsOptions *nats_GetDefaultOptions(void);

// Network and connection options
NATS_EXTERN natsStatus nats_SetIPResolutionOrder(natsOptions *opts, int order);
NATS_EXTERN natsStatus nats_SetNoRandomize(natsOptions *opts, bool noRandomize);
NATS_EXTERN natsStatus nats_SetServers(natsOptions *opts, const char **servers, int serversCount);
NATS_EXTERN natsStatus nats_SetName(natsOptions *opts, const char *name);

NATS_EXTERN natsStatus nats_SetOnConnected(natsOptions *opts, natsOnConnectionEventF f, void *closure);
NATS_EXTERN natsStatus nats_SetOnConnectionClosed(natsOptions *opts, natsOnConnectionEventF f, void *closure);

// NATS protocol options
NATS_EXTERN natsStatus nats_SetVerbose(natsOptions *opts, bool verbose);

#endif /* NATS_OPTS_H_ */
