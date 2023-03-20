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

natsMsg *
natsMicroserviceRequest_GetMsg(natsMicroserviceRequest *req)
{
    return req != NULL ? req->msg : NULL;
}

natsMicroserviceEndpoint *
natsMicroserviceRequest_GetEndpoint(natsMicroserviceRequest *req)
{
    return (req != NULL) ? req->ep : NULL;
}

natsMicroservice *
natsMicroserviceRequest_GetMicroservice(natsMicroserviceRequest *req)
{
    return ((req != NULL) && (req->ep != NULL)) ? req->ep->m : NULL;
}

natsConnection *
natsMicroserviceRequest_GetConnection(natsMicroserviceRequest *req)
{
    return ((req != NULL) && (req->ep != NULL) && (req->ep->m != NULL)) ? req->ep->m->nc : NULL;
}

natsError *
natsMicroserviceRequest_Respond(natsMicroserviceRequest *req, const char *data, int len)
{
    natsStatus s = NATS_OK;
    natsError *err = NULL;

    if ((req == NULL) || (req->msg == NULL) || (req->msg->sub == NULL) || (req->msg->sub->conn == NULL))
        return natsMicroserviceErrorInvalidArg;

    s = natsConnection_Publish(req->msg->sub->conn, natsMsg_GetReply(req->msg), data, len);
    if (s == NATS_OK)
        return NULL;

    err = natsError_Wrapf(nats_NewStatusError(s), "failed to respond to a message");
    natsMicroserviceEndpoint_updateLastError(req->ep, err);
    return err;
}

natsError *
natsMicroserviceRequest_RespondError(natsMicroserviceRequest *req, natsError *err, const char *data, int len)
{
    natsMsg *msg = NULL;
    natsStatus s = NATS_OK;
    char buf[64];

    if ((req == NULL) || (req->msg == NULL) || (req->msg->sub == NULL) || (req->msg->sub->conn == NULL) || (err == NULL))
    {
        return natsMicroserviceErrorInvalidArg;
    }

    IFOK(s, natsMsg_Create(&msg, natsMsg_GetReply(req->msg), NULL, data, len));

    if ((s == NATS_OK) && (err->status != NATS_OK))
    {
        s = natsMsgHeader_Set(msg, NATS_MICROSERVICE_STATUS_HDR, natsStatus_GetText(err->status));
    }
    if (s == NATS_OK)
    {
        s = natsMsgHeader_Set(msg, NATS_MICROSERVICE_ERROR_HDR, err->description);
    }
    if (s == NATS_OK)
    {
        snprintf(buf, sizeof(buf), "%d", err->code);
        s = natsMsgHeader_Set(msg, NATS_MICROSERVICE_ERROR_CODE_HDR, buf);
    }
    IFOK(s, natsConnection_PublishMsg(req->msg->sub->conn, msg));

    natsMsg_Destroy(msg);
    natsMicroserviceEndpoint_updateLastError(req->ep, err);

    if (s == NATS_OK)
    {
        return NULL;
    }
    else
    {
        err = natsError_Wrapf(nats_NewStatusError(s), "failed to respond to a message with an error");
        return err;
    }
}

void natsMicroserviceRequest_Error(natsMicroserviceRequest *req, natsError *err)
{
    if (err == NULL)
        return;

    natsMicroserviceRequest_RespondError(req, err, NULL, 0);
    natsError_Destroy(err);
}

void _freeMicroserviceRequest(natsMicroserviceRequest *req)
{
    NATS_FREE(req);
}

natsStatus
_newMicroserviceRequest(natsMicroserviceRequest **new_request, natsMsg *msg)
{
    natsStatus s = NATS_OK;
    natsMicroserviceRequest *req = NULL;

    req = (natsMicroserviceRequest *)NATS_CALLOC(1, sizeof(natsMicroserviceRequest));
    s = (req != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        req->msg = msg;
        *new_request = req;
        return NATS_OK;
    }

    _freeMicroserviceRequest(req);
    return NATS_UPDATE_ERR_STACK(s);
}
