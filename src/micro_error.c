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

#include <stdarg.h>

#include "microp.h"

static microError _errorOutOfMemory = {
    .status = NATS_NO_MEMORY,
    .message = (char *)"out of memory",
};

static microError _errorInvalidArg = {
    .status = NATS_INVALID_ARG,
    .message = (char *)"invalid function argument",
};

static microError _errorInvalidFormat = {
    .status = NATS_INVALID_ARG,
    .message = (char *)"invalid format string",
};

static microError *knownErrors[] = {
    &_errorOutOfMemory,
    &_errorInvalidArg,
    &_errorInvalidFormat,
    NULL,
};

microError *micro_ErrorOutOfMemory = &_errorOutOfMemory;
microError *micro_ErrorInvalidArg = &_errorInvalidArg;

static microError *
verrorf(natsStatus s, int code, const char *format, va_list args)
{
    microError *err = NULL;
    int message_len = 0;

    va_list args2;
    va_copy(args2, args);

    if (format == NULL)
        format = "";

    message_len = vsnprintf(NULL, 0, format, args);
    if (message_len < 0)
    {
        va_end(args2);
        return &_errorInvalidFormat;
    }

    err = NATS_CALLOC(1, sizeof(microError) + message_len + 1);
    if (err == NULL)
    {
        va_end(args2);
        return &_errorOutOfMemory;
    }

    err->code = code;
    err->status = s;
    err->message = (char *)(err + 1);
    vsnprintf(err->message, message_len + 1, format, args2);
    va_end(args2);
    return err;
}

microError *
micro_Errorf(const char *format, ...)
{
    microError *err = NULL;
    va_list args;

    va_start(args, format);
    err = verrorf(NATS_OK, 0, format, args);
    va_end(args);
    return err;
}

microError *
micro_ErrorfCode(int code, const char *format, ...)
{
    microError *err = NULL;
    va_list args;

    va_start(args, format);
    err = verrorf(NATS_OK, code, format, args);
    va_end(args);
    return err;
}

microError *
micro_ErrorFromStatus(natsStatus s)
{
    microError *err = NULL;
    const char *message = natsStatus_GetText(s);
    size_t message_len = strlen(message);

    if (s == NATS_OK)
        return NULL;

    err = NATS_CALLOC(1, sizeof(microError) + message_len + 1);
    if (err == NULL)
        return &_errorOutOfMemory;

    err->status = s;
    err->message = (char *)(err + 1);
    memcpy(err->message, message, message_len + 1);
    return err;
}

microError *
micro_is_error_message(natsStatus status, natsMsg *msg)
{
    microError *err = NULL;
    const char *c = NULL, *d = NULL;
    bool is_service_error;
    bool is_nats_error = (status != NATS_OK);
    int code = 0;

    if (msg != NULL)
    {
        natsMsgHeader_Get(msg, MICRO_ERROR_CODE_HDR, &c);
        natsMsgHeader_Get(msg, MICRO_ERROR_HDR, &d);
    }
    if (!nats_IsStringEmpty(c))
    {
        code = atoi(c);
    }
    is_service_error = (code != 0) || !nats_IsStringEmpty(d);
    
    if (is_service_error && !is_nats_error)
    {
        return micro_ErrorfCode(code, d);
    }
    else if (!is_service_error && is_nats_error)
    {
        return micro_ErrorFromStatus(status);
    }
    else if (is_service_error && is_nats_error)
    {
        err = microError_Wrapf(micro_ErrorFromStatus(status), d);
        err->code = code;
        return err;
    }

    return NULL;
}

microError *
microError_Wrapf(microError *err, const char *format, ...)
{
    va_list args;
    microError *new_err = NULL;

    if (err == NULL)
        return NULL;

    va_start(args, format);
    new_err = verrorf(NATS_OK, 0, format, args);
    va_end(args);

    new_err->cause = err;
    return new_err;
}

const char *
microError_String(microError *err, char *buf, int size)
{
    size_t used = 0;
    const char *caused;

    if (buf == NULL)
        return "";
    if (err == NULL)
    {
        snprintf(buf, size, "null");
        return buf;
    }

    if (err->status != NATS_OK)
    {
        used += snprintf(buf + used, size - used, "status %d: ", err->status);
    }
    if (err->code != 0)
    {
        used += snprintf(buf + used, size - used, "code %d: ", err->code);
    }
    used += snprintf(buf + used, size - used, "%s", err->message);

    if (err->cause != NULL)
    {
        used += snprintf(buf + used, size - used, ": ");
        caused = microError_String(err->cause, buf + used, size - used);
        used += strlen(caused);
    }
    return buf;
}

natsStatus
microError_Status(microError *err)
{
    if (err == NULL)
        return NATS_OK;

    if (err->status != NATS_OK)
        return err->status;

    return microError_Status(err->cause);
}

void microError_Destroy(microError *err)
{
    int i;

    if (err == NULL)
        return;

    for (i = 0; knownErrors[i] != NULL; i++)
    {
        if (err == knownErrors[i])
            return;
    }

    microError_Destroy(err->cause);
    NATS_FREE(err);
}
