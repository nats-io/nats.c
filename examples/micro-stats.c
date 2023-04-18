// Copyright 2021 The NATS Authors
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

#include <stdio.h>
#include <stdlib.h>

#include "examples.h"

typedef struct service_state_s
{
    // in a real application this should be protected by a mutex. In this
    // example, the main control flow provides synchronization.
    int odd_count;
} service_state_t;

static void handle_default(microRequest *req)
{
    char buf[64];
    const char *response = "odd";
    int n;
    service_state_t *state = microRequest_GetServiceState(req);

    snprintf(buf, sizeof(buf), "%.*s", microRequest_GetDataLength(req), microRequest_GetData(req));
    n = atoi(buf);
    if (n % 2 != 0)
    {
        // this should be protected by a mutex in a real application.
        state->odd_count++;

        response = "even";
    }
    microRequest_Respond(req, NULL, response, strlen(response));
    return;
}

static void handle_stats(microRequest *req)
{
    microError *err = NULL;
    microServiceStats *stats = NULL;
    char buf[2048];
    service_state_t *service_state = microRequest_GetServiceState(req);
    int total, custom, len;

    err = microService_GetStats(&stats, req->service);
    if (err != NULL)
    {
        microRequest_Respond(req, &err, NULL, 0);
        return;
    }

    total = stats->endpoints[0].num_requests;
    custom = service_state->odd_count;
    len = snprintf(buf, sizeof(buf),
                   "{\"total\":%d,\"odd\":%d}", total, custom);
    microRequest_Respond(req, NULL, buf, len);
}

static microError *
run_example(natsConnection *conn, microRequestHandler stats_handler, char *buf, int buf_cap)
{
    microError *err = NULL;
    microService *m = NULL;
    microClient *c = NULL;
    service_state_t service_state = {
        .odd_count = 0,
    };
    microEndpointConfig default_cfg = {
        .name = "default",
        .handler = handle_default,
    };
    microServiceConfig cfg = {
        .name = "c-stats",
        .description = "NATS microservice in C with a custom stats handler",
        .version = "1.0.0",
        .endpoint = &default_cfg,
        .stats_handler = stats_handler,
        .state = &service_state,
    };
    int i;
    int len;
    natsMsg *resp = NULL;
    natsMsg *stats_resp = NULL;

    MICRO_CALL(err, micro_AddService(&m, conn, &cfg));
    MICRO_CALL(err, micro_NewClient(&c, conn, NULL));

    if (err == NULL)
    {
        for (i = 0; i < 10; i++)
        {
            len = snprintf(buf, buf_cap, "%d", i);
            MICRO_CALL(err, microClient_DoRequest(&resp, c, "default", buf, len));
            MICRO_DO(err, natsMsg_Destroy(resp));
        }
    }

    MICRO_CALL(err, microClient_DoRequest(&stats_resp, c, "$SRV.STATS.c-stats", "", 0));
    if (err == NULL)
    {
        len = natsMsg_GetDataLength(stats_resp);
        if (len > buf_cap - 1)
        {
            len = buf_cap - 1;
        }
        memcpy(buf, natsMsg_GetData(stats_resp), len);
        buf[len] = '\0';

        natsMsg_Destroy(stats_resp);
    }

    microService_Destroy(m);
    microClient_Destroy(c);
    return err;
}

int main(int argc, char **argv)
{
    microError *err = NULL;
    natsOptions *opts = parseArgs(argc, argv, "");
    natsConnection *conn = NULL;
    char buf[2048];

    MICRO_CALL(err, micro_ErrorFromStatus(natsConnection_Connect(&conn, opts)));

    MICRO_CALL(err, run_example(conn, NULL, buf, sizeof(buf)));
    MICRO_DO(err, printf("Default stats response:\n----\n%s\n----\n\n", buf));

    MICRO_CALL(err, run_example(conn, handle_stats, buf, sizeof(buf)));
    MICRO_DO(err, printf("Custom stats response:\n----\n%s\n----\n\n", buf));

    if (err != NULL)
    {
        fprintf(stderr, "Error: %s\n", microError_String(err, buf, sizeof(buf)));
    }

    microError_Destroy(err);
    return err == NULL ? 0 : 1;
}
