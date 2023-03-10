// Copyright 2021-2023 The NATS Authors
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

static natsMicroserviceError *parse(void **args, int *args_len, int *i, const char *data, int data_len);

natsMicroserviceError *
natsParseAsArgs(natsArgs **new_args, const char *data, int data_len)
{
    natsMicroserviceError *err = NULL;
    int n;
    int i = 0;
    natsArgs *args = NULL;

    if ((new_args == NULL) || (data == NULL) || (data_len < 0))
        return nats_NewMicroserviceError(NATS_INVALID_ARG, 500, "Invalid function argument");

    // parse the number of arguments without allocating.
    err = parse(NULL, &n, &i, data, data_len);
    if (err == NULL)
    {
        args = NATS_CALLOC(1, sizeof(natsArgs));
        if (args == NULL)
            err = natsMicroserviceErrorOutOfMemory;
    }
    if (err == NULL)
    {
        args->args = NATS_CALLOC(n, sizeof(void *));
        if (args == NULL)
            err = natsMicroserviceErrorOutOfMemory;
        else
            args->count = n;
    }
    if (err == NULL)
    {
        i = 0;
        err = parse(args->args, &n, &i, data, data_len);
    }

    if (err == NULL)
    {
        *new_args = args;
    }
    else
    {
        natsArgs_Destroy(args);
    }

    return err;
}

void natsArgs_Destroy(natsArgs *args)
{
    int i;

    if (args == NULL)
        return;

    for (i = 0; i < args->count; i++)
    {
        NATS_FREE(args->args[i]);
    }
    NATS_FREE(args->args);
    NATS_FREE(args);
}

int natsArgs_Count(natsArgs *args)
{
    if (args == NULL)
        return 0;

    return args->count;
}

natsMicroserviceError *
natsArgs_GetInt(int *val, natsArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return natsMicroserviceErrorInvalidArg;

    *val = *((int *)args->args[index]);
    return NULL;
}

natsMicroserviceError *
natsArgs_GetFloat(long double *val, natsArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return natsMicroserviceErrorInvalidArg;

    *val = *((long double *)args->args[index]);
    return NULL;
}

natsMicroserviceError *
natsArgs_GetString(const char **val, natsArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return natsMicroserviceErrorInvalidArg;

    *val = (const char *)args->args[index];
    return NULL;
}

/// @brief decodes the rest of a string into a pre-allocated buffer of
/// sufficient length, or just calculates the needed buffer size. The opening
/// quote must have been already processed by the caller (parse).
///
/// @param dup receives the parsed string, must be freed by the caller. pass NULL to just get the length.
/// @param data raw message data
/// @param data_len length of data
/// @return error in case the string is not properly terminated.
static natsMicroserviceError *
decode_rest_of_string(char *dup, int *decoded_len, int *i, const char *data, int data_len)
{
    char c;
    int len = 0;
    bool terminated = false;
    bool escape = false;

    for (; !terminated && *i < data_len; (*i)++)
    {
        c = data[*i];
        switch (c)
        {
        case '"':
            if (escape)
            {
                // include the escaped quote.
                if (dup != NULL)
                {
                    dup[len] = c;
                }
                len++;
                escape = false;
            }
            else
            {
                // end of quoted string.
                terminated = true;
            }
            break;

        case '\\':
            if (!escape)
            {
                escape = true;
            }
            else
            {
                // include the escaped backslash.
                if (dup != NULL)
                {
                    dup[len] = c;
                }
                len++;
                escape = false;
            }
            break;

        default:
            if (dup != NULL)
            {
                dup[len] = c;
            }
            len++;
            escape = false;
            break;
        }
    }
    if (!terminated)
    {
        nats_NewMicroserviceError(NATS_INVALID_ARG, 400, "a quoted string is not properly terminated");
    }

    *decoded_len = len;
    return NULL;
}

