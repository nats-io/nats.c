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

static natsMicroserviceError _errorOutOfMemory = {
    .status = NATS_NO_MEMORY,
    .code = 500,
    .description = "Out of memory",
};

static natsMicroserviceError _errorInvalidArg = {
        .status = NATS_INVALID_ARG,
        .code = 400,
        .description = "Invalid function argument",
};

static natsMicroserviceError *knownErrors[] = {
    &_errorOutOfMemory,
    &_errorInvalidArg,
    NULL,
};

natsMicroserviceError *natsMicroserviceErrorOutOfMemory = &_errorOutOfMemory;
natsMicroserviceError *natsMicroserviceErrorInvalidArg = &_errorInvalidArg;

static const char *
_string(natsMicroserviceError *err, char *buf, int size) {
    if (err == NULL || buf == NULL)
        return "";
    if (err->status == NATS_OK)
        snprintf(buf, size, "%d: %s", err->code, err->description);
    else
        snprintf(buf, size, "%d:%d: %s", err->status, err->code, err->description);
    return buf;
}

natsMicroserviceError *
nats_NewMicroserviceError(natsStatus s, int code, const char *description)
{
    natsMicroserviceError *err = NULL;

    if (s == NATS_OK)
        return NULL;

    err = NATS_CALLOC(1, sizeof(natsMicroserviceError));
    if (err == NULL)
        return &_errorOutOfMemory;

    err->status = s;
    err->code = code;
    err->description = NATS_STRDUP(description); // it's ok if NULL
    err->String = _string;
    return err;
}

void natsMicroserviceError_Destroy(natsMicroserviceError *err)
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

natsMicroserviceError *
nats_MicroserviceErrorFromMsg(natsStatus status, natsMsg *msg)
{
    natsMicroserviceError *err = NULL;
    const char *c = NULL, *d = NULL;
    bool is_error;
    int code = 0;

    if (msg == NULL)
        return NULL;

    natsMsgHeader_Get(msg, NATS_MICROSERVICE_ERROR_CODE_HDR, &c);
    natsMsgHeader_Get(msg, NATS_MICROSERVICE_ERROR_HDR, &d);

    is_error = (status != NATS_OK) || !nats_IsStringEmpty(c) || !nats_IsStringEmpty(d);
    if (!is_error)
        return NULL;

    err = nats_NewMicroserviceError(status, 0, "");
    if (err == NULL)
    {
        // This is not 100% correct - returning an OOM error that was not in the
        // message, but since it is usually a fatal condition, it is ok.
        return &_errorOutOfMemory;
    }

    if (nats_IsStringEmpty(d) && (status != NATS_OK))
    {
        d = natsStatus_GetText(status);
        if (d == NULL)
            d = "";
    }

    if (!nats_IsStringEmpty(c))
    {
        code = atoi(c);
    }
    err->status = status;
    err->code = code;
    err->description = d;

    return err;
}
