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

#ifdef _WIN32
#error "This example does not work on Windows"
#endif

#include <stdio.h>
#include <pthread.h>

#include "examples.h"

static int fakeClosure = 0;

// The "main" functions for the services, each is run as a pthread.
static void *run_arithmetics(void *closure);
static void *run_functions(void *closure);
static void *run_sequence(void *closure);

// a generic service runner, used by the services' "main" functions.
static void *run_service(natsConnection *conn, microServiceConfig *svc,
                         microEndpointConfig **endpoints, int len_endpoints);

// Callers and handlers for operations (2 floating point args, and a single int arg).
static microError *
call_arithmetics(long double *result, natsConnection *nc, const char *subject, long double a1, long double a2);
typedef microError *(*arithmeticsOP)(
    long double *result, natsConnection *conn, long double a1, long double a2);
static void handle_arithmetics_op(microRequest *req, arithmeticsOP op);

static microError *
call_function(long double *result, natsConnection *nc, const char *subject, int n);
typedef microError *(*functionOP)(long double *result, natsConnection *conn, int n);
static void handle_function_op(microRequest *req, functionOP op);

// Stop handler is the same for all services.
static void handle_stop(microService *m, microRequest *req);

// Handler for "sequence",  the main endpoint of the sequence service.
static void handle_sequence(microService *m, microRequest *req);

// Math operations, wrapped as handlers.
static microError *add(long double *result, natsConnection *nc, long double a1, long double a2);
static microError *divide(long double *result, natsConnection *nc, long double a1, long double a2);
static microError *multiply(long double *result, natsConnection *nc, long double a1, long double a2);

static void handle_add(microService *m, microRequest *req) { handle_arithmetics_op(req, add); }
static void handle_divide(microService *m, microRequest *req) { handle_arithmetics_op(req, divide); }
static void handle_multiply(microService *m, microRequest *req) { handle_arithmetics_op(req, multiply); }

static microError *factorial(long double *result, natsConnection *nc, int n);
static microError *fibonacci(long double *result, natsConnection *nc, int n);
static microError *power2(long double *result, natsConnection *nc, int n);
static void handle_factorial(microService *m, microRequest *req) { handle_function_op(req, factorial); }
static void handle_fibonacci(microService *m, microRequest *req) { handle_function_op(req, fibonacci); }
static void handle_power2(microService *m, microRequest *req) { handle_function_op(req, power2); }

static microEndpointConfig stop_cfg = {
    .name = "stop",
    .handler = handle_stop,
    .closure = &fakeClosure,
    .schema = NULL,
};

int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    pthread_t arithmetics_th;
    void *a_val = NULL;
    pthread_t functions_th;
    void *f_val = NULL;
    pthread_t sequence_th;
    void *s_val = NULL;
    int errno;
    char errorbuf[1024];

    // Connect and start the services
    opts = parseArgs(argc, argv, "");
    s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
    {
        errno = pthread_create(&arithmetics_th, NULL, run_arithmetics, conn);
        if (errno != 0)
        {
            printf("Error creating arithmetics thread: %d: %s\n", errno, strerror(errno));
            return 1;
        }

        errno = pthread_create(&functions_th, NULL, run_functions, conn);
        if (errno != 0)
        {
            printf("Error creating functions thread: %d: %s\n", errno, strerror(errno));
            return 1;
        }

        errno = pthread_create(&sequence_th, NULL, run_sequence, conn);
        if (errno != 0)
        {
            printf("Error creating sequence thread: %d: %s\n", errno, strerror(errno));
            return 1;
        }
    }

    // Wait for the services to stop and self-destruct.
    if (s == NATS_OK)
    {
        MICRO_DO(err,
                 {
                     if (pthread_join(arithmetics_th, &a_val) == 0)
                         err = (microError *)a_val;
                 });
        MICRO_DO(err,
                 {
                     if (pthread_join(functions_th, &f_val) == 0)
                         err = (microError *)f_val;
                 });
        MICRO_DO(err,
                 {
                     if (pthread_join(sequence_th, &s_val) == 0)
                         err = (microError *)s_val;
                 });
    }

    if (s != NATS_OK)
    {
        err = microError_FromStatus(s);
    }
    if (err != NULL)
    {
        printf("Error: %s\n", microError_String(err, errorbuf, sizeof(errorbuf)));
        return 1;
    }
    return 0;
}

