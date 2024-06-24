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

#include "url.h"

bool natsUrl_IsLocalhost(natsUrl *url)
{
    return ((strcasecmp(url->host, "localhost") == 0) ||
            (strcasecmp(url->host, "127.0.0.1") == 0) ||
            (strcasecmp(url->host, "::1") == 0));
}

static natsStatus
_parsePort(int *port, const char *sport)
{
    natsStatus s = NATS_OK;
    int64_t n = 0;

    n = nats_ParseInt64(sport, strlen(sport));
    if ((n < 0) || (n > INT32_MAX))
        s = nats_setError(NATS_INVALID_ARG, "invalid port '%s'", sport);
    else
        *port = (int)n;

    return s;
}

natsStatus
natsUrl_Create(natsUrl **newUrl, natsPool *pool, const char *urlStr)
{
    natsStatus s = NATS_OK;
    char *copy = NULL;
    char *ptr = NULL;
    const char *scheme = NULL;
    const char *user = NULL;
    const char *pwd = NULL;
    const char *host = NULL;
    const char *port = NULL;
    const char *path = NULL;
    natsUrl *url = NULL;

    if (nats_isCStringEmpty(urlStr))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = CHECK_NO_MEMORY(url = nats_palloc( pool, sizeof(natsUrl)));
    IFOK(s, nats_Trim(&copy, pool, urlStr));

    // Scheme
    if (STILL_OK(s))
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
            scheme = (const char *)copy;
            ptr += 3;
        }
    }
    // User info
    if (STILL_OK(s))
    {
        char *sep = strrchr(ptr, '@');

        if (sep != NULL)
        {
            host = (const char *)(sep + 1);
            *sep = '\0';

            if (ptr != sep)
            {
                sep = strchr(ptr, ':');
                if (sep != NULL)
                {
                    *sep = '\0';
                    if (sep != ptr)
                        user = (const char *)ptr;
                    if (sep + 1 != host)
                        pwd = (const char *)(sep + 1);
                }
                else
                {
                    user = (const char *)ptr;
                }
            }
        }
        else
        {
            host = (const char *)ptr;
        }
    }
    // Host
    if (STILL_OK(s))
    {
        // Search for end of IPv6 address (if applicable)
        ptr = strrchr(host, ']');
        if (ptr == NULL)
            ptr = (char *)host;

        // From that point, search for the last ':' character
        ptr = strrchr(ptr, ':');
        if (ptr != NULL)
        {
            *ptr = '\0';
            port = (const char *)(ptr + 1);
        }
        if (nats_isCStringEmpty(host))
            host = "localhost";
    }
    // Port
    if (STILL_OK(s))
    {
        if (port != NULL)
        {
            char *sep = strchr(port, '/');

            if (sep != NULL)
            {
                *sep = '\0';
                path = (const char *)(sep + 1);
            }
        }
        if (nats_isCStringEmpty(port))
            url->port = 4222;
        else
            s = _parsePort(&url->port, port);
    }
    // Assemble everything
    if (STILL_OK(s))
    {
        const char *userval = (nats_isCStringEmpty(user) ? "" : user);
        const char *usep = (nats_isCStringEmpty(pwd) ? "" : ":");
        const char *pwdval = (nats_isCStringEmpty(pwd) ? "" : pwd);
        const char *hsep = (nats_isCStringEmpty(user) ? "" : "@");
        const char *pathsep = (nats_isCStringEmpty(path) ? "" : "/");
        const char *pathval = (nats_isCStringEmpty(path) ? "" : path);

        IFOK(s, ALWAYS_OK(url->host = nats_pstrdupC(pool, host)));
        IFOK(s, ALWAYS_OK(url->username = nats_pstrdupC(pool, user)));
        IFOK(s, ALWAYS_OK(url->password = nats_pstrdupC(pool, pwd)));

        if (STILL_OK(s))
        {
            size_t need = 0;
            size_t cap = 0;
            char *buf = NULL;
            for (int i = 0; i < 2; i++)
            {
                if (need != 0)
                {
                    buf = nats_palloc(pool, need);
                    if (buf == NULL)
                        return nats_setDefaultError(NATS_NO_MEMORY);
                    cap = need;
                }
                need = snprintf(buf, cap,
                                "%s://%s%s%s%s%s:%d%s%s",
                                scheme, userval, usep, pwdval, hsep, host, url->port, pathsep, pathval);
                need++; // For the '\0'
            }
            url->fullUrl = buf;
        }
        else
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    if (STILL_OK(s))
        *newUrl = url;

    return NATS_UPDATE_ERR_STACK(s);
}
