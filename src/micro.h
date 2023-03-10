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

#define NATS_MICROSERVICE_STATUS_HDR "Nats-Status"
#define NATS_MICROSERVICE_ERROR_HDR "Nats-Service-Error"
#define NATS_MICROSERVICE_ERROR_CODE_HDR "Nats-Service-Error-Code"

enum natsMicroserviceVerb
{
    natsMicroserviceVerbPing = 0,
    natsMicroserviceVerbStats,
    natsMicroserviceVerbInfo,
    natsMicroserviceVerbSchema,
    natsMicroserviceVerbMax
};

typedef enum natsMicroserviceVerb natsMicroserviceVerb;

/**
 * The Microservice error. Note that code and description do not have a
 * well-defined lifespan and should be copied if needed to be used later.
 */
typedef struct natsMicroserviceError
{
    natsStatus status;
    int code;
    const char *description;

    const char *(*String)(struct natsMicroserviceError *err, char *buf, int size);
} natsMicroserviceError;

/**
 * The Microservice request object.
 */
typedef struct __microserviceRequest natsMicroserviceRequest;

/**
 * The Microservice object. Create and start with #nats_AddMicroservice.
 */
typedef struct __microservice natsMicroservice;

/**
 * The Microservice configuration object.
 */
typedef struct natsMicroserviceConfig
{
    const char *name;
    const char *version;
    const char *description;
    struct natsMicroserviceEndpointConfig *endpoint;
} natsMicroserviceConfig;

/**
 * The Microservice endpoint object.
 * TODO document the interface.
 */
typedef struct __microserviceEndpoint natsMicroserviceEndpoint;

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
    const char *subject;
    natsMicroserviceRequestHandler handler;
    void *closure;
    struct natsMicroserviceSchema *schema;
} natsMicroserviceEndpointConfig;

/**
 * The Microservice endpoint stats struct.
 */
typedef struct natsMicroserviceEndpointStats
{
    const char *name;
    const char *subject;
    int num_requests;
    int num_errors;
    char *last_error;
    int processing_time_ms;
    int average_processing_time_ms;
    bool stopped;
} natsMicroserviceEndpointStats;

/**
 * The Microservice endpoint schema object.
 */
typedef struct natsMicroserviceSchema
{
    const char *request;
    const char *response;
} natsMicroserviceSchema;

/**
 * Request unmarshaled as "arguments", a space-separated list of numbers and strings.
 * TODO document the interface.
 */
typedef struct __args_t natsArgs;

/**
 * The Microservice client. Initialize with #nats_NewMicroserviceClient.
 */
typedef struct __microserviceClient natsMicroserviceClient;

/**
 * The Microservice configuration object. Initialize with #natsMicroserviceConfig_Init.
 */
typedef struct natsMicroserviceClientConfig
{
    // TBD in the future.
    int dummy;
} natsMicroserviceClientConfig;

/** \defgroup microserviceGroup Microservice support
 *
 * A simple NATS-based microservice implementation framework.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \brief Makes a new error.
 *
 * @param new_microservice the location where to store the newly created
 * #natsMicroservice object.
 * @param nc the pointer to the #natsCOnnection object on which the service will listen on.
 * @param cfg the pointer to the #natsMicroserviceConfig configuration
 * information used to create the #natsMicroservice object.
 */
NATS_EXTERN natsMicroserviceError *
nats_NewMicroserviceError(natsStatus s, int code, const char *desc);

NATS_EXTERN void
natsMicroserviceError_Destroy(natsMicroserviceError *err);

/** \brief Adds a microservice with a given configuration.
 *
 * Adds a microservice with a given configuration.
 *
 * \note The return #natsMicroservice object needs to be destroyed using
 * #natsMicroservice_Destroy when no longer needed to free allocated memory.
 *
 * @param new_microservice the location where to store the newly created
 * #natsMicroservice object.
 * @param nc the pointer to the #natsCOnnection object on which the service will listen on.
 * @param cfg the pointer to the #natsMicroserviceConfig configuration
 * information used to create the #natsMicroservice object.
 */
NATS_EXTERN natsMicroserviceError *
nats_AddMicroservice(natsMicroservice **new_microservice, natsConnection *nc, natsMicroserviceConfig *cfg);

/** \brief Waits for the microservice to stop.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroservice_Run(natsMicroservice *m);

/** \brief Stops a running microservice.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroservice_Stop(natsMicroservice *m);

/** \brief Checks if a microservice is stopped.
 */
NATS_EXTERN bool
natsMicroservice_IsStopped(natsMicroservice *m);

/** \brief Destroys a microservice object.
 *
 * Destroys a microservice object; frees all memory. The service must be stopped
 * first, this function does not check if it is.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroservice_Destroy(natsMicroservice *m);

/** \brief Adds (and starts) a microservice endpoint.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroservice_AddEndpoint(natsMicroserviceEndpoint **new_endpoint, natsMicroservice *m, const char *name, natsMicroserviceEndpointConfig *cfg);

/** \brief Returns the microservice's NATS connection.
 */
NATS_EXTERN natsConnection *
natsMicroservice_GetConnection(natsMicroservice *m);

/** \brief Responds to a microservice request.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroserviceRequest_Respond(natsMicroserviceRequest *req, const char *data, int len);

/** \brief Responds to a microservice request with an error, does NOT free the
 * error.
 */
NATS_EXTERN natsMicroserviceError *
natsMicroserviceRequest_RespondError(natsMicroserviceRequest *req, natsMicroserviceError *err, const char *data, int len);

/** \brief A convenience method to respond to a microservice request with a
 * simple error (no data), and free the error.
 */
NATS_EXTERN void
natsMicroserviceRequest_Error(natsMicroserviceRequest *req, natsMicroserviceError **err);

/** \brief Returns the original NATS message underlying the request.
 */
NATS_EXTERN natsMsg *
natsMicroserviceRequest_GetMsg(natsMicroserviceRequest *req);

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

NATS_EXTERN natsMicroserviceEndpoint *
natsMicroserviceRequest_GetEndpoint(natsMicroserviceRequest *req);

NATS_EXTERN natsMicroservice *
natsMicroserviceRequest_GetMicroservice(natsMicroserviceRequest *req);

NATS_EXTERN natsConnection *
natsMicroserviceRequest_GetConnection(natsMicroserviceRequest *req);

NATS_EXTERN natsMicroserviceError *
nats_MicroserviceErrorFromMsg(natsStatus s, natsMsg *msg);

NATS_EXTERN natsMicroserviceError *
natsParseAsArgs(natsArgs **args, const char *data, int data_len);

NATS_EXTERN int
natsArgs_Count(natsArgs *args);

NATS_EXTERN natsMicroserviceError *
natsArgs_GetInt(int *val, natsArgs *args, int index);

NATS_EXTERN natsMicroserviceError *
natsArgs_GetFloat(long double *val, natsArgs *args, int index);

NATS_EXTERN natsMicroserviceError *
natsArgs_GetString(const char **val, natsArgs *args, int index);

NATS_EXTERN void
natsArgs_Destroy(natsArgs *args);

natsMicroserviceError * 
nats_NewMicroserviceClient(natsMicroserviceClient **new_client, natsConnection *nc, natsMicroserviceClientConfig *cfg);

void 
natsMicroserviceClient_Destroy(natsMicroserviceClient *client);

natsMicroserviceError *
natsMicroserviceClient_DoRequest(natsMicroserviceClient *client, natsMsg **reply, const char *subject, const char *data, int data_len);

/** @} */ // end of microserviceGroup

#endif /* MICRO_H_ */
