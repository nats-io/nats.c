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
static void *run_service(natsConnection *conn, natsMicroserviceConfig *svc,
                         natsMicroserviceEndpointConfig **endpoints, const char **ep_names, int len_endpoints);

// Callers and handlers for operations (2 floating point args, and a single int arg).
static natsMicroserviceError *
    call_arithmetics(long double *result, natsConnection *nc, const char *subject, long double a1, long double a2);
typedef natsMicroserviceError *(*arithmeticsOP)(
    long double *result, natsConnection *conn, long double a1, long double a2);
static void handle_arithmetics_op(natsMicroserviceRequest *req, arithmeticsOP op);

static natsMicroserviceError *
    call_function(long double *result, natsConnection *nc, const char *subject, int n);
typedef natsMicroserviceError *(*functionOP)(long double *result, natsConnection *conn, int n);
static void handle_function_op(natsMicroserviceRequest *req, functionOP op);

// Stop handler is the same for all services.
static void handle_stop(natsMicroservice *m, natsMicroserviceRequest *req);

// Handler for "sequence",  the main endpoint of the sequence service.
static void handle_sequence(natsMicroservice *m, natsMicroserviceRequest *req);

// Math operations, wrapped as handlers.
static natsMicroserviceError *add(long double *result, natsConnection *nc, long double a1, long double a2);
static natsMicroserviceError *divide(long double *result, natsConnection *nc, long double a1, long double a2);
static natsMicroserviceError *multiply(long double *result, natsConnection *nc, long double a1, long double a2);

static void handle_add(natsMicroservice *m, natsMicroserviceRequest *req) { handle_arithmetics_op(req, add); }
static void handle_divide(natsMicroservice *m, natsMicroserviceRequest *req) { handle_arithmetics_op(req, divide); }
static void handle_multiply(natsMicroservice *m, natsMicroserviceRequest *req) { handle_arithmetics_op(req, multiply); }

static natsMicroserviceError *factorial(long double *result, natsConnection *nc, int n);
static natsMicroserviceError *fibonacci(long double *result, natsConnection *nc, int n);
static natsMicroserviceError *power2(long double *result, natsConnection *nc, int n);
static void handle_factorial(natsMicroservice *m, natsMicroserviceRequest *req) { handle_function_op(req, factorial); }
static void handle_fibonacci(natsMicroservice *m, natsMicroserviceRequest *req) { handle_function_op(req, fibonacci); }
static void handle_power2(natsMicroservice *m, natsMicroserviceRequest *req) { handle_function_op(req, power2); }

static natsMicroserviceEndpointConfig stop_cfg = {
    .subject = "stop",
    .handler = handle_stop,
    .closure = &fakeClosure,
    .schema = NULL,
};

int main(int argc, char **argv)
{
    natsStatus s = NATS_OK;
    natsConnection *conn = NULL;
    natsOptions *opts = NULL;
    pthread_t arithmetics_th;
    void *a_val = NULL;
    pthread_t functions_th;
    void *f_val = NULL;
    pthread_t sequence_th;
    void *s_val = NULL;
    int errno;

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
        pthread_join(arithmetics_th, &a_val);
        s = (natsStatus)(uintptr_t)a_val;
    }
    if (s == NATS_OK)
    {
        pthread_join(functions_th, &f_val);
        s = (natsStatus)(uintptr_t)f_val;
    }
    if (s == NATS_OK)
    {
        pthread_join(sequence_th, &s_val);
        s = (natsStatus)(uintptr_t)s_val;
    }

    if (s == NATS_OK)
    {
        return 0;
    }
    else
    {
        printf("Error: %u - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        return 1;
    }
}

