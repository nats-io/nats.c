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

// Sequence NATS microservice example.
//
// This example illustrated multiple NATS microservices communicating with each
// other. Please see the main microservice, micro-sequence.c for a more detailed
// explanation.
//
// This specific microservice implements add, multiply, and divide operations.

// arithmeticsOp is a type for a C function that implements am operation: add,
// multiply, divide.
typedef microError *(*arithmeticsOP)(long double *result, long double a1, long double a2);

// handle_arithmetics_op is a helper function that wraps an implementation of an
// operation into a request handler.
static void
handle_arithmetics_op(microRequest *req, arithmeticsOP op)
{
    microError *err = NULL;
    microArgs *args = NULL;
    long double a1, a2, result;
    char buf[1024];
    int len = 0;

    MICRO_CALL(err, micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req)));
    if ((err == NULL) && (microArgs_Count(args) != 2))
    {
        err = micro_Errorf(400, "Invalid number of arguments, expected 2 got %d", microArgs_Count(args));
    }

    MICRO_CALL(err, microArgs_GetFloat(&a1, args, 0));
    MICRO_CALL(err, microArgs_GetFloat(&a2, args, 1));
    MICRO_CALL(err, op(&result, a1, a2));
    MICRO_DO(err, len = snprintf(buf, sizeof(buf), "%Lf", result));

    microRequest_Respond(req, &err, buf, len);
    microArgs_Destroy(args);
}

static microError *
add(long double *result, long double a1, long double a2)
{
    *result = a1 + a2;
    return NULL;
}

static microError *
divide(long double *result, long double a1, long double a2)
{
    *result = a1 / a2;
    return NULL;
}

static microError *multiply(long double *result, long double a1, long double a2)
{
    *result = a1 * a2;
    return NULL;
}

// request handlers for each operation.
static void handle_add(microRequest *req) { handle_arithmetics_op(req, add); }
static void handle_divide(microRequest *req) { handle_arithmetics_op(req, divide); }
static void handle_multiply(microRequest *req) { handle_arithmetics_op(req, multiply); }

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

    microServiceConfig cfg = {
        .description = "Arithmetic operations - NATS microservice example in C",
        .name = "c-arithmetics",
        .version = "1.0.0",
    };
    microEndpointConfig add_cfg = {
        .name = "add",
        .handler = handle_add,
    };
    microEndpointConfig divide_cfg = {
        .name = "divide",
        .handler = handle_divide,
    };
    microEndpointConfig multiply_cfg = {
        .name = "multiply",
        .handler = handle_multiply,
    };

    // Connect to NATS server
    opts = parseArgs(argc, argv, "");
    s = natsConnection_Connect(&conn, opts);
    if (s != NATS_OK)
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        return 1;
    }

    // Create the Microservice that listens on nc.
    MICRO_CALL(err, micro_AddService(&m, conn, &cfg));

    // Add the endpoints for the functions.
    MICRO_CALL(err, microService_AddGroup(&g, m, "op"));
    MICRO_CALL(err, microGroup_AddEndpoint(g, &add_cfg));
    MICRO_CALL(err, microGroup_AddEndpoint(g, &multiply_cfg));
    MICRO_CALL(err, microGroup_AddEndpoint(g, &divide_cfg));

    // Run the service, until stopped.
    MICRO_CALL(err, microService_Run(m));

    // Cleanup.
    microService_Destroy(m);
    if (err != NULL)
    {
        printf("Error: %s\n", microError_String(err, errorbuf, sizeof(errorbuf)));
        microError_Destroy(err);
        return 1;
    }
    return 0;
}
