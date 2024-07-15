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
#include "util.h"

#include <string.h>
#include <ctype.h>

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
_parsePort(int *port, const char *sport)
{
    natsStatus  s    = NATS_OK;
    int64_t     n    = 0;

    n = nats_ParseInt64(sport, (int) strlen(sport));
    if ((n < 0) || (n > INT32_MAX))
        s = nats_setError(NATS_INVALID_ARG, "invalid port '%s'", sport);
    else
        *port = (int) n;

    return s;
}

static natsStatus _decodeAndDup(char **decoded, const char *encoded)
{
    size_t len = strlen(encoded);
    const char *p = encoded;
    const char *e = encoded + len;
    char *d;

    *decoded = NATS_MALLOC(len + 1);
    if (*decoded == NULL)
    {
        return nats_setDefaultError(NATS_NO_MEMORY);
    }
    d = *decoded;
    for (; p < e; p++)
    {
        if (*p != '%')
        {
            *d++ = *p;
            continue;
        }

        if (e - p < 3 || (!isxdigit(*(p + 1)) || !isxdigit(*(p + 2))))
        {
            NATS_FREE(*decoded);
            *decoded = NULL;
            return nats_setError(NATS_ERR, "invalid percent encoding in URL: %s", encoded);
        }

        char buf[3] = {p[1], p[2], '\0'};
        *d++ = (char)strtol(buf, NULL, 16);
        p += 2;
    }
    *d = '\0';
    return NATS_OK;
}

natsStatus
natsUrl_Create(natsUrl **newUrl, const char *urlStr)
{
    natsStatus  s      = NATS_OK;
    char        *copy  = NULL;
    char        *ptr   = NULL;
    const char  *scheme= NULL;
    const char  *user  = NULL;
    const char  *pwd   = NULL;
    const char  *host  = NULL;
    const char  *port  = NULL;
    const char  *path  = NULL;
    natsUrl     *url   = NULL;

    if (nats_IsStringEmpty(urlStr))
        return nats_setDefaultError(NATS_INVALID_ARG);

    url = (natsUrl*) NATS_CALLOC(1, sizeof(natsUrl));
    if (url == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_Trim(&copy, urlStr);

    // Scheme
    if (s == NATS_OK)
    {
        ptr = strstr(copy, "://");
        if (ptr == NULL)
        {
            scheme = "nats";
            ptr = copy;
        }
        else
        {
            *ptr = '\0';
            scheme = (const char*) copy;
            ptr += 3;
        }
    }
    // User info
    if (s == NATS_OK)
    {
        char *sep  = strrchr(ptr, '@');

        if (sep != NULL)
        {
            host = (const char*) (sep+1);
            *sep = '\0';

            if (ptr != sep)
            {
                sep = strchr(ptr, ':');
                if (sep != NULL)
                {
                    *sep = '\0';
                    if (sep != ptr)
                        user = (const char*) ptr;
                    if (sep+1 != host)
                        pwd = (const char*) (sep+1);
                }
                else
                {
                    user = (const char*) ptr;
                }
            }
        }
        else
        {
            host = (const char*) ptr;
        }
    }
    // Host
    if (s == NATS_OK)
    {
        // Search for end of IPv6 address (if applicable)
        ptr = strrchr(host, ']');
        if (ptr == NULL)
            ptr = (char*) host;

        // From that point, search for the last ':' character
        ptr = strrchr(ptr, ':');
        if (ptr != NULL)
        {
            *ptr = '\0';
            port = (const char*) (ptr+1);
        }
        if (nats_IsStringEmpty(host))
            host = "localhost";
    }
    // Port
    if (s == NATS_OK)
    {
        if (port != NULL)
        {
            char *sep = strchr(port, '/');

            if (sep != NULL)
            {
                *sep = '\0';
                path = (const char*) (sep+1);
            }
        }
        if (nats_IsStringEmpty(port))
            url->port = 4222;
        else
            s = _parsePort(&url->port, port);
    }
    // Assemble everything
    if (s == NATS_OK)
    {
        const char  *userval    = (nats_IsStringEmpty(user) ? "" : user);
        const char  *usep       = (nats_IsStringEmpty(pwd) ? "" : ":");
        const char  *pwdval     = (nats_IsStringEmpty(pwd) ? "" : pwd);
        const char  *hsep       = (nats_IsStringEmpty(user) ? "" : "@");
        const char  *pathsep    = (nats_IsStringEmpty(path) ? "" : "/");
        const char  *pathval    = (nats_IsStringEmpty(path) ? "" : path);

        DUP_STRING(s, url->host, host);

        if (user != NULL)
            IFOK(s, _decodeAndDup(&url->username, user));
        if (pwd != NULL)
            IFOK(s, _decodeAndDup(&url->password, pwd));

        if ((s == NATS_OK) && nats_asprintf(&url->fullUrl, "%s://%s%s%s%s%s:%d%s%s",
                scheme, userval, usep, pwdval, hsep, host, url->port, pathsep, pathval) < 0)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
    }

    NATS_FREE(copy);

    if (s == NATS_OK)
        *newUrl = url;
    else
        natsUrl_Destroy(url);

    return NATS_UPDATE_ERR_STACK(s);
}