static void *run_sequence(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    natsMicroserviceConfig cfg = {
        .description = "Sequence adder - NATS microservice example in C",
        .name = "c-sequence",
        .version = "1.0.0",
    };
    natsMicroserviceEndpointConfig sequence_cfg = {
        .subject = "sequence",
        .handler = handle_sequence,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig *endpoints[] = {&sequence_cfg};
    const char *names[] = {"sequence"};

    return run_service(conn, &cfg, endpoints, names, 1);
}

// calculates the sum of X/f(1) + X/f(2)... up to N (included). The inputs are X
// (float), f name (string), and N (int). E.g.: '10.0 "power2" 10' will
// calculate 10/2 + 10/4 + 10/8 + 10/16 + 10/32 + 10/64 + 10/128 + 10/256 +
// 10/512 + 10/1024 = 20.998046875
static void handle_sequence(natsMicroservice *m, natsMicroserviceRequest *req)
{
    natsMicroserviceError *err = NULL;
    natsConnection *nc = natsMicroservice_GetConnection(m);
    natsArgs *args = NULL;
    int n = 0;
    int i;
    const char *function;
    long double initialValue = 1.0;
    long double value = 1.0;
    long double denominator = 0;
    char result[64];

    err = natsParseAsArgs(&args, natsMicroserviceRequest_GetData(req), natsMicroserviceRequest_GetDataLength(req));
    if ((err == NULL) &&
        (natsArgs_Count(args) != 2))
    {
        err = nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "Invalid number of arguments");
    }

    if (err == NULL)
    {
        err = natsArgs_GetString(&function, args, 0);
    }
    if (err == NULL)
    {
        err = natsArgs_GetInt(&n, args, 1);
    }
    if ((err == NULL) &&
        (strcmp(function, "factorial") != 0) &&
        (strcmp(function, "power2") != 0) &&
        (strcmp(function, "fibonacci") != 0))
    {
        err = nats_NewMicroserviceError(
            NATS_INVALID_ARG, 400,
            "Invalid function name, must be 'factorial', 'power2', or 'fibonacci'");
    }
    if ((err == NULL) &&
        (n < 1))
    {
        err = nats_NewMicroserviceError(
            NATS_INVALID_ARG, 400,
            "Invalid number of iterations, must be at least 1");
    }

    for (i = 1; (err == NULL) && (i <= n); i++)
    {
        err = call_function(&denominator, nc, function, i);
        if (err == NULL && denominator == 0)
        {
            err = nats_NewMicroserviceError(0, 500, "division by zero");
        }
        if (err == NULL)
        {
            value = value + initialValue / denominator;
        }
    }

    if (err == NULL)
    {
        snprintf(result, sizeof(result), "%Lf", value);
        err = natsMicroserviceRequest_Respond(req, result, strlen(result));
    }

    if (err != NULL)
    {
        natsMicroserviceRequest_Error(req, &err);
    }
    natsArgs_Destroy(args);
}

