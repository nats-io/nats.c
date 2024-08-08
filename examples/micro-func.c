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
// This specific microservice implements factorial, fibonacci, and power2
// functions. Instead of performing arithmetic operations locally, we call the
// arithmetics microservice to perform the operations.

// functionHandler is a type for a C function that implements a "function", i.e.
// power2, factorial, etc.
typedef microError *(*functionHandler)(long double *result, natsConnection *conn, int n);

// callArithmetics is a helper function that calls the arithmetics microservice.
static microError *
call_arithmetics(long double *result, natsConnection *nc, const char *subject, long double a1, long double a2)
{
    microError *err = NULL;
    microClient *client = NULL;
    natsMsg *response = NULL;
    microArgs *args = NULL;
    char buf[1024];
    int len = 0;

    err = micro_NewClient(&client, nc, NULL);
    if (err == NULL)
        len = snprintf(buf, sizeof(buf), "%Lf %Lf", a1, a2);
    if (err == NULL)
        err = microClient_DoRequest(&response, client, subject, buf, len);
    if (err == NULL)
        err = micro_ParseArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response));
    if (err == NULL)
        err = microArgs_GetFloat(result, args, 0);

    microClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

// factorial implements the factorial(N) function. Calls the arithmetics service
// for all multiplications.
static microError *
factorial(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;

    if (n < 1)
        return micro_Errorf("n=%d. must be greater than 0", n);

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "op.multiply", *result, i);
        if (err != NULL)
            return err;
    }
    return NULL;
}

// fibonacci implements the fibonacci(N) function. Calls the arithmetics service
// for all additions.
static microError *
fibonacci(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;
    long double n1, n2;

    if (n < 0)
        return micro_Errorf("n=%d. must be non-negative", n);

    if (n < 2)
    {
        *result = n;
        return NULL;
    }

    for (i = 1, n1 = 0, n2 = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "op.add", n1, n2);
        if (err != NULL)
            return err;
        n1 = n2;
        n2 = *result;
    }
    return NULL;
}

// power2 implements the 2**N function. Calls the arithmetics service
// for all multiplications.
static microError *power2(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;

    if (n < 1)
        return micro_Errorf("n=%d. must be greater than 0", n);

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "op.multiply", *result, 2);
        if (err != NULL)
            return err;
    }
    return NULL;
}

// handle_function_op is a helper function that wraps an implementation function
// like factorial, fibonacci, etc. into a request handler.
static microError *
handle_function_op(microRequest *req, functionHandler op)
{
    microError *err = NULL;
    microArgs *args = NULL;
    int n = 0;
    long double result = 0;
    char buf[1024];
    int len = 0;

    err = micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req));
    if ((err == NULL) && (microArgs_Count(args) != 1))
    {
        err = micro_Errorf("Invalid number of arguments, expected 1 got %d", microArgs_Count(args));
    }
    if (err == NULL)
        err = microArgs_GetInt(&n, args, 0);
    if (err == NULL)
        err = op(&result, microRequest_GetConnection(req), n);
    if (err == NULL)
        len = snprintf(buf, sizeof(buf), "%Lf", result);
    if (err == NULL)
        err = microRequest_Respond(req, buf, len);

    microArgs_Destroy(args);
    return err;
}

// handle_... are the request handlers for each function.
static microError *handle_factorial(microRequest *req) { return handle_function_op(req, factorial); }
static microError *handle_fibonacci(microRequest *req) { return handle_function_op(req, fibonacci); }
static microError *handle_power2(microRequest *req) { return handle_function_op(req, power2); }

// main is the main entry point for the microservice.
int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsOptions *opts = NULL;
    natsConnection *conn = NULL;
    microService *m = NULL;
    microGroup *g = NULL;
    char errorbuf[1024];

    microServiceConfig cfg = {
        .Description = "Functions - NATS microservice example in C",
        .Name = "c-functions",
        .Version = "1.0.0",
    };
    microEndpointConfig factorial_cfg = {
        .Name = "factorial",
        .Handler = handle_factorial,
    };
    microEndpointConfig fibonacci_cfg = {
        .Name = "fibonacci",
        .Handler = handle_fibonacci,
    };
    microEndpointConfig power2_cfg = {
        .Name = "power2",
        .Handler = handle_power2,
    };

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
    err = micro_AddService(&m, conn, &cfg);

    // Add the endpoints for the functions.
    if (err == NULL)
        err = microService_AddGroup(&g, m, "f");
    if (err == NULL)
        err = microGroup_AddEndpoint(g, &factorial_cfg);
    if (err == NULL)
        err = microGroup_AddEndpoint(g, &fibonacci_cfg);
    if (err == NULL)
        err = microGroup_AddEndpoint(g, &power2_cfg);

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
