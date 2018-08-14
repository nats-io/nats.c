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

#include "natsp.h"

#include <string.h>

#include "mem.h"

void
natsUrl_Destroy(natsUrl *url)
{
    if (url == NULL)
        return;

    NATS_FREE(url->fullUrl);
    NATS_FREE(url->host);
    NATS_FREE(url->username);
    NATS_FREE(url->password);
    NATS_FREE(url);
}

static natsStatus
_parseHostAndPort(natsUrl *url, char *host, bool uInfo)
{
    natsStatus  s      = NATS_OK;
    char        *sport = NULL;

    sport = strrchr(host, ':');
    if (sport != NULL)
    {
        if (sport[1] != '\0')
            url->port = atoi(sport + 1);

        *sport = '\0';
    }

    if (uInfo)
    {
        url->host = NATS_STRDUP(host + 1);
        *host = '\0';

        if (url->host == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    else
    {
        if (*host == '\0')
            url->host = NATS_STRDUP("localhost");
        else
            url->host = NATS_STRDUP(host);
        if (url->host == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    return s;
}

natsStatus
natsUrl_Create(natsUrl **newUrl, const char *urlStr)
{
    natsStatus  s      = NATS_OK;
    bool        uInfo  = false;
    char        *copy  = NULL;
    char        *ptr   = NULL;
    char        *host  = NULL;
    char        *pwd   = NULL;
    natsUrl     *url   = NULL;

    if ((urlStr == NULL) || (strlen(urlStr) == 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    url = (natsUrl*) NATS_CALLOC(1, sizeof(natsUrl));
    if (url == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    copy = NATS_STRDUP(urlStr);
    if (copy == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    // Add scheme if missing.
    if ((s == NATS_OK) && (strstr(copy, "://") == NULL))
    {
        char *str = NULL;

        if (nats_asprintf(&str, "nats://%s", copy) == -1)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            NATS_FREE(copy);
            copy = str;
        }
    }
    // Add default host/port if missing.
    if (s == NATS_OK)
    {
        char *start = NULL;

        // Now that we know that there is a scheme, move past it.
        start = strstr(copy, "://");
        start += 3;

        // If we are at then end, will add "localhost:4222".
        if (*start != '\0')
        {
            // First, search for end of IPv6 address (if applicable)
            ptr = strrchr(start, ']');
            if (ptr == NULL)
                ptr = start;

            // From that point, search for the last ':' character
            ptr = strrchr(ptr, ':');
        }
        if ((*start == '\0') || (ptr == NULL) || (*(ptr+1) == '\0'))
        {
            char        *str = NULL;
            int         res  = 0;
            const char  *fmt = NULL;

            if (*start == '\0')
                fmt = "%slocalhost:%s";
            else if (ptr != NULL)
                fmt = "%s%s";
            else
                fmt = "%s:%s";

            res = nats_asprintf(&str, fmt, copy, DEFAULT_PORT_STRING);
            if (res == -1)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                NATS_FREE(copy);
                copy = str;
            }
        }
    }

    // Keep a copy of the full URL.
    if (s == NATS_OK)
    {
        url->fullUrl = NATS_STRDUP(copy);
        if (url->fullUrl == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    // At this point, we are guaranteed to have '://' in the string
    if ((s == NATS_OK)
            && ((ptr = strstr(copy, "://")) != NULL)
            && (*(ptr += 3) != '\0'))
    {
        // If '@' is present, everything after is the host
        // everything before is username/password combo.

        host = strrchr(ptr, '@');
        if (host != NULL)
            uInfo = true;
        else
            host = ptr;

        if ((host != NULL) && (host[1] != '\0'))
        {
            s = _parseHostAndPort(url, host, uInfo);
        }

        if ((s == NATS_OK) && uInfo)
        {
            pwd = strchr(ptr, ':');
            if ((pwd != NULL) && (pwd[1] != '\0'))
            {
                url->password = NATS_STRDUP(pwd + 1);
                *pwd = '\0';

                if (url->password == NULL)
                    return nats_setDefaultError(NATS_NO_MEMORY);
            }

            if ((s == NATS_OK) && (strlen(ptr) > 0))
            {
                url->username = NATS_STRDUP(ptr);
                if (url->username == NULL)
                    return nats_setDefaultError(NATS_NO_MEMORY);
            }
        }
    }

    NATS_FREE(copy);

    if (s == NATS_OK)
        *newUrl = url;
    else
        natsUrl_Destroy(url);

    return NATS_UPDATE_ERR_STACK(s);
}
