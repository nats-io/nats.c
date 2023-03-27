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

#ifndef MICRO_H_
#define MICRO_H_

#include "nats.h"

#define MICRO_API_PREFIX "$SRV"

#define MICRO_INFO_RESPONSE_TYPE "io.nats.micro.v1.info_response"
#define MICRO_PING_RESPONSE_TYPE "io.nats.micro.v1.ping_response"
#define MICRO_STATS_RESPONSE_TYPE "io.nats.micro.v1.stats_response"
#define MICRO_STATS_SCHEMA_TYPE "io.nats.micro.v1.schema_response"

#define MICRO_PING_VERB "PING"
#define MICRO_STATS_VERB "STATS"
#define MICRO_INFO_VERB "INFO"
#define MICRO_SCHEMA_VERB "SCHEMA"

#define MICRO_STATUS_HDR "Nats-Status"
#define MICRO_ERROR_HDR "Nats-Service-Error"
#define MICRO_ERROR_CODE_HDR "Nats-Service-Error-Code"

#define MICRO_CALL(__err, __call) \
    if ((__err) == NULL)          \
    {                             \
        __err = (__call);         \
    }

#define MICRO_CALL_IF(__err, __cond, __call) \
    if (((__err) == NULL) && (__cond))       \
    {                                        \
        __err = (__call);                    \
    }

#define MICRO_DO(__err, __block) \
    if ((__err) == NULL)         \
        __block;

/**
 * The Microservice request object.
 */
typedef struct micro_request_s microRequest;

/**
 * The Microservice object. Create and start with #microService_Create.
 */
typedef struct micro_service_s microService;

/**
 * The Microservice configuration object. The service holds on to it, so it must
 * be constant for the lifetime of the service.
 */
typedef struct microServiceConfig
{
    const char *name;
    const char *version;
    const char *description;
    struct microEndpointConfig *endpoint;
} microServiceConfig;

typedef struct microServiceInfo
{
    const char *type;
    const char *name;
    const char *version;
    const char *description;
    const char *id;
    const char **subjects;
    int subjects_len;
} microServiceInfo;

/**
 * The Microservice endpoint object.
 * TODO document the interface.
 */
typedef struct micro_endpoint_s microEndpoint;

/** \brief Callback used to deliver messages to a microservice.
 *
 * This is the callback that one provides when creating a microservice endpoint.
 * The library will invoke this callback for each message arriving to the
 * specified subject.
 *
 * @see microService_AddEndpoint()
 */
typedef void (*microRequestHandler)(microService *m, microRequest *req);

/**
 * The Microservice endpoint configuration object.
 */
typedef struct microEndpointConfig
{
    const char *name;
    microRequestHandler handler;
    void *closure;
    const char *subject;
    struct microSchema *schema;
} microEndpointConfig;

/**
 * The Microservice endpoint stats struct.
 */
typedef struct microEndpointStats
{
    const char *name;
    const char *subject;
    int64_t num_requests;
    int64_t num_errors;
    int64_t processing_time_s;
    int64_t processing_time_ns;
    int64_t average_processing_time_ns;
    char last_error_string[2048];
} microEndpointStats;

/**
 * The Microservice stats struct.
 */
typedef struct microServiceStats
{
    const char *type;
    const char *name;
    const char *version;
    const char *id;
    int64_t started;
    int endpoints_len;
    microEndpointStats *endpoints;
} microServiceStats;

/**
 * The Microservice endpoint schema object.
 */
typedef struct microSchema
{
    const char *request;
    const char *response;
} microSchema;

/**
 * The Microservice client. Initialize with #microClient_Create.
 */
typedef struct micro_client_s microClient;

/**
 * The Microservice configuration object.
 */
typedef struct microClientConfig microClientConfig;

typedef struct micro_error_s microError;

//
// microService methods.

NATS_EXTERN microError *microService_AddEndpoint(microEndpoint **new_endpoint, microService *m, microEndpointConfig *cfg);
NATS_EXTERN microError *microService_Create(microService **new_microservice, natsConnection *nc, microServiceConfig *cfg);
NATS_EXTERN natsConnection *microService_GetConnection(microService *m);
NATS_EXTERN microError *microService_GetInfo(microServiceInfo **new_info, microService *m);
NATS_EXTERN microError *natsMicroservice_GetStats(microServiceStats **new_stats, microService *m);
NATS_EXTERN bool microService_IsStopped(microService *m);
NATS_EXTERN microError *microService_Release(microService *m);
NATS_EXTERN microError *microService_Run(microService *m);
NATS_EXTERN microError *microService_Stop(microService *m);

//
// microRequest methods.

NATS_EXTERN natsConnection *microRequest_GetConnection(microRequest *req);
NATS_EXTERN microEndpoint *microRequest_GetEndpoint(microRequest *req);
NATS_EXTERN natsMsg *microRequest_GetMsg(microRequest *req);
NATS_EXTERN microService *microRequest_GetService(microRequest *req);
NATS_EXTERN microError *microRequest_Respond(microRequest *req, microError **err_will_free, const char *data, size_t len);

#define microRequest_AddHeader(req, key, value) natsMsgHeader_Add(microRequest_GetMsg(req), (key), (value))
#define microRequest_DeleteHeader(req, key) natsMsgHeader_Delete(microRequest_GetMsg(req), (key))
#define microRequest_GetData(req) natsMsg_GetData(microRequest_GetMsg(req))
#define microRequest_GetDataLength(req) natsMsg_GetDataLength(microRequest_GetMsg(req))
#define microRequest_GetHeader(req, key, value) natsMsgHeader_Get(microRequest_GetMsg(req), (key), (value))
#define microRequest_GetHeaderKeys(req, key, keys, count) natsMsgHeader_Keys(microRequest_GetMsg(req), (key), (keys), (count))
#define microRequest_GetHeaderValues(req, key, values, count) natsMsgHeader_Values(microRequest_GetMsg(req), (key), (values), (count))
#define microRequest_GetReply(req) natsMsg_GetReply(microRequest_GetMsg(req))
#define microRequest_GetSequence(req) natsMsg_GetSequence(microRequest_GetMsg(req))
#define microRequest_GetSubject(req) natsMsg_GetSubject(microRequest_GetMsg(req))
#define microRequest_GetTime(req) natsMsg_GetTime(microRequest_GetMsg(req))
#define microRequest_SetHeader(req, key, value) natsMsgHeader_Set(microRequest_GetMsg(req), (key), (value))

//
// microError methods.

NATS_EXTERN microError *micro_NewErrorf(int code, const char *format, ...);
NATS_EXTERN natsStatus microError_Code(microError *err);
NATS_EXTERN void microError_Destroy(microError *err);
NATS_EXTERN microError *microError_FromResponse(natsStatus s, natsMsg *msg);
NATS_EXTERN microError *microError_FromStatus(natsStatus s);
NATS_EXTERN natsStatus microError_Status(microError *err);
NATS_EXTERN const char *microError_String(microError *err, char *buf, int len);
NATS_EXTERN microError *microError_Wrapf(microError *err, const char *format, ...);

//
// natsClient methods.

microError *microClient_Create(microClient **new_client, natsConnection *nc, microClientConfig *cfg);
void microClient_Destroy(microClient *client);
microError *microClient_DoRequest(microClient *client, natsMsg **reply, const char *subject, const char *data, int data_len);

//
// microServiceInfo methods.

void microServiceInfo_Destroy(microServiceInfo *info);

//
// microServiceStats methods.

void natsMicroserviceStats_Destroy(microServiceStats *stats);

/** @} */ // end of microserviceGroup

#endif /* MICRO_H_ */
