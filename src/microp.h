// Copyright 2023 The NATS Authors
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

#define MICRO_CALL(__err, __call) \
    if ((__err) == NULL)          \
    {                             \
        __err = (__call);         \
    }

#define MICRO_DO(__err, __block) \
    if ((__err) == NULL)         \
        __block;

#define MICRO_DEFAULT_QUEUE_GROUP "q"

#define MICRO_DEFAULT_ENDPOINT_NAME "default"

struct micro_error_s
{
    bool is_internal;
    struct micro_error_s *cause;
    natsStatus status;
    int code;
    const char *message;
};

struct micro_client_s
{
    natsConnection *nc;
};

struct micro_endpoint_s
{
    // The name and subject that the endpoint is listening on (may be different
    // from one specified in config).
    char *subject;

    // A copy of the config provided to add_endpoint.
    microEndpointConfig *config;

    // Retained/released by the service that owns the endpoint to avoid race
    // conditions.
    microService *m;
    microGroup *group;

    // Monitoring endpoints are different in a few ways. For now, express it as
    // a single flag but consider unbundling:
    //   - use_queue_group: Service endpoints use a queue group, monitoring
    //     endpoints don't.
    //   - forward_response_errors_to_async_handler: Service endpoints handle
    //     respond errors themselves, standard monitoring endpoints don't, so
    //     send the errors to the service async handler, if exists.
    //   - gather_stats: Monitoring endpoints don't need stats.
    //   - include_in_info: Monitoring endpoints are not listed in INFO
    //     responses.
    bool is_monitoring_endpoint;

    // Mutex for starting/stopping the endpoint, and for updating the stats.
    natsMutex *endpoint_mu;
    int refs;

    // The subscription for the endpoint. If NULL, the endpoint is stopped.
    natsSubscription *sub;

    // Endpoint stats. These are initialized only for running endpoints, and are
    // cleared if the endpoint is stopped.
    microEndpointStats stats;

    microEndpoint *next;
};

struct micro_group_s
{
    struct micro_group_config_s *config;
    struct micro_service_s *m;
    struct micro_group_s *next;
};

struct micro_service_s
{
    // these are set at initialization time time and do not change.
    natsConnection *nc;
    microServiceConfig *cfg;
    char id[NUID_BUFFER_LEN + 1];

    // groups are just convenient wrappers to make "prefixed" endpoints with
    // AddEndpoint. They are added at initializaton time, so no need to lock.
    struct micro_group_s *groups;

    // these are are updated concurrently with access as the service runs, so
    // need to be protected by mutex.
    natsMutex *service_mu;
    int refs;

    // a linked list of endpoints.
    struct micro_endpoint_s *first_ep;
    int numEndpoints;

    int64_t started; // UTC time expressed as number of nanoseconds since epoch.
    bool stopped;
};

/**
 * A microservice request.
 *
 * microRequest represents a request received by a microservice endpoint.
 */
struct micro_request_s
{
    /**
     * @brief The NATS message underlying the request.
     */
    natsMsg *Message;

    /**
     * @brief A reference to the service that received the request.
     */
    microService *Service;

    /**
     * @brief A reference to the service that received the request.
     */
    microEndpoint *Endpoint;
};

microError *micro_add_endpoint(microEndpoint **new_ep, microService *m, microGroup *g, microEndpointConfig *cfg, bool is_internal);
microError *micro_clone_endpoint_config(microEndpointConfig **out, microEndpointConfig *cfg);
microError *micro_init_monitoring(microService *m);
microError *micro_is_error_message(natsStatus s, natsMsg *msg);
microError *micro_new_control_subject(char **newSubject, const char *verb, const char *name, const char *id);
microError *micro_new_endpoint(microEndpoint **new_ep, microService *m, microGroup *g, microEndpointConfig *cfg, bool is_internal);
microError *micro_new_request(microRequest **new_request, microService *m, microEndpoint *ep, natsMsg *msg);
microError *micro_start_endpoint(microEndpoint *ep);
microError *micro_stop_endpoint(microEndpoint *ep);

void micro_free_cloned_endpoint_config(microEndpointConfig *cfg);
void micro_free_endpoint(microEndpoint *ep);
void micro_free_request(microRequest *req);
void micro_release_endpoint(microEndpoint *ep);
void micro_release_endpoint_when_unsubscribed(void *closure);
void micro_retain_endpoint(microEndpoint *ep);
void micro_update_last_error(microEndpoint *ep, microError *err);
const char *micro_queue_group_for_endpoint(microEndpoint *ep);

bool micro_is_valid_name(const char *name);
bool micro_is_valid_subject(const char *subject);
bool micro_match_endpoint_subject(const char *ep_subject, const char *actual_subject);

static inline void micro_lock_endpoint(microEndpoint *ep) { natsMutex_Lock(ep->endpoint_mu); }
static inline void micro_unlock_endpoint(microEndpoint *ep) { natsMutex_Unlock(ep->endpoint_mu); }

static inline microError *
micro_strdup(char **ptr, const char *str)
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
