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
microRequest_GetMsg(microRequest *req)
{
    return req != NULL ? req->msg : NULL;
}

microEndpoint *
microRequest_GetEndpoint(microRequest *req)
{
    return (req != NULL) ? req->ep : NULL;
}

microService *
microRequest_GetService(microRequest *req)
{
    return ((req != NULL) && (req->ep != NULL)) ? req->ep->m : NULL;
}

natsConnection *
microRequest_GetConnection(microRequest *req)
{
    return ((req != NULL) && (req->ep != NULL) && (req->ep->m != NULL)) ? req->ep->m->nc : NULL;
}

microError *
microRequest_Respond(microRequest *req, microError **err_will_free, const char *data, size_t len)
{
    natsMsg *msg = NULL;
    microError *err = NULL;
    natsStatus s = NATS_OK;
    char buf[64];

    if ((req == NULL) || (req->msg == NULL) || (req->msg->sub == NULL) || (req->msg->sub->conn == NULL))
    {
        return micro_ErrorInvalidArg;
    }

    IFOK(s, natsMsg_Create(&msg, natsMsg_GetReply(req->msg), NULL, data, len));

    if ((err_will_free != NULL) && (*err_will_free != NULL))
    {
        err = *err_will_free;
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
        micro_update_last_error(req->ep, err);
    }
    IFOK(s, natsConnection_PublishMsg(req->msg->sub->conn, msg));

    natsMsg_Destroy(msg);
    if (err_will_free != NULL)
    {
        microError_Destroy(*err_will_free);
        *err_will_free = NULL;
    }
    return microError_Wrapf(
        microError_FromStatus(s),
        "failed to respond to a message with an error");
}

void micro_free_request(microRequest *req)
{
    NATS_FREE(req);
}

natsStatus
micro_new_request(microRequest **new_request, natsMsg *msg)
{
    natsStatus s = NATS_OK;
    microRequest *req = NULL;

    req = (microRequest *)NATS_CALLOC(1, sizeof(microRequest));
    s = (req != NULL) ? NATS_OK : nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        req->msg = msg;
        *new_request = req;
        return NATS_OK;
    }

    micro_free_request(req);
    return NATS_UPDATE_ERR_STACK(s);
}
