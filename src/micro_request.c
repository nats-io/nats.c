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

#include "microp.h"
#include "mem.h"

microError *
microRequest_Respond(microRequest *req, microError **err_will_free, const char *data, size_t len)
{
    natsMsg *msg = NULL;
    microError *err = NULL;
    natsStatus s = NATS_OK;
    char buf[64];

    if ((req == NULL) || (req->Message == NULL) || (req->Message->sub == NULL) || (req->Message->sub->conn == NULL))
    {
        return micro_ErrorInvalidArg;
    }

    IFOK(s, natsMsg_Create(&msg, natsMsg_GetReply(req->Message), NULL, data, len));
    if ((s == NATS_OK) && (err_will_free != NULL) && (*err_will_free != NULL))
    {
        err = *err_will_free;
        micro_update_last_error(req->Endpoint, err);
        if ((s == NATS_OK) && (err->status != NATS_OK))
        {
            s = natsMsgHeader_Set(msg, MICRO_STATUS_HDR, natsStatus_GetText(err->status));
        }
        if (s == NATS_OK)
        {
            s = natsMsgHeader_Set(msg, MICRO_ERROR_HDR, err->description);
        }
        if (s == NATS_OK)
        {
            snprintf(buf, sizeof(buf), "%d", err->code);
            s = natsMsgHeader_Set(msg, MICRO_ERROR_CODE_HDR, buf);
        }
    }
    IFOK(s, natsConnection_PublishMsg(req->Message->sub->conn, msg));

    natsMsg_Destroy(msg);
    if (err_will_free != NULL)
    {
        microError_Destroy(*err_will_free);
        *err_will_free = NULL;
    }
    return microError_Wrapf(
        micro_ErrorFromStatus(s),
        "failed to respond to a message with an error");
}

microError *
microRequest_AddHeader(microRequest *req, const char *key, const char *value)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Add(microRequest_GetMsg(req), key, value));
}

microError *
microRequest_DeleteHeader(microRequest *req, const char *key)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Delete(microRequest_GetMsg(req), key));
}

natsConnection *
microRequest_GetConnection(microRequest *req)
{
    return ((req != NULL) && (req->Service != NULL)) ? req->Service->nc : NULL;
}

const char *
microRequest_GetData(microRequest *req)
{
    return natsMsg_GetData(microRequest_GetMsg(req));
}

int microRequest_GetDataLength(microRequest *req)
{
    return natsMsg_GetDataLength(microRequest_GetMsg(req));
}

microEndpoint *
microRequest_GetEndpoint(microRequest *req)
{
    return (req != NULL) ? req->Endpoint : NULL;
}

microError *
microRequest_GetHeaderKeys(microRequest *req, const char ***keys, int *count)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Keys(microRequest_GetMsg(req), keys, count));
}

microError *
microRequest_GetHeaderValue(microRequest *req, const char *key, const char **value)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Get(microRequest_GetMsg(req), key, value));
}

microError *
microRequest_GetHeaderValues(microRequest *req, const char *key, const char ***values, int *count)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Values(microRequest_GetMsg(req), key, values, count));
}

natsMsg *
microRequest_GetMsg(microRequest *req)
{
    return (req != NULL) ? req->Message : NULL;
}

const char *
microRequest_GetReply(microRequest *req)
{
    return natsMsg_GetReply(microRequest_GetMsg(req));
}

const char *microRequest_GetSubject(microRequest *req)
{
    return natsMsg_GetSubject(microRequest_GetMsg(req));
}

void *microRequest_GetServiceState(microRequest *req)
{
    if ((req == NULL) || (req->Service == NULL) || (req->Service->cfg == NULL))
    {
        return NULL;
    }
    return req->Service->cfg->State;
}

void *microRequest_GetEndpointState(microRequest *req)
{
    if ((req == NULL) || (req->Endpoint == NULL) || (req->Endpoint->config == NULL))
    {
        return NULL;
    }
    return req->Endpoint->config->State;
}

microError *
microRequest_SetHeader(microRequest *req, const char *key, const char *value)
{
    return micro_ErrorFromStatus(
        natsMsgHeader_Set(microRequest_GetMsg(req), key, value));
}

microService *
microRequest_GetService(microRequest *req)
{
    return (req != NULL) ? req->Service : NULL;
}

void micro_destroy_request(microRequest *req)
{
    NATS_FREE(req);
}

microError *
micro_new_request(microRequest **new_request, microService *m, microEndpoint *ep, natsMsg *msg)
{
    microRequest *req = NULL;

    // endpoint is optional, service and message references are required.
    if ((new_request == NULL) || (m == NULL) || (msg == NULL))
        return micro_ErrorInvalidArg;

    req = (microRequest *)NATS_CALLOC(1, sizeof(microRequest));
    if (req == NULL)
        return micro_ErrorOutOfMemory;

    req->Message = msg;
    req->Service = m;
    req->Endpoint = ep;
    *new_request = req;
    return NULL;
}