static void *run_arithmetics(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    natsMicroserviceConfig cfg = {
        .description = "Arithmetic operations - NATS microservice example in C",
        .name = "c-arithmetics",
        .version = "1.0.0",
    };
    natsMicroserviceEndpointConfig add_cfg = {
        .subject = "add",
        .handler = handle_add,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig divide_cfg = {
        .subject = "divide",
        .handler = handle_divide,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig multiply_cfg = {
        .subject = "multiply",
        .handler = handle_multiply,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig *endpoints[] =
        {&add_cfg, &divide_cfg, &multiply_cfg};
    const char *names[] =
        {"add", "divide", "multiply"};

    return run_service(conn, &cfg, endpoints, names, 3);
}

static void *run_functions(void *closure)
{
    natsConnection *conn = (natsConnection *)closure;
    natsMicroserviceConfig cfg = {
        .description = "Functions - NATS microservice example in C",
        .name = "c-functions",
        .version = "1.0.0",
    };
    natsMicroserviceEndpointConfig factorial_cfg = {
        .subject = "factorial",
        .handler = handle_factorial,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig fibonacci_cfg = {
        .subject = "fibonacci",
        .handler = handle_fibonacci,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig power2_cfg = {
        .subject = "power2",
        .handler = handle_power2,
        .closure = &fakeClosure,
        .schema = NULL,
    };
    natsMicroserviceEndpointConfig *endpoints[] =
        {&factorial_cfg, &fibonacci_cfg, &power2_cfg};
    const char *names[] =
        {"factorial", "fibonacci", "power2"};

    return run_service(conn, &cfg, endpoints, names, 3);
}

static void
handle_arithmetics_op(natsMicroserviceRequest *req, arithmeticsOP op)
{
    natsMicroserviceError *err = NULL;
    natsArgs *args = NULL;
    long double a1, a2, result;
    char buf[1024];
    int len;

    err = natsParseAsArgs(&args, natsMicroserviceRequest_GetData(req), natsMicroserviceRequest_GetDataLength(req));
    if ((err == NULL) && (natsArgs_Count(args) != 2))
    {
        err = nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "Invalid number of arguments, must be 2");
    }
    if (err == NULL)
    {
        err = natsArgs_GetFloat(&a1, args, 0);
    }
    if (err == NULL)
    {
        err = natsArgs_GetFloat(&a2, args, 1);
    }
    if (err == NULL)
    {
        err = op(&result, natsMicroserviceRequest_GetConnection(req), a1, a2);
    }
    if (err == NULL)
    {
        len = snprintf(buf, sizeof(buf), "%Lf", result);
        err = natsMicroserviceRequest_Respond(req, buf, len);
    }

    if (err != NULL)
    {
        natsMicroserviceRequest_Error(req, &err);
    }
    natsArgs_Destroy(args);
}

static void
handle_function_op(natsMicroserviceRequest *req, functionOP op)
{
    natsMicroserviceError *err = NULL;
    natsArgs *args = NULL;
    int n;
    long double result;
    char buf[1024];
    int len;

    err = natsParseAsArgs(&args, natsMicroserviceRequest_GetData(req), natsMicroserviceRequest_GetDataLength(req));
    if ((err == NULL) && (natsArgs_Count(args) != 1))
    {
        err = nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "Invalid number of arguments, must be 1");
    }
    if (err == NULL)
    {
        err = natsArgs_GetInt(&n, args, 0);
    }
    if (err == NULL)
    {
        err = op(&result, natsMicroserviceRequest_GetConnection(req), n);
    }
    if (err == NULL)
    {
        len = snprintf(buf, sizeof(buf), "%Lf", result);
        err = natsMicroserviceRequest_Respond(req, buf, len);
    }

    if (err != NULL)
    {
        natsMicroserviceRequest_Error(req, &err);
    }
    natsArgs_Destroy(args);
}

static natsMicroserviceError *
call_arithmetics(long double *result, natsConnection *nc, const char *subject, long double a1, long double a2)
{
    natsMicroserviceError *err = NULL;
    natsMicroserviceClient *client = NULL;
    natsMsg *response = NULL;
    natsArgs *args = NULL;
    char buf[1024];
    int len;

    err = nats_NewMicroserviceClient(&client, nc, NULL);
    if (err == NULL)
    {
        len = snprintf(buf, sizeof(buf), "%Lf %Lf", a1, a2);
        err = natsMicroserviceClient_DoRequest(client, &response, subject, buf, len);
    }
    if (err == NULL)
    {
        err = natsParseAsArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response));
    }
    if (err == NULL)
    {
        err = natsArgs_GetFloat(result, args, 0);
    }

    natsMicroserviceClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

static natsMicroserviceError *
call_function(long double *result, natsConnection *nc, const char *subject, int n)
{
    natsMicroserviceError *err = NULL;
    natsMicroserviceClient *client = NULL;
    natsMsg *response = NULL;
    natsArgs *args = NULL;
    char buf[1024];
    int len;

    err = nats_NewMicroserviceClient(&client, nc, NULL);
    if (err == NULL)
    {
        len = snprintf(buf, sizeof(buf), "%d", n);
        err = natsMicroserviceClient_DoRequest(client, &response, subject, buf, len);
    }
    if (err == NULL)
    {
        err = natsParseAsArgs(&args, natsMsg_GetData(response), natsMsg_GetDataLength(response));
    }
    if (err == NULL)
    {
        err = natsArgs_GetFloat(result, args, 0);
    }

    natsMicroserviceClient_Destroy(client);
    natsMsg_Destroy(response);
    return err;
}

static void handle_stop(natsMicroservice *m, natsMicroserviceRequest *req)
{
    natsMicroserviceError *err;

    err = natsMicroservice_Stop(m);
    if (err == NULL)
    {
        err = natsMicroserviceRequest_Respond(req, "OK", 2);
    }

    if (err == NULL)
    {
        pthread_exit((void *)(NATS_OK));
    }
    else
    {
        natsMicroserviceRequest_Error(req, &err);
        pthread_exit((void *)(err->status));
    }
}

static void *run_service(natsConnection *conn, natsMicroserviceConfig *svc,
                         natsMicroserviceEndpointConfig **endpoints, const char **ep_names, int len_endpoints)
{
    natsMicroserviceError *err = NULL;
    natsMicroservice *m = NULL;
    char errbuf[1024];
    int i;

    err = nats_AddMicroservice(&m, conn, svc);
    for (i = 0; (err == NULL) && (i < len_endpoints); i++)
    {
        err = natsMicroservice_AddEndpoint(NULL, m, ep_names[i], endpoints[i]);
        if (err != NULL)
        {
            break;
        }
    }
    if (err == NULL)
    {
        err = natsMicroservice_AddEndpoint(NULL, m, "stop", &stop_cfg);
    }
    if (err == NULL)
    {
        err = natsMicroservice_Run(m);
    }

    natsMicroservice_Destroy(m);
    if (err != NULL)
    {
        printf("Error: %s\n", err->String(err, errbuf, sizeof(errbuf)));
        return (void *)(err->status);
    }
    return (void *)NATS_OK;
}

static natsMicroserviceError *
add(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 + a2;
    return NULL;
}

static natsMicroserviceError *
divide(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 / a2;
    return NULL;
}

static natsMicroserviceError *multiply(long double *result, natsConnection *nc, long double a1, long double a2)
{
    *result = a1 * a2;
    return NULL;
}

static natsMicroserviceError *
factorial(long double *result, natsConnection *nc, int n)
{
    natsMicroserviceError *err = NULL;
    int i;

    if (n < 1)
        return nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "n must be greater than 0");

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "multiply", *result, i);
        if (err != NULL)
            return err;
    }
    return NULL;
}

static natsMicroserviceError *
fibonacci(long double *result, natsConnection *nc, int n)
{
    natsMicroserviceError *err = NULL;
    int i;
    long double n1, n2;

    if (n < 0)
        return nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "n must be greater than 0");

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

static natsMicroserviceError *power2(long double *result, natsConnection *nc, int n)
{
    natsMicroserviceError *err = NULL;
    int i;

    if (n < 1)
        return nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "n must be greater than 0");

    *result = 1;
    for (i = 1; i <= n; i++)
    {
        err = call_arithmetics(result, nc, "multiply", *result, 2);
        if (err != NULL)
            return err;
    }
    return NULL;
}
