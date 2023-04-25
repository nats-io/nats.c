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
#include "mem.h"

#define MICRO_QUEUE_GROUP "q"

#define MICRO_DEFAULT_ENDPOINT_NAME "default"

// 4 verbs: INFO, STATS, PING, SCHEMA;
// 3 subjects for each verb.
#define MICRO_MONITORING_SUBS_CAP (4 * 3)

struct micro_error_s
{
    natsStatus status;
    int code;
    const char *description;
};

struct micro_client_s
{
    natsConnection *nc;
};

struct micro_endpoint_s
{
    // The subject that the endpoint is listening on (may be different from
    // one specified in config).
    char *subject;

    // References to other entities.
    microService *m;
    microEndpointConfig *config;

    // Mutex for starting/stopping the endpoint, and for updating the stats.
    natsMutex *mu;

    // The subscription for the endpoint. If NULL, the endpoint is stopped.
    natsSubscription *sub;

    // Endpoint stats. These are initialized only for running endpoints, and are
    // cleared if the endpoint is stopped.
    microEndpointStats stats;
};

struct micro_group_s
{
    struct micro_service_s *m;
    struct micro_group_s *next;
    char prefix[];
};

struct micro_service_s
{
    // these are set at initialization time time and do not change.
    natsConnection *nc;
    struct microServiceConfig *cfg;
    char id[NUID_BUFFER_LEN + 1];

    // groups are just convenient wrappers to make "prefixed" endpoints with
    // AddEndpoint. They are added at initializaton time, so no need to lock.
    struct micro_group_s *groups;

    // these are are updated concurrently with access as the service runs, so
    // need to be protected by mutex.
    natsMutex *mu;

    int refs;

    struct micro_endpoint_s **endpoints;
    int endpoints_len;
    int endpoints_cap;

    natsSubscription **monitoring_subs;
    int monitoring_subs_len;

    natsConnectionHandler prev_on_connection_closed;
    void *prev_on_connection_closed_closure;
    natsConnectionHandler prev_on_connection_disconnected;
    void *prev_on_connection_disconnected_closure;
    natsErrHandler prev_on_error;
    void *prev_on_error_closure;

    int64_t started; // UTC time expressed as number of nanoseconds since epoch.
    bool is_stopped;
    bool is_stopping;
};

extern microError *micro_ErrorOutOfMemory;
extern microError *micro_ErrorInvalidArg;

microError *micro_clone_endpoint_config(microEndpointConfig **new_cfg, microEndpointConfig *cfg);
microError *micro_clone_service_config(microServiceConfig **new_cfg, microServiceConfig *cfg);
void micro_destroy_cloned_endpoint_config(microEndpointConfig *cfg);
void micro_destroy_cloned_service_config(microServiceConfig *cfg);
void micro_destroy_request(microRequest *req);
microError *micro_init_monitoring(microService *m);
bool micro_match_endpoint_subject(const char *ep_subject, const char *actual_subject);
microError *micro_new_endpoint(microEndpoint **new_endpoint, microService *m, const char *prefix, microEndpointConfig *cfg);
microError *micro_new_request(microRequest **new_request, microService *m, microEndpoint *ep, natsMsg *msg);
microError *micro_start_endpoint(microEndpoint *ep);
microError *micro_stop_endpoint(microEndpoint *ep);
microError *micro_stop_and_destroy_endpoint(microEndpoint *ep);
void micro_update_last_error(microEndpoint *ep, microError *err);
microError *micro_new_control_subject(char **newSubject, const char *verb, const char *name, const char *id);


static inline microError *micro_strdup(char **ptr, const char *str)
{
    // Make a strdup(NULL) be a no-op, so we don't have to check for NULL
    // everywhere.
    if (str == NULL)
    {
        *ptr = NULL;
        return NULL;
    }
    *ptr = NATS_STRDUP(str);
    if (*ptr == NULL)
        return micro_ErrorOutOfMemory;
    return NULL;
}

#endif /* MICROP_H_ */
