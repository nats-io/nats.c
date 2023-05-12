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

#include "microp.h"
#include "conn.h"

microError *
micro_NewClient(microClient **new_client, natsConnection *nc, microClientConfig *cfg)
{
    microClient *client = NULL;

    if (new_client == NULL)
        return micro_ErrorInvalidArg;

    client = NATS_CALLOC(1, sizeof(microClient));
    if (client == NULL)
        return micro_ErrorOutOfMemory;

    natsConn_retain(nc);
    client->nc = nc;
    *new_client = client;
    return NULL;
}

void microClient_Destroy(microClient *client)
{
    if (client == NULL)
        return;

    natsConn_release(client->nc);
    NATS_FREE(client);
}

microError *
microClient_DoRequest(natsMsg **reply, microClient *client, const char *subject, const char *data, int data_len)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsMsg *msg = NULL;

    if ((client == NULL) || (reply == NULL))
        return micro_ErrorInvalidArg;

    s = natsConnection_Request(&msg, client->nc, subject, data, data_len, 5000);
    if (s != NATS_OK)
    {
        return microError_Wrapf(micro_ErrorFromStatus(s), "request failed");
    }

    err = micro_is_error_message(s, msg);
    if (err == NULL)
    {
        *reply = msg;
    }
    return err;
}
