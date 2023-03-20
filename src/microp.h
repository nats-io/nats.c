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

#ifndef MICROP_H_
#define MICROP_H_

#include "natsp.h"

#define natsMicroserviceQueueGroup "q"

#define natsMicroserviceDefaultEndpointName "default"

struct error_s
{
    natsStatus status;
    int code;
    const char *description;
};

struct microservice_client_s
{
    natsConnection *nc;
};

struct microservice_endpoint_s
{
    // The subject that the endpoint is listening on (may be different from
    // one specified in config).
    char *subject;

    // References to other entities.
    natsMicroservice *m;
    natsMicroserviceEndpointConfig *config;

    // Mutex for starting/stopping the endpoint, and for updating the stats.
    natsMutex *mu;

    // The subscription for the endpoint. If NULL, the endpoint is stopped.
    natsSubscription *sub;

    // Endpoint stats. These are initialized only for running endpoints, and are
    // cleared if the endpoint is stopped.
    natsMicroserviceEndpointStats stats;
};

struct microservice_s
{
    // these are set at initialization time time and do not change.
    natsConnection *nc;
    struct natsMicroserviceConfig *cfg;
    char *id;

    // these are are updated concurrently with access as the service runs, so
    // need to be protected by mutex.
    natsMutex *mu;

    struct microservice_endpoint_s **endpoints;
    int endpoints_len;
    int endpoints_cap;

    int64_t started; // UTC time expressed as number of nanoseconds since epoch.
    bool stopped;
    int refs;
};

struct microservice_request_s
{
    natsMsg *msg;
    struct microservice_endpoint_s *ep;
    void *closure;
};

extern natsError *natsMicroserviceErrorOutOfMemory;
extern natsError *natsMicroserviceErrorInvalidArg;

natsStatus
micro_monitoring_init(natsMicroservice *m);

natsStatus nats_clone_microservice_config(natsMicroserviceConfig **new_cfg, natsMicroserviceConfig *cfg);
void nats_free_cloned_microservice_config(natsMicroserviceConfig *cfg);

natsStatus nats_new_endpoint(natsMicroserviceEndpoint **new_endpoint, natsMicroservice *m, natsMicroserviceEndpointConfig *cfg);
natsStatus nats_clone_endpoint_config(natsMicroserviceEndpointConfig **new_cfg, natsMicroserviceEndpointConfig *cfg);
natsStatus nats_start_endpoint(natsMicroserviceEndpoint *ep);
natsStatus nats_stop_endpoint(natsMicroserviceEndpoint *ep);
void nats_free_cloned_endpoint_config(natsMicroserviceEndpointConfig *cfg);

natsStatus
nats_stop_and_destroy_endpoint(natsMicroserviceEndpoint *ep);

void natsMicroserviceEndpoint_updateLastError(natsMicroserviceEndpoint *ep, natsError *err);

natsStatus
_newMicroserviceRequest(natsMicroserviceRequest **new_request, natsMsg *msg);

void _freeMicroserviceRequest(natsMicroserviceRequest *req);

#endif /* MICROP_H_ */
