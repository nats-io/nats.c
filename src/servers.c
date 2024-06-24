// Copyright 2015-2024 The NATS Authors
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

#include "hash.h"
#include "servers.h"
#include "conn.h"
#include "opts.h"

static natsStatus
_createSrv(natsServer **newSrv, natsPool *pool, char *url, bool implicit, const char *tlsName)
{
    natsStatus s = NATS_OK;
    natsServer *srv = nats_palloc(pool, sizeof(natsServer));
    if (srv == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    srv->isImplicit = implicit;

    s = natsUrl_Create(&(srv->url), pool, url);
    if ((STILL_OK(s)) && (tlsName != NULL))
    {
        srv->tlsName = nats_pstrdupC(pool, tlsName);
        if (srv->tlsName == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    if (STILL_OK(s))
        *newSrv = srv;

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_addURLToServers(natsServers *servers, char *sURL, bool implicit, const char *tlsName)
{
    natsStatus s;
    natsServer *srv = NULL;

    s = _createSrv(&srv, servers->pool, sURL, implicit, tlsName);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // For and explicit URL, we will save the user info if one is provided
    // and if not already done.
    if (!implicit && (servers->user == NULL) && (srv->url->username != NULL))
    {
        s = CHECK_NO_MEMORY(servers->user = nats_pstrdupC(servers->pool, srv->url->username));
        IFOK(s, ALWAYS_OK(servers->pwd = nats_pstrdupC(servers->pool, srv->url->password))); // password can be NULL
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    if (servers->size + 1 > servers->cap)
    {
        natsServer **newArray = NULL;
        int newCap = 2 * servers->cap;

        newArray = nats_palloc(servers->pool, newCap * sizeof(natsServer *));
        memcpy(newArray, servers->srvrs, servers->size * sizeof(*newArray));
        if (newArray == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (STILL_OK(s))
        {
            servers->cap = newCap;
            servers->srvrs = newArray;
        }
    }
    if (STILL_OK(s))
        servers->srvrs[servers->size++] = srv;

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_shuffleServers(natsServers *pool, int offset)
{
    int i, j;
    natsServer *tmp;

    if (pool->size <= offset + 1)
        return;

    srand((unsigned int)nats_nowInNanoSeconds());

    for (i = offset; i < pool->size; i++)
    {
        j = offset + rand() % (i + 1 - offset);
        tmp = pool->srvrs[i];
        pool->srvrs[i] = pool->srvrs[j];
        pool->srvrs[j] = tmp;
    }
}

// Create the list of servers using the options given.
// We will place a Url option first, followed by any
// Server Options. We will randomize the server pool unlesss
// the NoRandomize flag is set.
natsStatus
natsServers_Create(natsServers **newServers, natsPool *pool, natsOptions *opts)
{
    natsStatus s = NATS_OK;
    natsServers *servers = NULL;
    int i;
    int c;

    c = (opts->url != NULL ? 1 : 0);
    c += opts->serversCount;

    // If the pool is going to be empty, we will add the default URL.
    if (c == 0)
        c = 1;

    IFOK(s, CHECK_NO_MEMORY(servers = nats_palloc(pool, sizeof(natsServers))));
    IFOK(s, CHECK_NO_MEMORY(servers->srvrs = nats_palloc(pool, c * sizeof(natsServer *))));

    // Set the current capacity. The array of urls may have to grow in
    // the future.
    if (STILL_OK(s))
    {
        servers->pool = pool;
        servers->cap = c;
        servers->randomize = !opts->net.noRandomize;
    }

    // Add URLs from Options' Servers
    for (i = 0; (STILL_OK(s)) && (i < opts->serversCount); i++)
        s = _addURLToServers(servers, opts->servers[i], false, NULL);

    if (STILL_OK(s))
    {
        // Randomize if allowed to
        if (servers->randomize)
            _shuffleServers(servers, 0);
    }

    // Normally, if this one is set, Options.Servers should not be,
    // but we always allowed that, so continue to do so.
    if ((STILL_OK(s)) && (opts->url != NULL))
    {
        // Add to the end of the array
        s = _addURLToServers(servers, opts->url, false, NULL);
        if ((STILL_OK(s)) && (servers->size > 1))
        {
            // Then swap it with first to guarantee that Options.Url is tried first.
            natsServer *opstUrl = servers->srvrs[servers->size - 1];

            servers->srvrs[servers->size - 1] = servers->srvrs[0];
            servers->srvrs[0] = opstUrl;
        }
    }
    else if ((STILL_OK(s)) && (servers->size == 0))
    {
        // Place default URL if servers is empty.
        s = _addURLToServers(servers, (char *)NATS_DEFAULT_URL, false, NULL);
    }

    if (STILL_OK(s))
        *newServers = servers;

    return NATS_UPDATE_ERR_STACK(s);
}

natsServer *
natsServers_GetCurrentServer(natsServers *servers, const natsServer *cur, int *index)
{
    natsServer *s = NULL;
    int i;

    for (i = 0; i < servers->size; i++)
    {
        s = servers->srvrs[i];
        if (s == cur)
        {
            if (index != NULL)
                *index = i;

            return s;
        }
    }

    if (index != NULL)
        *index = -1;

    return NULL;
}

// Pop the current server and put onto the end of the list. Select head of list as long
// as number of reconnect attempts under MaxReconnect.
natsServer *
natsServers_GetNextServer(natsServers *servers, natsOptions *opts, const natsServer *cur)
{
    natsServer *s = NULL;
    int i, j;

    s = natsServers_GetCurrentServer(servers, cur, &i);
    if (i < 0)
        return NULL;

    // Shift left servers past current to the current's position
    for (j = i; j < servers->size - 1; j++)
        servers->srvrs[j] = servers->srvrs[j + 1];

    if ((opts->net.maxReconnect < 0) || (s->reconnects < opts->net.maxReconnect))
    {
        // Move the current server to the back of the list
        servers->srvrs[servers->size - 1] = s;
    }
    else
    {
        // Remove the server from the list
        servers->size--;
    }

    if (servers->size <= 0)
        return NULL;

    return servers->srvrs[0];
}

natsStatus
natsServers_addNewURLs(natsServers *servers,
                       const natsUrl *curUrl, const char **urls, int urlCount, const char *tlsName, bool *added)
{
    natsStatus s = NATS_OK;
    char url[256];
    char *sport;
    int portPos;
    bool found;
    bool isLH;
    natsServer *srv = NULL;
    const char **infoURLs = NULL;
    int infoURLCount = urlCount;

    // Note about pool randomization: when the pool was first created,
    // it was randomized (if allowed). We keep the order the same (removing
    // implicit servers that are no longer sent to us). New URLs are sent
    // to us in no specific order so don't need extra randomization.

    *added = false;

    // Clone the INFO urls so we can modify the list.
    infoURLs = nats_palloc(servers->pool, sizeof(const char *) * urlCount);
    if (infoURLs == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);
    for (int i = 0; i < urlCount; i++)
        infoURLs[i] = urls[i];

    // Walk the pool and removed the implicit servers that are no longer in the
    // given array/map
    for (int i = 0; i < servers->size; i++)
    {
        bool inInfo;
        int n;

        srv = servers->srvrs[i];
        snprintf(url, sizeof(url), "%s:%d", srv->url->host, srv->url->port);

        // Remove from the temp list so that at the end we are left with only
        // new (or restarted) servers that need to be added to the pool.
        n = nats_strarray_remove((char **)infoURLs, infoURLCount, url);
        inInfo = (n != infoURLCount);
        infoURLCount = n;

        // Keep servers that were set through Options, but also the one that
        // we are currently connected to (even if it is a discovered server).
        if (!(srv->isImplicit) || (srv->url == curUrl))
        {
            continue;
        }
        if (!inInfo)
        {
            // Remove from servers. Keep current order.

            // Shift left servers past current to the current's position
            for (int j = i; j < servers->size - 1; j++)
            {
                servers->srvrs[j] = servers->srvrs[j + 1];
            }
            servers->size--;
            i--;
        }
    }

    // If there are any left in infoURLs, these are new (or restarted) servers
    // and need to be added to the pool.
    if (STILL_OK(s))
    {
        for (int i = 0; (STILL_OK(s)) && (i < infoURLCount); i++)
        {
            const char *curl = infoURLs[i];

            // Before adding, check if this is a new (as in never seen) URL.
            // This is used to figure out if we invoke the DiscoveredServersCB

            isLH = false;
            found = false;

            // Consider localhost:<port>, 127.0.0.1:<port> and [::1]:<port>
            // all the same.
            sport = strrchr(curl, ':');
            portPos = (int)(sport - curl);
            if (((nats_strcasestr(curl, "localhost") == curl) && (portPos == 9)) || (strncmp(curl, "127.0.0.1", portPos) == 0) || (strncmp(curl, "[::1]", portPos) == 0))
            {
                isLH = ((curl[0] == 'l') || (curl[0] == 'L'));

                for (int j = 0; j < servers->size; j++)
                {
                    srv = servers->srvrs[j];
                    if (natsUrl_IsLocalhost(srv->url))
                    {
                        found = true;
                        break;
                    }
                }
            }
            else
            {
                for (int j = 0; j < servers->size; j++)
                {
                    srv = servers->srvrs[j];
                    snprintf(url, sizeof(url), "nats://%s", curl);
                    if (strcmp(srv->url->fullUrl, url) == 0)
                    {
                        found = true;
                        break;
                    }
                }
            }

            snprintf(url, sizeof(url), "nats://%s", curl);
            if (!found)
            {
                // Make sure that localhost URL is always stored in lower case.
                if (isLH)
                    snprintf(url, sizeof(url), "nats://localhost%s", sport);

                *added = true;
            }
            s = _addURLToServers(servers, url, true, tlsName);
        }

        if ((STILL_OK(s)) && *added && servers->randomize)
            _shuffleServers(servers, 1);
    }

    return NATS_UPDATE_ERR_STACK(s);
}
