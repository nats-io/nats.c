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

#include "../natsp.h"

#include "../mem.h"

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

#if _MSC_VER < 1900
int
nats_snprintf(char *buffer, size_t countszt, char *format, ...)
{
    int     count = (int) countszt;
    int     len   = 0;
    va_list ap;

    memset(buffer, 0, count);

    va_start(ap, format);
    len = (int) vsnprintf(buffer, count, format, ap);
    va_end(ap);
    if ((len == count) || (len < 0))
    {
        buffer[count-1] = '\0';
        len = count-1;
    }
    return len;
}
#endif