/// @brief decodes and duplicates the rest of a string, as the name says.
/// @param dup receives the parsed string, must be freed by the caller. pass
/// NULL to just get the length.
/// @param data raw message data
/// @param data_len length of data
/// @return error.
static natsMicroserviceError *
decode_and_dupe_rest_of_string(char **dup, int *i, const char *data, int data_len)
{
    natsMicroserviceError *err = NULL;
    int start = *i;
    int decoded_len;

    err = decode_rest_of_string(NULL, &decoded_len, i, data, data_len);
    if (err != NULL)
    {
        return err;
    }
    if (dup == NULL)
    {
        // nothing else to do - the string has been scanned and validated.
        return NULL;
    }

    *i = start;

    *dup = NATS_CALLOC(decoded_len + 1, sizeof(char));
    if (*dup == NULL)
    {
        return natsMicroserviceErrorOutOfMemory;
    }

    // no need to check for error the 2nd time, we already know the string is
    // valid.
    decode_rest_of_string(*dup, &decoded_len, i, data, data_len);
    (*dup)[decoded_len] = 0;
    return NULL;
}

typedef enum parserState
{
    NewArg = 0,
    NumberArg,
} parserState;

static natsMicroserviceError *
parse(void **args, int *args_len, int *i, const char *data, int data_len)
{
    natsMicroserviceError *err = NULL;
    char c;
    int n = 0;
    parserState state = NewArg;
    char errbuf[1024];
    char numbuf[64];
    int num_len = 0;
    bool is_float = false;

#define EOS 0
    for (; *i < data_len + 1;)
    {
        c = (*i < data_len) ? data[*i] : EOS;

        switch (state)
        {
        case NewArg:
            switch (c)
            {
            case EOS:
            case ' ':
                (*i)++;
                break;

            case '"':
                (*i)++; // consume the opening quote.
                err = decode_and_dupe_rest_of_string((char **)(&args[n]), i, data, data_len);
                if (err != NULL)
                {
                    return err;
                }
                n++;
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
            case '+':
            case '.':
                state = NumberArg;
                num_len = 0;
                numbuf[num_len++] = c;
                is_float = (c == '.');
                (*i)++;
                break;

            default:
                snprintf(errbuf, sizeof(errbuf), "unexpected '%c', an argument must be a number or a quoted string", c);
                return nats_NewMicroserviceError(NATS_ERR, 400, errbuf);
            }
            break;

        case NumberArg:
            switch (c)
            {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
            case '+':
            case '.':
            case 'e':
            case 'E':
            case ',':
                numbuf[num_len] = c;
                num_len++;
                is_float = is_float || (c == '.') || (c == 'e') || (c == 'E');
                (*i)++;
                break;

            case EOS:
            case ' ':
                if (args != NULL)
                {
                        numbuf[num_len] = 0;
                    if (is_float)
                    {
                        args[n] = NATS_CALLOC(1, sizeof(long double));
                        if (args[n] == NULL)
                        {
                            return natsMicroserviceErrorOutOfMemory;
                        }
                        *(long double *)args[n] = strtold(numbuf, NULL);
                    }
                    else
                    {
                        args[n] = NATS_CALLOC(1, sizeof(int));
                        if (args[n] == NULL)
                        {
                            return natsMicroserviceErrorOutOfMemory;
                        }
                        *(int *)args[n] = atoi(numbuf);
                    }
                }
                n++;
                (*i)++;
                state = NewArg;
                break;

            default:
                snprintf(errbuf, sizeof(errbuf), "unexpected '%c', a number must be followed by a space", c);
                return nats_NewMicroserviceError(NATS_ERR, 400, errbuf);
            }
            break;

        default:
            snprintf(errbuf, sizeof(errbuf), "unreachable: wrong state for a ' ', expected NewArg or NumberArg, got %d", state);
            return nats_NewMicroserviceError(NATS_ERR, 500, errbuf);
        }
    }

    *args_len = n;
    return NULL;
}
