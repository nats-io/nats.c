// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "mem.h"
#include "time.h"

#define ASCII_0 (48)
#define ASCII_9 (57)

// parseInt64 expects decimal positive numbers. We
// return -1 to signal error
int64_t
nats_ParseInt64(const char *d, int dLen)
{
    int     i;
    char    dec;
    int64_t n = 0;

    if (dLen == 0)
        return -1;

    for (i=0; i<dLen; i++)
    {
        dec = d[i];
        if ((dec < ASCII_0) || (dec > ASCII_9))
            return -1;

        n = (n * 10) + ((int64_t)dec - ASCII_0);
    }

    return n;
}

natsStatus
nats_ParseControl(natsControl *control, const char *line)
{
    natsStatus  s           = NATS_OK;
    char        *tok        = NULL;
    const char  *last       = line;
    int         len         = 0;
    int         i           = 0;

    if (line == NULL)
        return NATS_PROTOCOL_ERROR;

    while ((s == NATS_OK)
           && ((tok = strchr(last, (int) ' ')) != NULL))
    {
        len = (int)(tok - last);
        if (len <= 0)
        {
            s = NATS_PROTOCOL_ERROR;
            break;
        }

        if ((i == 0) || (i == 1))
        {
            char *tmp = NATS_MALLOC(len + 1);

            if (tmp == NULL)
                s = NATS_NO_MEMORY;

            if (s == NATS_OK)
            {
                memcpy(tmp, last, len);
                tmp[len] = '\0';

                if (i == 0)
                    control->op = tmp;
                else
                    control->args = tmp;
            }
        }

        if (i++ > 2)
            s = NATS_PROTOCOL_ERROR;

        last = (tok + 1);
    }

    if ((i < 1) || (i > 2) || (s != NATS_OK))
    {
        if (s == NATS_OK)
            s = NATS_PROTOCOL_ERROR;

        NATS_FREE(control->op);
        control->op = NULL;

        NATS_FREE(control->args);
        control->args = NULL;
    }

    return s;
}

natsStatus
nats_CreateStringFromBuffer(char **newStr, natsBuffer *buf)
{
    char    *str = NULL;
    int     len  = 0;

    if ((buf == NULL) || ((len = natsBuf_Len(buf)) == 0))
        return NATS_OK;

    str = NATS_MALLOC(len + 1);
    if (str == NULL)
        return NATS_NO_MEMORY;

    memcpy(str, natsBuf_Data(buf), len);
    str[len] = '\0';

    *newStr = str;

    return NATS_OK;
}

void
nats_Sleep(int64_t millisec)
{
    struct timeval tv;

    tv.tv_sec  = millisec / 1000;
    tv.tv_usec = (millisec % 1000) * 1000;

    select(0, NULL, NULL, NULL, &tv);
}

void
nats_Randomize(int *array, int arraySize)
{
    int i, j;

    srand(nats_NowInNanoSeconds());

    for (i = 0; i < arraySize; i++)
    {
        j = rand() % (i + 1);
        array[i] = array[j];
        array[j] = i;
    }
}

const char*
nats_GetBoolStr(bool value)
{
    if (value)
        return "true";

    return "false";
}

#ifdef _WIN32
char*
nats_asprintf(const char *fmt, ...)
{
    char    tmp[128];
    char    *str = NULL;
    int     n;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0)
        return NULL;

    if (n < (int) sizeof(tmp))
        return NATS_STRDUP(tmp);

    str = NATS_MALLOC(n + 1);
    if (str != NULL)
    {
        va_start(ap, fmt);
        n = vsnprintf(str, sizeof(str), fmt, ap);
        va_end(ap);

        if (n < 0)
        {
            NATS_FREE(str);
            str = NULL;
        }
    }

    return str;
}
#endif
