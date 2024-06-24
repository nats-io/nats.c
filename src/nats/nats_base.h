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

#ifndef NATS_BASE_H_
#define NATS_BASE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#include "nats_version.h"

/** \def NATS_EXTERN
 *  \brief Needed for shared library.
 *
 *  Based on the platform this is compiled on, it will resolve to
 *  the appropriate instruction so that objects are properly exported
 *  when building the shared library.
 */
#define NATS_EXTERN
#if defined(_WIN32)
#undef NATS_EXTERN
#if defined(nats_EXPORTS)
#define NATS_EXTERN __declspec(dllexport)
#elif defined(nats_IMPORTS)
#define NATS_EXTERN __declspec(dllimport)
#endif
#endif

#include "nats_status.h"

typedef struct __natsMessage natsMessage;
typedef struct __natsOptions natsOptions;
typedef struct __natsConnection natsConnection;

typedef void (*natsOnConnectionEventF)(natsConnection *nc, void *closure);
typedef void (*natsOnMessagePublishedF)(natsConnection *nc, natsMessage *msg, void *closure);

#endif /* NATS_BASE_H_ */
