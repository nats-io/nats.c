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

#ifndef MICRO_ARGS_H_
#define MICRO_ARGS_H_

#include "micro.h"

/**
 * Request unmarshaled as "arguments", a space-separated list of numbers and strings.
 * TODO document the interface.
 */
typedef struct args_s natsMicroserviceArgs;

NATS_EXTERN natsError *
nats_ParseMicroserviceArgs(natsMicroserviceArgs **args, const char *data, int data_len);

NATS_EXTERN int
natsMicroserviceArgs_Count(natsMicroserviceArgs *args);

NATS_EXTERN natsError *
natsMicroserviceArgs_GetInt(int *val, natsMicroserviceArgs *args, int index);

NATS_EXTERN natsError *
natsMicroserviceArgs_GetFloat(long double *val, natsMicroserviceArgs *args, int index);

NATS_EXTERN natsError *
natsMicroserviceArgs_GetString(const char **val, natsMicroserviceArgs *args, int index);

NATS_EXTERN void
natsMicroserviceArgs_Destroy(natsMicroserviceArgs *args);

/** @} */ // end of microserviceGroup

#endif /* MICRO_H_ */
