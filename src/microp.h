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

#define natsMicroserviceInfoResponseType "io.nats.micro.v1.info_response"
#define natsMicroservicePingResponseType "io.nats.micro.v1.ping_response"
#define natsMicroserviceStatsResponseType "io.nats.micro.v1.stats_response"
#define natsMicroserviceSchemaResponseType "io.nats.micro.v1.schema_response"

#define natsMicroserviceQueueGroup "q"

#define natsMicroserviceDefaultEndpointName "default"

struct __microserviceClient
{
    natsConnection *nc;
};

struct __microserviceConfig
{
    const char *name;
    const char *version;
    const char *description;
};

struct __microserviceIdentity
{
    const char *name;
    const char *version;
    char *id;
};

typedef struct __microserviceEndpointList
{
    natsMutex *mu;
    int len;
    struct __microserviceEndpoint **endpoints;
} __microserviceEndpointList;

struct __microserviceEndpoint
{
    // The name of the endpoint, uniquely identifies the endpoint. The name ""
    // is reserved for the top level endpoint of the service.
    char *name;

    // Indicates if the endpoint is stopped, or is actively subscribed to a
    // subject.
    bool stopped;

    // The subject that the endpoint is listening on. The subject is also used
    // as the prefix for the children endpoints.
    char *subject;
    natsSubscription *sub;

    // Children endpoints. Their subscriptions are prefixed with the parent's
    // subject. Their stats are summarized in the parent's stats when requested.
    __microserviceEndpointList *children;
    int len_children;

    // Endpoint stats. These are initialized only for running endpoints, and are
    // cleared if the endpoint is stopped.
    natsMutex *stats_mu;
    natsMicroserviceEndpointStats *stats;

    // References to other entities.
    natsMicroservice *m;
    natsMicroserviceEndpointConfig *config;
};

struct __microservice
{
    natsConnection *nc;
    int refs;
    natsMutex *mu;
    struct __microserviceIdentity identity;
    struct natsMicroserviceConfig *cfg;
    bool stopped;
    struct __microserviceEndpoint *root;
};

struct __microserviceRequest
{
    natsMsg *msg;
    struct __microserviceEndpoint *ep;
    char *err;
    void *closure;
};

typedef struct __args_t
{
    void **args;
    int count;
} __args_t;

extern natsMicroserviceError *natsMicroserviceErrorOutOfMemory;
extern natsMicroserviceError *natsMicroserviceErrorInvalidArg;

natsStatus
_initMicroserviceMonitoring(natsMicroservice *m);

natsStatus
_newMicroserviceEndpoint(natsMicroserviceEndpoint **new_endpoint, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg);

natsStatus
natsMicroserviceEndpoint_AddEndpoint(natsMicroserviceEndpoint **new_ep, natsMicroserviceEndpoint *parent, const char *name, natsMicroserviceEndpointConfig *cfg);

natsStatus
natsMicroserviceEndpoint_Start(natsMicroserviceEndpoint *ep);

natsStatus
natsMicroserviceEndpoint_Stop(natsMicroserviceEndpoint *ep);

natsStatus
natsMicroserviceEndpoint_Destroy(natsMicroserviceEndpoint *ep);

natsStatus
_destroyMicroserviceEndpointList(__microserviceEndpointList *list);

natsStatus
_newMicroserviceEndpointList(__microserviceEndpointList **new_list);

natsMicroserviceEndpoint *
_microserviceEndpointList_Find(__microserviceEndpointList *list, const char *name, int *index);

natsStatus
_microserviceEndpointList_Put(__microserviceEndpointList *list, int index, natsMicroserviceEndpoint *ep);

natsStatus
_microserviceEndpointList_Remove(__microserviceEndpointList *list, int index);

natsStatus
_newMicroserviceRequest(natsMicroserviceRequest **new_request, natsMsg *msg);

void _freeMicroserviceRequest(natsMicroserviceRequest *req);

#endif /* MICROP_H_ */
