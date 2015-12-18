// Copyright 2015 Apcera Inc. All rights reserved.

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

    url->fullUrl = NATS_STRDUP(urlStr);
    if (url->fullUrl == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (s == NATS_OK)
    {
        copy = NATS_STRDUP(urlStr);
        if (copy == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
    }

    // This is based on the results of the Go parsing.
    // If '//' is not present, there is no error, but
    // no host, user, password is returned.
    if ((s == NATS_OK)
        && ((ptr = strstr(copy, "//")) != NULL)
        && (*(ptr += 2) != '\0'))
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
