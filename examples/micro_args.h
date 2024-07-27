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

#ifndef MICRO_ARGS_H_
#define MICRO_ARGS_H_

/**
 * Request unmarshaled as "arguments", a space-separated list of numbers and strings.
 * TODO document the interface.
 */
typedef struct args_s microArgs;

struct args_s
{
    void **args;
    int count;
};


static inline microError *new_args(microArgs **ptr, int n)
{
    *ptr = calloc(1, sizeof(microArgs));
    if (*ptr == NULL)
        return micro_ErrorOutOfMemory;

    (*ptr)->args = calloc(n, sizeof(void *));
    if ((*ptr)->args == NULL)
    {
        free(*ptr);
        return micro_ErrorOutOfMemory;
    }

    (*ptr)->count = n;
    return NULL;
}

typedef enum parserState
{
    NewArg = 0,
    NumberArg,
} parserState;

// decodes the rest of a string into a pre-allocated buffer of sufficient
// length, or just calculates the needed buffer size. The opening quote must
// have been already processed by the caller (parse).
static microError *
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
        return micro_Errorf("a quoted string is not properly terminated");
    }

    *decoded_len = len;
    return NULL;
}

static microError *
decode_and_dupe_rest_of_string(char **dup, int *i, const char *data, int data_len)
{
    microError *err = NULL;
    int startPos = *i;
    int decoded_len = 0;

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

    *i = startPos;

    *dup = calloc(decoded_len + 1, sizeof(char));
    if (*dup == NULL)
    {
        return micro_ErrorOutOfMemory;
    }

    // no need to check for error the 2nd time, we already know the string is
    // valid.
    decode_rest_of_string(*dup, &decoded_len, i, data, data_len);
    (*dup)[decoded_len] = 0;
    return NULL;
}

static microError *
parse(void **args, int *args_len, const char *data, int data_len)
{
    int i = 0;
    microError *err = NULL;
    char c;
    int n = 0;
    parserState state = NewArg;
    char numbuf[64];
    int num_len = 0;
    bool is_float = false;

#define EOS 0
    for (; i < data_len + 1;)
    {
        c = (i < data_len) ? data[i] : EOS;

        switch (state)
        {
        case NewArg:
            switch (c)
            {
            case EOS:
            case ' ':
                i++;
                break;

            case '"':
                i++; // consume the opening quote.
                err = decode_and_dupe_rest_of_string((char **)(&args[n]), &i, data, data_len);
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
                i++;
                break;

            default:
                return micro_Errorf("unexpected '%c', an argument must be a number or a quoted string", c);
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
                i++;
                break;

            case EOS:
            case ' ':
                if (args != NULL)
                {
                    numbuf[num_len] = 0;
                    if (is_float)
                    {
                        args[n] = calloc(1, sizeof(long double));
                        if (args[n] == NULL)
                        {
                            return micro_ErrorOutOfMemory;
                        }
                        *(long double *)args[n] = strtold(numbuf, NULL);
                    }
                    else
                    {
                        args[n] = calloc(1, sizeof(int));
                        if (args[n] == NULL)
                        {
                            return micro_ErrorOutOfMemory;
                        }
                        *(int *)args[n] = atoi(numbuf);
                    }
                }
                n++;
                i++;
                state = NewArg;
                break;

            default:
                return micro_Errorf("unexpected '%c', a number must be followed by a space", c);
            }
            break;

        default:
            return micro_Errorf("unreachable: wrong state for a ' ', expected NewArg or NumberArg, got %d", state);
        }
    }

    *args_len = n;
    return NULL;
}

static inline void microArgs_Destroy(microArgs *args)
{
    int i;

    if (args == NULL)
        return;

    for (i = 0; i < args->count; i++)
    {
        free(args->args[i]);
    }
    free(args->args);
    free(args);
}

static microError *
micro_ParseArgs(microArgs **ptr, const char *data, int data_len)
{
    microError *err = NULL;
    microArgs *args = NULL;
    int n = 0;

    if ((ptr == NULL) || (data == NULL) || (data_len < 0))
        return microError_Wrapf(micro_ErrorInvalidArg, "failed to parse args");

    if (err == NULL)
        err = parse(NULL, &n, data, data_len);
    if (err == NULL)
        err = new_args(&args, n);
    if (err == NULL)
        err = parse(args->args, &n, data, data_len);

    if (err != NULL)
    {
        microArgs_Destroy(args);
        return microError_Wrapf(err, "failed to parse args");
    }
    *ptr = args;
    return NULL;
}

static inline int microArgs_Count(microArgs *args)
{
    if (args == NULL)
        return 0;

    return args->count;
}

static inline microError *
microArgs_GetInt(int *val, microArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return micro_ErrorInvalidArg;

    *val = *((int *)args->args[index]);
    return NULL;
}

static inline microError *
microArgs_GetFloat(long double *val, microArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return micro_ErrorInvalidArg;

    *val = *((long double *)args->args[index]);
    return NULL;
}

static inline microError *
microArgs_GetString(const char **val, microArgs *args, int index)
{
    if ((args == NULL) || (index < 0) || (index >= args->count) || (val == NULL))
        return micro_ErrorInvalidArg;

    *val = (const char *)args->args[index];
    return NULL;
}


#endif /* MICRO_H_ */
