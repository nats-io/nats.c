// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "mem.h"

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
    int         len         = 0;

    if ((line == NULL) || (line[0] == '\0'))
        return nats_setDefaultError(NATS_PROTOCOL_ERROR);

    tok = strchr(line, (int) ' ');
    if (tok == NULL)
    {
        control->op = NATS_STRDUP(line);
        if (control->op == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);

        return NATS_OK;
    }

    len = (int) (tok - line);
    control->op = NATS_MALLOC(len + 1);
    if (control->op == NULL)
    {
        s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    else
    {
        memcpy(control->op, line, len);
        control->op[len] = '\0';
    }

    if (s == NATS_OK)
    {
        // Discard all spaces and the like in between the next token
        while ((tok[0] != '\0')
               && ((tok[0] == ' ')
                   || (tok[0] == '\r')
                   || (tok[0] == '\n')
                   || (tok[0] == '\t')))
        {
            tok++;
        }
    }

    // If there is a token...
    if (tok[0] != '\0')
    {
        char *tmp;

        len = (int) strlen(tok);
        tmp = &(tok[len - 1]);

        // Remove trailing spaces and the like.
        while ((tmp[0] != '\0')
                && ((tmp[0] == ' ')
                    || (tmp[0] == '\r')
                    || (tmp[0] == '\n')
                    || (tmp[0] == '\t')))
        {
            tmp--;
            len--;
        }

        // We are sure that len is > 0 because of the first while() loop.

        control->args = NATS_MALLOC(len + 1);
        if (control->args == NULL)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            memcpy(control->args, tok, len);
            control->args[len] = '\0';
        }
    }

    if (s != NATS_OK)
    {
        NATS_FREE(control->op);
        control->op = NULL;

        NATS_FREE(control->args);
        control->args = NULL;
    }

    return NATS_UPDATE_ERR_STACK(s);
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
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(str, natsBuf_Data(buf), len);
    str[len] = '\0';

    *newStr = str;

    return NATS_OK;
}

void
nats_Sleep(int64_t millisec)
{
#ifdef _WIN32
    Sleep((DWORD) millisec);
#else
    usleep(millisec * 1000);
#endif
}

void
nats_Randomize(int *array, int arraySize)
{
    int i, j;

    srand((unsigned int) nats_NowInNanoSeconds());

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

void
nats_NormalizeErr(char *error)
{
    int start = 0;
    int end   = 0;
    int len   = (int) strlen(error);
    int i;

    if (strncmp(error, _ERR_OP_, _ERR_OP_LEN_) == 0)
        start = _ERR_OP_LEN_;

    for (i=start; i<len; i++)
    {
        if ((error[i] != ' ') && (error[i] != '\''))
            break;
    }

    start = i;
    if (start == len)
    {
        error[0] = '\0';
        return;
    }

    for (end=len-1; end>0; end--)
        if ((error[end] != ' ') && (error[end] != '\''))
            break;

    if (end <= start)
    {
        error[0] = '\0';
        return;
    }

    len = end - start + 1;
    memmove(error, error + start, len);
    error[len] = '\0';
}
