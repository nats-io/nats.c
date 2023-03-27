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

#include "micro.h"
#include "microp.h"
#include "mem.h"

microError *
microRequest_Respond(microRequest *req, microError **err_will_free, const char *data, size_t len)
{
    natsMsg *msg = NULL;
    microError *err = NULL;
    natsStatus s = NATS_OK;
    char buf[64];

    if ((req == NULL) || (req->message == NULL) || (req->message->sub == NULL) || (req->message->sub->conn == NULL))
    {
        return micro_ErrorInvalidArg;
    }

    IFOK(s, natsMsg_Create(&msg, natsMsg_GetReply(req->message), NULL, data, len));
    if ((s == NATS_OK) && (err_will_free != NULL) && (*err_will_free != NULL))
    {
        err = *err_will_free;
        micro_update_last_error(req->endpoint, err);
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
    IFOK(s, natsConnection_PublishMsg(req->message->sub->conn, msg));

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
    return ((req != NULL) && (req->service != NULL)) ? req->service->nc : NULL;
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
    return (req != NULL) ? req->endpoint : NULL;
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
    return (req != NULL) ? req->message : NULL;
}

const char *
microRequest_GetReply(microRequest *req)
{
    return natsMsg_GetReply(microRequest_GetMsg(req));
}

uint64_t microRequest_GetSequence(microRequest *req)
{
    return natsMsg_GetSequence(microRequest_GetMsg(req));
}

const char *microRequest_GetSubject(microRequest *req)
{
    return natsMsg_GetSubject(microRequest_GetMsg(req));
}

void *microRequest_GetServiceState(microRequest *req)
{
    if ((req == NULL) || (req->service == NULL) || (req->service->cfg == NULL))
    {
        return NULL;
    }
    return req->service->cfg->state;
}

void *microRequest_GetEndpointState(microRequest *req)
{
    if ((req == NULL) || (req->endpoint == NULL) || (req->endpoint->config == NULL))
    {
        return NULL;
    }
    return req->endpoint->config->state;
}

int64_t microRequest_GetTime(microRequest *req)
{
    return natsMsg_GetTime(microRequest_GetMsg(req));
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
    return (req != NULL) ? req->service : NULL;
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

    req->message = msg;
    req->service = m;
    req->endpoint = ep;
    *new_request = req;
    return NULL;
}