static void *run_sequence(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    microServiceConfig cfg = {
        .description = "Sequence adder - NATS microservice example in C",
        .name = "c-sequence",
        .version = "1.0.0",
    };
    microEndpointConfig sequence_cfg = {
        .subject = "sequence",
        .name = "sequence-service",
        .handler = handle_sequence,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig *endpoints[] = {&sequence_cfg};

    return run_service(conn, &cfg, endpoints, 1);
}

// calculates the sum of X/f(1) + X/f(2)... up to N (included). The inputs are X
// (float), f name (string), and N (int). E.g.: '10.0 "power2" 10' will
// calculate 10/2 + 10/4 + 10/8 + 10/16 + 10/32 + 10/64 + 10/128 + 10/256 +
// 10/512 + 10/1024 = 20.998046875
static void handle_sequence(microService *m, microRequest *req)
{
    microError *err = NULL;
    natsConnection *nc = microService_GetConnection(m);
    microArgs *args = NULL;
    int n = 0;
    int i;
    const char *function;
    long double initialValue = 1.0;
    long double value = 1.0;
    long double denominator = 0;
    char result[64];
    int result_len = 0;

    MICRO_CALL(err, micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req)));
    MICRO_CALL_IF(err, (microArgs_Count(args) != 2),
                  micro_NewErrorf(400, "Invalid number of arguments, expected 2 got %d", microArgs_Count(args)));

    MICRO_CALL(err, microArgs_GetString(&function, args, 0));
    MICRO_CALL_IF(err, ((strcmp(function, "factorial") != 0) && (strcmp(function, "power2") != 0) && (strcmp(function, "fibonacci") != 0)),
                  micro_NewErrorf(400, "Invalid function name '%s', must be 'factorial', 'power2', or 'fibonacci'", function));
    MICRO_CALL(err, microArgs_GetInt(&n, args, 1));
    MICRO_CALL_IF(err,
                  (n < 1),
                  micro_NewErrorf(400, "Invalid number of iterations %d, must be at least 1", n));

    for (i = 1; (err == NULL) && (i <= n); i++)
    {
        MICRO_CALL(err, call_function(&denominator, nc, function, i));
        MICRO_CALL_IF(err, denominator == 0,
                      micro_NewErrorf(500, "division by zero at step %d", i));
        MICRO_DO(err, value = value + initialValue / denominator);
    }
    
    MICRO_DO(err, result_len = snprintf(result, sizeof(result), "%Lf", value));

    microRequest_Respond(req, &err, result, result_len);
    microArgs_Destroy(args);
}

