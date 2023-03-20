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
#include "conn.h"

natsError *
nats_NewMicroserviceClient(natsMicroserviceClient **new_client, natsConnection *nc, natsMicroserviceClientConfig *cfg)
{
    natsMicroserviceClient *client = NULL;

    if (new_client == NULL)
        return natsMicroserviceErrorInvalidArg;

    client = NATS_CALLOC(1, sizeof(struct microservice_client_s));
    if (client == NULL)
        return natsMicroserviceErrorOutOfMemory;

    natsConn_retain(nc);
    client->nc = nc;
    *new_client = client;
    return NULL;
}

void natsMicroserviceClient_Destroy(natsMicroserviceClient *client)
{
    if (client == NULL)
        return;

    natsConn_release(client->nc);
    NATS_FREE(client);
}

natsError *
natsMicroserviceClient_DoRequest(natsMicroserviceClient *client, natsMsg **reply, const char *subject, const char *data, int data_len)
{
    natsStatus s = NATS_OK;
    natsError *err = NULL;
    natsMsg *msg = NULL;

    if ((client == NULL) || (reply == NULL))
        return natsMicroserviceErrorInvalidArg;

    s = natsConnection_Request(&msg, client->nc, subject, data, data_len, 5000);
    if (s != NATS_OK)
        err = natsError_Wrapf(nats_NewStatusError(s), "request failed");

    err = nats_IsErrorResponse(s, msg);
    if (err == NULL)
    {
        *reply = msg;
    }
    return err;
}
