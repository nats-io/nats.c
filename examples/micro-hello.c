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

#include "examples.h"

// Hello World! NATS microservice example.
//
// Requires NATS server and CLI, and the nats.c examples fully built. See
// https://github.com/nats-io/nats.c#building
//
// RUN:
//   ```sh
//   $NATS_SERVER & # NATS_SERVER points to the NATS server binary
//   nats_pid=$!
//   sleep 2 # wait for server to start
//   ./examples/nats-micro-hello &
//   hello_pid=$!
//   sleep 2 # wait for microservice to start
//   nats request 'hello' ''
//   kill $hello_pid $nats_pid 
//   ```
//
// OUTPUT:
//   ```
//   06:34:57 Sending request on "hello"
//   06:34:57 Received with rtt 1.08ms
//   Hello, World!
//   ```

#define HELLO "Hello, World!"

static void
handle(microRequest *req)
{
    microRequest_Respond(req, NULL, HELLO, sizeof(HELLO));
}

int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    microService *m = NULL;
    char errorbuf[1024];

    microEndpointConfig hello_cfg = {
        .name = "hello",
        .handler = handle,
    };
    microServiceConfig cfg = {
        .description = "Hello World! - NATS microservice example in C",
        .name = "c-hello",
        .version = "1.0.0",
        .endpoint = &hello_cfg,
    };

    // Connect and start the services
    opts = parseArgs(argc, argv, "");
    s = natsConnection_Connect(&conn, opts);
    if (s != NATS_OK)
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        return 1;
    }
    MICRO_CALL(err, micro_AddService(&m, conn, &cfg));
    MICRO_CALL(err, microService_Run(m));

    microService_Destroy(m);
    if (err != NULL)
    {
        printf("Error: %s\n", microError_String(err, errorbuf, sizeof(errorbuf)));
        microError_Destroy(err);
        return 1;
    }
    return 0;
}