static void *run_arithmetics(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    microServiceConfig cfg = {
        .description = "Arithmetic operations - NATS microservice example in C",
        .name = "c-arithmetics",
        .version = "1.0.0",
    };
    microEndpointConfig add_cfg = {
        .name = "add",
        .handler = handle_add,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig divide_cfg = {
        .name = "divide",
        .handler = handle_divide,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig multiply_cfg = {
        .name = "multiply",
        .handler = handle_multiply,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig *endpoints[] =
        {&add_cfg, &divide_cfg, &multiply_cfg};

    return run_service(conn, &cfg, endpoints, 3);
}

static void *run_functions(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    microServiceConfig cfg = {
        .description = "Functions - NATS microservice example in C",
        .name = "c-functions",
        .version = "1.0.0",
    };
    microEndpointConfig factorial_cfg = {
        .name = "factorial",
        .handler = handle_factorial,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig fibonacci_cfg = {
        .name = "fibonacci",
        .handler = handle_fibonacci,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig power2_cfg = {
        .name = "power2",
        .handler = handle_power2,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    microEndpointConfig *endpoints[] =
        {&factorial_cfg, &fibonacci_cfg, &power2_cfg};

    return run_service(conn, &cfg, endpoints, 3);
}

static void
handle_arithmetics_op(microRequest *req, arithmeticsOP op)
{
    microError *err = NULL;
    microArgs *args = NULL;
    long double a1, a2, result;
    char buf[1024];
    int len = 0;

    MICRO_CALL(err, micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req)));
    MICRO_CALL_IF(err, (microArgs_Count(args) != 2),
                  micro_NewErrorf(400, "Invalid number of arguments, expected 2 got %d", microArgs_Count(args)));
    MICRO_CALL(err, microArgs_GetFloat(&a1, args, 0));
    MICRO_CALL(err, microArgs_GetFloat(&a2, args, 1));
    MICRO_CALL(err, op(&result, microRequest_GetConnection(req), a1, a2));
    MICRO_DO(err, len = snprintf(buf, sizeof(buf), "%Lf", result));

    microRequest_Respond(req, &err, buf, len);
    microArgs_Destroy(args);
}

static void
handle_function_op(microRequest *req, functionOP op)
{
    microError *err = NULL;
    microArgs *args = NULL;
    int n;
    long double result;
    char buf[1024];
    int len = 0;

    MICRO_CALL(err, micro_ParseArgs(&args, microRequest_GetData(req), microRequest_GetDataLength(req)));
    MICRO_CALL_IF(err, (microArgs_Count(args) != 1),
                  micro_NewErrorf(400, "Invalid number of arguments, expected 1 got %d", microArgs_Count(args)));
    MICRO_CALL(err, microArgs_GetInt(&n, args, 0));
    MICRO_CALL(err, op(&result, microRequest_GetConnection(req), n));
    MICRO_DO(err, len = snprintf(buf, sizeof(buf), "%Lf", result));

    microRequest_Respond(req, &err, buf, len);
    microArgs_Destroy(args);
}

static microError *
call_arithmetics(long double *result, natsConnection *nc, const char *subject, long double a1, long double a2)
{
    microError *err = NULL;
    microClient *client = NULL;
    natsMsg *response = NULL;
    microArgs *args = NULL;
    char buf[1024];
    int len;

    MICRO_CALL(err, microClient_Create(&client, nc, NULL));
    MICRO_DO(err, len = snprintf(buf, sizeof(buf), "%Lf %Lf", a1, a2));
    MICRO_CALL(err, microClient_DoRequest(client, &response, subject, buf, len));
    MICRO_CALL(err, micro_ParseArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response)));
    MICRO_CALL(err, microArgs_GetFloat(result, args, 0));

    microClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

static microError *
call_function(long double *result, natsConnection *nc, const char *subject, int n)
{
    microError *err = NULL;
    microClient *client = NULL;
    natsMsg *response = NULL;
    microArgs *args = NULL;
    char buf[1024];
    int len;

    MICRO_CALL(err, microClient_Create(&client, nc, NULL));
    MICRO_DO(err, len = snprintf(buf, sizeof(buf), "%d", n));
    MICRO_CALL(err, microClient_DoRequest(client, &response, subject, buf, len));
    MICRO_CALL(err, micro_ParseArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response)));
    MICRO_CALL(err, microArgs_GetFloat(result, args, 0));

    microClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

static void handle_stop(microService *m, microRequest *req)
{
    microError *err = NULL;
    const char *response = "OK";
    int len = 0;
    void *ret;

    MICRO_CALL(err, microService_Stop(m));
    MICRO_DO(err, len = strlen(response));

    ret = (void *)(microError_Status(err));
    microRequest_Respond(req, &err, response, len);
    pthread_exit(ret);
}

static void *run_service(natsConnection *conn, microServiceConfig *svc,
                         microEndpointConfig **endpoints, int len_endpoints)
{
    microError *err = NULL;
    microService *m = NULL;
    char errbuf[1024];
    int i;

    MICRO_CALL(err, microService_Create(&m, conn, svc));
    for (i = 0; (err == NULL) && (i < len_endpoints); i++)
    {
        MICRO_CALL(err, microService_AddEndpoint(NULL, m, endpoints[i]));
    }
    MICRO_CALL(err, microService_AddEndpoint(NULL, m, &stop_cfg));
    MICRO_CALL(err, microService_Run(m));

    microService_Release(m);
    return err;
}

static microError *
add(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 + a2;
    return NULL;
}

static microError *
divide(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 / a2;
    return NULL;
}

static microError *multiply(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 * a2;
    return NULL;
}

static microError *
factorial(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;

    if (n < 1)
        err = micro_NewErrorf(400, "n=%d. must be greater than 0", n);

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "multiply", *result, i);
        if (err != NULL)
            return err;
    }
    return NULL;
}

static microError *
fibonacci(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;
    long double n1, n2;

    if (n < 0)
        err = micro_NewErrorf(400, "n=%d. must be non-negative", n);

    if (n < 2)
    {
        *result = n;
        return NULL;
    }

    for (i = 1, n1 = 0, n2 = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "add", n1, n2);
        if (err != NULL)
            return err;
        n1 = n2;
        n2 = *result;
    }
    return NULL;
}

static microError *power2(long double *result, natsConnection *nc, int n)
{
    microError *err = NULL;
    int i;

    if (n < 1)
        return micro_NewErrorf(400, "n=%d. must be greater than 0", n);

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "multiply", *result, 2);
        if (err != NULL)
            return err;
    }
    return NULL;
}
