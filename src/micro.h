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

#define natsMicroserviceAPIPrefix "$SRV"

#define natsMicroserviceInfoResponseType "io.nats.micro.v1.info_response"
#define natsMicroservicePingResponseType "io.nats.micro.v1.ping_response"
#define natsMicroserviceStatsResponseType "io.nats.micro.v1.stats_response"
#define natsMicroserviceSchemaResponseType "io.nats.micro.v1.schema_response"

#define natsMicroservicePingVerb "PING"
#define natsMicroserviceStatsVerb "STATS"
#define natsMicroserviceInfoVerb "INFO"
#define natsMicroserviceSchemaVerb "SCHEMA"

#define NATS_MICROSERVICE_STATUS_HDR "Nats-Status"
#define NATS_MICROSERVICE_ERROR_HDR "Nats-Service-Error"
#define NATS_MICROSERVICE_ERROR_CODE_HDR "Nats-Service-Error-Code"

/**
 * The Microservice request object.
 */
typedef struct microservice_request_s natsMicroserviceRequest;

/**
 * The Microservice object. Create and start with #nats_AddMicroservice.
 */
typedef struct microservice_s natsMicroservice;

/**
 * The Microservice configuration object. The service holds on to it, so it must
 * be constant for the lifetime of the service.
 */
typedef struct natsMicroserviceConfig
{
    const char *name;
    const char *version;
    const char *description;
    struct natsMicroserviceEndpointConfig *endpoint;
} natsMicroserviceConfig;

typedef struct natsMicroserviceInfo
{
    const char *type;
    const char *name;
    const char *version;
    const char *description;
    const char *id;
    const char **subjects;
    int subjects_len;
} natsMicroserviceInfo;

/**
 * The Microservice endpoint object.
 * TODO document the interface.
 */
typedef struct microservice_endpoint_s natsMicroserviceEndpoint;

/** \brief Callback used to deliver messages to a microservice.
 *
 * This is the callback that one provides when creating a microservice endpoint.
 * The library will invoke this callback for each message arriving to the
 * specified subject.
 *
 * @see natsMicroservice_AddEndpoint()
 */
typedef void (*natsMicroserviceRequestHandler)(natsMicroservice *m, natsMicroserviceRequest *req);

/**
 * The Microservice endpoint configuration object.
 */
typedef struct natsMicroserviceEndpointConfig
{
    const char *name;
    natsMicroserviceRequestHandler handler;
    void *closure;
    const char *subject;
    struct natsMicroserviceSchema *schema;
} natsMicroserviceEndpointConfig;

/**
 * The Microservice endpoint stats struct.
 */
typedef struct natsMicroserviceEndpointStats
{
    const char *name;
    const char *subject;
    int64_t num_requests;
    int64_t num_errors;
    int64_t processing_time_s;
    int64_t processing_time_ns;
    int64_t average_processing_time_ns;
    char last_error_string[2048];
} natsMicroserviceEndpointStats;

/**
 * The Microservice stats struct.
 */
typedef struct natsMicroserviceStats
{
    const char *type;
    const char *name;
    const char *version;
    const char *id;
    int64_t started;
    int endpoints_len;
    natsMicroserviceEndpointStats *endpoints;
} natsMicroserviceStats;

/**
 * The Microservice endpoint schema object.
 */
typedef struct natsMicroserviceSchema
{
    const char *request;
    const char *response;
} natsMicroserviceSchema;

/**
 * The Microservice client. Initialize with #nats_NewMicroserviceClient.
 */
typedef struct microservice_client_s natsMicroserviceClient;

/**
 * The Microservice configuration object.
 */
typedef struct natsMicroserviceClientConfig natsMicroserviceClientConfig;

typedef struct error_s natsError;

//
// natsMicroservice methods.

NATS_EXTERN natsError *nats_AddMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg);
NATS_EXTERN natsError *natsMicroservice_AddEndpoint(natsMicroserviceEndpoint **new_endpoint, natsMicroservice *m, natsMicroserviceEndpointConfig *cfg);
NATS_EXTERN natsConnection *natsMicroservice_GetConnection(natsMicroservice *m);
NATS_EXTERN bool natsMicroservice_IsStopped(natsMicroservice *m);
NATS_EXTERN natsError *natsMicroservice_Release(natsMicroservice *m);
NATS_EXTERN natsError *natsMicroservice_Run(natsMicroservice *m);
NATS_EXTERN natsError *natsMicroservice_Stop(natsMicroservice *m);

//
// natsMicroserviceRequest methods.

