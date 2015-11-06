// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"

#include "../mem.h"

int
nats_asprintf(char **newStr, const char *fmt, ...)
{
    char    tmp[256];
    char    *str;
    int     n, size;
    va_list ap;

    size = sizeof(tmp);
    str  = (char*) tmp;

    do
    {
        va_start(ap, fmt);
        n = vsnprintf(str, size, fmt, ap);
        va_end(ap);

        if ((n < 0) || (n >= size))
        {
            // We failed, but we don't know how much we need, so start with
            // doubling the size and see if it's better.
            if (n < 0)
            {
                size *= 2;
            }
            else
            {
                // We know exactly how much we need.
                size = (n + 1);

                // now set n to -1 so that we loop again.
                n = -1;
            }

            if (str != tmp)
            {
                char *realloced = NULL;

                realloced = NATS_REALLOC(str, size);
                if (realloced == NULL)
                {
                    NATS_FREE(str);
                    str = NULL;
                }
                else
                {
                    str = realloced;
                }
            }
            else
            {
                str = NATS_MALLOC(size);
            }
        }
    }
    while ((n < 0) && (str != NULL));

    if (str != NULL)
    {
        if (str != tmp)
            *newStr = str;
        else
            *newStr = NATS_STRDUP(str);

        if (*newStr == NULL)
            n = -1;
    }

    return n;
}

char*
nats_strcasestr(const char *haystack, const char *needle)
{
    char *lowHaystack = NATS_STRDUP(haystack);
    char *lowNeedle   = NATS_STRDUP(needle);
    char *res         = NULL;
    int  offset       = 0;

    if ((lowHaystack == NULL) || (lowNeedle == NULL))
    {
        NATS_FREE(lowHaystack);
        NATS_FREE(lowNeedle);
        return NULL;
    }

    _strlwr(lowHaystack);
    _strlwr(lowNeedle);

    res = strstr((const char*) lowHaystack, (const char*) lowNeedle);
    if (res != NULL)
    {
        offset = (int) (res - lowHaystack);
        res = (char *) (haystack + offset);
    }

    NATS_FREE(lowHaystack);
    NATS_FREE(lowNeedle);

    return res;
}

