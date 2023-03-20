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

#ifdef DEV_MODE
// For type safety

static void _retain(natsError *err) { err->refs++; }
static void _release(natsError *err) { err->refs--; }

void natsError_Lock(natsConnection *nc) { natsMutex_Lock(nc->mu); }
void natsConn_Unlock(natsConnection *nc) { natsMutex_Unlock(nc->mu); }

#else
// We know what we are doing :-)

#define _retain(c) ((c)->refs++)
#define _release(c) ((c)->refs--)

#endif // DEV_MODE

static natsError _errorOutOfMemory = {
    .status = NATS_NO_MEMORY,
    .code = 500,
    .description = "Out of memory",
};

static natsError _errorInvalidArg = {
    .status = NATS_INVALID_ARG,
    .code = 400,
    .description = "Invalid function argument",
};

static natsError _errorInvalidFormat = {
    .status = NATS_INVALID_ARG,
    .code = 400,
    .description = "Invalid error format string",
};

static natsError *knownErrors[] = {
    &_errorOutOfMemory,
    &_errorInvalidArg,
    &_errorInvalidFormat,
    NULL,
};

natsError *natsMicroserviceErrorOutOfMemory = &_errorOutOfMemory;
natsError *natsMicroserviceErrorInvalidArg = &_errorInvalidArg;

const char *
natsError_String(natsError *err, char *buf, int size)
{
    if (err == NULL || buf == NULL)
        return "";
    if (err->status == NATS_OK)
        snprintf(buf, size, "%d: %s", err->code, err->description);
    else
        snprintf(buf, size, "%d:%d: %s", err->status, err->code, err->description);
    return buf;
}

natsStatus
natsError_StatusCode(natsError *err)
{
    return (err != NULL) ? err->status : NATS_OK;
}

static natsError *
new_error(natsStatus s, int code, char *description)
{
    natsError *err = NULL;

    err = NATS_CALLOC(1, sizeof(natsError));
    if (err == NULL)
        return &_errorOutOfMemory;

    err->status = s;
    err->code = code;
    err->description = description;

    return err;
}

natsError *
nats_NewError(int code, const char *description)
{
    return nats_Errorf(NATS_ERR, description);
}

natsError *
nats_NewStatusError(natsStatus s)
{
    char *dup = NULL;

    if (s == NATS_OK)
        return NULL;

    dup = NATS_STRDUP(natsStatus_GetText(s));
    if (dup == NULL)
        return &_errorOutOfMemory;

    return new_error(s, 0, dup);
}

natsError *
natsError_Wrapf(natsError *err, const char *format, ...)
{
    va_list args;
    char *buf = NULL;
    int len1 = 0, len2 = 0;
    natsStatus s;
    int code;

    if (err == NULL)
        return NULL;
    if (nats_IsStringEmpty(format))
        return err;

    va_start(args, format);
    len1 = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (len1 < 0)
    {
        return &_errorInvalidFormat;
    }
    if (!nats_IsStringEmpty(err->description))
    {
        len2 = strlen(err->description) + 2; // ": "
    }
    buf = NATS_MALLOC(len1 + len2 + 1);
    if (buf == NULL)
    {
        return &_errorOutOfMemory;
    }

    va_start(args, format);
    vsnprintf(buf, len1 + 1, format, args);
    va_end(args);
    if (!nats_IsStringEmpty(err->description))
    {
        buf[len1] = ':';
        buf[len1 + 1] = ' ';
        memcpy(buf + len1 + 2, err->description, len2 - 2);
    }
    buf[len1 + len2] = '\0';

    code = err->code;
    s = err->status;
    natsError_Destroy(err);
    return new_error(s, code, buf);
}

natsError *
nats_Errorf(int code, const char *format, ...)
{
    va_list args1, args2;
    char *buf = NULL;
    int len = 0;

    if ((code == 0) && nats_IsStringEmpty(format))
        return NULL;

    va_start(args1, format);
    va_copy(args2, args1);

    len = vsnprintf(NULL, 0, format, args1);
    va_end(args1);
    if (len < 0)
    {
        va_end(args2);
        return &_errorInvalidFormat;
    }
    buf = NATS_MALLOC(len + 1);
    if (buf == NULL)
    {
        va_end(args2);
        return &_errorOutOfMemory;
    }

    vsnprintf(buf, len + 1, format, args2);
    va_end(args2);

    return new_error(NATS_ERR, code, buf);
}

void natsError_Destroy(natsError *err)
{
    int i;

    if (err == NULL)
        return;

    for (i = 0; knownErrors[i] != NULL; i++)
    {
        if (err == knownErrors[i])
            return;
    }

    // description is declared const for the users, but is strdup-ed on
    // creation.
    NATS_FREE((void *)err->description);
    NATS_FREE(err);
}

natsError *
nats_IsErrorResponse(natsStatus status, natsMsg *msg)
{
    natsError *err = NULL;
    const char *c = NULL, *d = NULL;
    bool is_error;

    if (msg != NULL)
    {
        natsMsgHeader_Get(msg, NATS_MICROSERVICE_ERROR_CODE_HDR, &c);
        natsMsgHeader_Get(msg, NATS_MICROSERVICE_ERROR_HDR, &d);
    }

    is_error = (status != NATS_OK) || !nats_IsStringEmpty(c) || !nats_IsStringEmpty(d);
    if (!is_error)
        return NULL;

    err = natsError_Wrapf(nats_NewStatusError(status), d);
    if (!nats_IsStringEmpty(c) && (err != NULL))
    {
        err->code = atoi(c);
    }
    return err;
}