NATS_EXTERN void natsMicroserviceRequest_Error(natsMicroserviceRequest *req, natsError *err);
NATS_EXTERN natsConnection *natsMicroserviceRequest_GetConnection(natsMicroserviceRequest *req);
NATS_EXTERN natsMicroserviceEndpoint *natsMicroserviceRequest_GetEndpoint(natsMicroserviceRequest *req);
NATS_EXTERN natsMicroservice *natsMicroserviceRequest_GetMicroservice(natsMicroserviceRequest *req);
NATS_EXTERN natsMsg *natsMicroserviceRequest_GetMsg(natsMicroserviceRequest *req);
NATS_EXTERN natsError *natsMicroserviceRequest_Respond(natsMicroserviceRequest *req, const char *data, int len);
NATS_EXTERN natsError *natsMicroserviceRequest_RespondError(natsMicroserviceRequest *req, natsError *err, const char *data, int len);

#define natsMicroserviceRequestHeader_Set(req, key, value) \
    natsMsgHeader_Set(natsMicroserviceRequest_GetMsg(req), (key), (value))
#define natsMicroserviceRequestHeader_Add(req, key, value) \
    natsMsgHeader_Add(natsMicroserviceRequest_GetMsg(req), (key), (value))
#define natsMicroserviceRequestHeader_Get(req, key, value) \
    natsMsgHeader_Get(natsMicroserviceRequest_GetMsg(req), (key), (value))
#define natsMicroserviceRequestHeader_Values(req, key, values, count) \
    natsMsgHeader_Values(natsMicroserviceRequest_GetMsg(req), (key), (values), (count))
#define natsMicroserviceRequestHeader_Keys(req, key, keys, count) \
    natsMsgHeader_Keys(natsMicroserviceRequest_GetMsg(req), (key), (keys), (count))
#define natsMicroserviceRequestHeader_Delete(req, key) \
    natsMsgHeader_Delete(natsMicroserviceRequest_GetMsg(req), (key))

#define natsMicroserviceRequest_GetSubject(req) \
    natsMsg_GetSubject(natsMicroserviceRequest_GetMsg(req))
#define natsMicroserviceRequest_GetReply(req) \
    natsMsg_GetReply(natsMicroserviceRequest_GetMsg(req))
#define natsMicroserviceRequest_GetData(req) \
    natsMsg_GetData(natsMicroserviceRequest_GetMsg(req))
#define natsMicroserviceRequest_GetDataLength(req) \
    natsMsg_GetDataLength(natsMicroserviceRequest_GetMsg(req))
#define natsMicroserviceRequest_GetSequence(req) \
    natsMsg_GetSequence(natsMicroserviceRequest_GetMsg(req))
#define natsMicroserviceRequest_GetTime(req) \
    natsMsg_GetTime(natsMicroserviceRequest_GetMsg(req))

//
// natsError methods.

NATS_EXTERN natsError *nats_Errorf(int code, const char *format, ...);
NATS_EXTERN natsError *nats_IsErrorResponse(natsStatus s, natsMsg *msg);
NATS_EXTERN natsError *nats_NewError(int code, const char *desc);
NATS_EXTERN natsError *nats_NewStatusError(natsStatus s);
NATS_EXTERN void natsError_Destroy(natsError *err);
NATS_EXTERN natsStatus natsError_ErrorCode(natsError *err);
NATS_EXTERN natsStatus natsError_StatusCode(natsError *err);
NATS_EXTERN const char *natsError_String(natsError *err, char *buf, int len);
NATS_EXTERN natsError *natsError_Wrapf(natsError *err, const char *format, ...);

//
// natsClient methods.

natsError *nats_NewMicroserviceClient(natsMicroserviceClient **new_client, natsConnection *nc, natsMicroserviceClientConfig *cfg);
void natsMicroserviceClient_Destroy(natsMicroserviceClient *client);
natsError *
natsMicroserviceClient_DoRequest(natsMicroserviceClient *client, natsMsg **reply, const char *subject, const char *data, int data_len);

//
// natsMicroserviceInfo methods.

natsStatus natsMicroservice_Info(natsMicroserviceInfo **new_info, natsMicroservice *m);
void natsMicroserviceInfo_Destroy(natsMicroserviceInfo *info);

//
// natsMicroserviceStats methods.

natsStatus natsMicroservice_Stats(natsMicroserviceStats **new_stats, natsMicroservice *m);
void natsMicroserviceStats_Destroy(natsMicroserviceStats *stats);

/** @} */ // end of microserviceGroup

#endif /* MICRO_H_ */
