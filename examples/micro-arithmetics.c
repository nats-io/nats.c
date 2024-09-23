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
// other. Please see the main microservice, micro-sequence.c for a more detailed
// explanation.
//
// This specific microservice implements add, multiply, and divide operations.

// arithmeticsOp is a type for a C function that implements am operation: add,
// multiply, divide.
typedef void (*arithmeticsOP)(long double *result, long double a1, long double a2);

// handle_arithmetics_op is a helper function that wraps an implementation of an
// operation into a request handler.
static microError *
handle_arithmetics_op(microRequest *req, arithmeticsOP op)
{
    microError *err = NULL;
    microArgs *args = NULL;
    long double a1 = 0, a2 = 0, result = 0;
    char buf[1024];
    int len = 0;

    err = micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req));
    if ((err == NULL) && (microArgs_Count(args) != 2))
    {
        err = micro_Errorf("invalid number of arguments, expected 2 got %d", microArgs_Count(args));
    }
    if (err == NULL)
        err = microArgs_GetFloat(&a1, args, 0);
    if (err == NULL)
        err = microArgs_GetFloat(&a2, args, 1);
    if (err == NULL)
        op(&result, a1, a2);
    if (err == NULL)
        len = snprintf(buf, sizeof(buf), "%Lf", result);
    if (err == NULL)
        err = microRequest_Respond(req, buf, len);

    microArgs_Destroy(args);
    return microError_Wrapf(err, "failed to handle arithmetics operation");
}

static void add(long double *result, long double a1, long double a2)
{
    *result = a1 + a2;
}

static void divide(long double *result, long double a1, long double a2)
{
    *result = a1 / a2;
}

static void multiply(long double *result, long double a1, long double a2)
{
    *result = a1 * a2;
}

// request handlers for each operation.
static microError *handle_add(microRequest *req) { return handle_arithmetics_op(req, add); }
static microError *handle_divide(microRequest *req) { return handle_arithmetics_op(req, divide); }
static microError *handle_multiply(microRequest *req) { return handle_arithmetics_op(req, multiply); }

// main is the main entry point for the microservice.
int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    microService *m = NULL;
    microGroup *g = NULL;
    char errorbuf[1024];

    // Connect to NATS server
    opts = parseArgs(argc, argv, "");
    s = natsConnection_Connect(&conn, opts);
    if (s != NATS_OK)
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        natsOptions_Destroy(opts);
        return 1;
    }

    // Create the Microservice that listens on nc.
    microServiceConfig cfg = {
        .Description = "Arithmetic operations - NATS microservice example in C",
        .Name = "c-arithmetics",
        .Version = "1.0.0",
    };
    err = micro_AddService(&m, conn, &cfg);

    // Add the endpoints for the functions.
    if (err == NULL)
    {
        microGroupConfig groupConfig = { .Prefix = "op" };
        err = microService_AddGroup(&g, m, &groupConfig);
    }
    if (err == NULL)
    {
        microEndpointConfig addConfig = { .Name = "add", .Handler = handle_add };
        err = microGroup_AddEndpoint(g, &addConfig);
    }
    if (err == NULL)
    {
        microEndpointConfig multiplyConfig = { .Name = "multiply", .Handler = handle_multiply };
        err = microGroup_AddEndpoint(g, &multiplyConfig);
    }
    if (err == NULL)
    {
        microEndpointConfig divideConfig = { .Name = "divide", .Handler = handle_divide };
        err = microGroup_AddEndpoint(g, &divideConfig);
    }

    // Run the service, until stopped.
    if (err == NULL)
        err = microService_Run(m);

    // Cleanup.
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
