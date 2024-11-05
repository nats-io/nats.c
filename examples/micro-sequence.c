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

#include "examples.h"
#include "micro_args.h"

// Sequence NATS microservice example.
//
// This example illustrates multiple NATS microservices communicating with each
// other.
//
// The main service (c-sequence) calculates the sum of 1/f(1) + 1/f(2)... up to
// N (included).  It exposes one (default) endpoint, "sequence". The inputs are
// f (the function name) and N. name, can be "factorial", "fibonacci", or
// "power2").
//
// c-sequence parses the request, then calculates the sequence by calling the
// c-functions microservice to calculate f(1), f(2), etc. The c-functions
// service in turn uses c-arithmetics microservice for all arithmetic
// operations.
//
// Requires NATS server and CLI, and the nats.c examples fully built. See
// https://github.com/nats-io/nats.c#building
//
// RUN:
//   ```sh
//   $NATS_SERVER & # NATS_SERVER points to the NATS server binary
//   nats_pid=$!
//   sleep 2 # wait for server to start
//   ./examples/nats-micro-sequence &
//   sequence_pid=$!
//   ./examples/nats-micro-func &
//   func_pid=$!
//   ./examples/nats-micro-arithmetics &
//   arithmetics_pid=$!
//   sleep 2 # wait for microservice to start
//   nats request -r 'sequence' '"factorial" 10'
//   nats request -r 'sequence' '"power2" 10'
//   nats request -r 'sequence' '"fibonacci" 10'
//   kill $sequence_pid $func_pid $arithmetics_pid $nats_pid
//   ```
//
// OUTPUT:
//   ```
//   2.718282
//   1.999023
//   3.341705
//   ```

static microError *
call_function(long double *result, natsConnection *nc, const char *subject, int n)
{
    microError *err = NULL;
    microClient *client = NULL;
    natsMsg *response = NULL;
    microArgs *args = NULL;
    char buf[256];
    char sbuf[256];
    int len;

    len = snprintf(buf, sizeof(buf), "%d", n);
    snprintf(sbuf, sizeof(sbuf), "f.%s", subject);
    err = micro_NewClient(&client, nc, NULL);
    if (err == NULL)
        err = microClient_DoRequest(&response, client, sbuf, buf, len);
    if (err == NULL)
        err = micro_ParseArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response));
    if (err == NULL)
        err = microArgs_GetFloat(result, args, 0);

    microClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

// calculates the sum of X/f(1) + X/f(2)... up to N (included). The inputs are X
// (float), f name (string), and N (int). E.g.: '10.0 "power2" 10' will
// calculate 10/2 + 10/4 + 10/8 + 10/16 + 10/32 + 10/64 + 10/128 + 10/256 +
// 10/512 + 10/1024 = 20.998046875
static microError *handle_sequence(microRequest *req)
{
    microError *err = NULL;
    natsConnection *nc = microRequest_GetConnection(req);
    microArgs *args = NULL;
    int n = 0;
    int i;
    const char *function = NULL;
    long double initialValue = 1.0;
    long double value = 1.0;
    long double denominator = 0;
    char result[64];
    int result_len = 0;

    err = micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req));
    if ((err == NULL) && (microArgs_Count(args) != 2))
    {
        err = micro_Errorf("Invalid number of arguments, expected 2 got %d", microArgs_Count(args));
    }

    if (err == NULL)
        err = microArgs_GetString(&function, args, 0);
    if ((err == NULL) &&
        (strcmp(function, "factorial") != 0) &&
        (strcmp(function, "power2") != 0) &&
        (strcmp(function, "fibonacci") != 0))
    {
        err = micro_Errorf("Invalid function name '%s', must be 'factorial', 'power2', or 'fibonacci'", function);
    }
    if (err == NULL)
        err = microArgs_GetInt(&n, args, 1);
    if ((err == NULL) && (n < 1))
    {
        err = micro_Errorf("Invalid number of iterations %d, must be at least 1", n);
    }

    for (i = 1; (err == NULL) && (i <= n); i++)
    {
        if (err == NULL)
            err = call_function(&denominator, nc, function, i);
        if ((err == NULL) && (denominator == 0))
        {
            err = micro_Errorf("division by zero at step %d", i);
        }
        if (err == NULL)
            value = value + initialValue / denominator;
    }

    if (err == NULL)
        result_len = snprintf(result, sizeof(result), "%Lf", value);
    if (err == NULL)
        err = microRequest_Respond(req, result, result_len);

    microArgs_Destroy(args);
    return err;
}

int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    microService *m = NULL;
    char errorbuf[1024];

    microEndpointConfig sequence_cfg = {
        .Subject = "sequence",
        .Name = "sequence-service",
        .Handler = handle_sequence,
    };
    microServiceConfig cfg = {
        .Description = "Sequence adder - NATS microservice example in C",
        .Name = "c-sequence",
        .Version = "1.0.0",
        .Endpoint = &sequence_cfg,
    };

    opts = parseArgs(argc, argv, "");
    s = natsConnection_Connect(&conn, opts);
    if (s != NATS_OK)
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        natsOptions_Destroy(opts);
        return 1;
    }

    err = micro_AddService(&m, conn, &cfg);
    if (err == NULL)
        err = microService_Run(m);

    microService_Destroy(m);
    natsOptions_Destroy(opts);
    natsConnection_Destroy(conn);
    if (err != NULL)
    {
        printf("Error: %s\n", microError_String(err, errorbuf, sizeof(errorbuf)));
        microError_Destroy(err);
        return 1;
    }
    return 0;
}
