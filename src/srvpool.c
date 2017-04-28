// Copyright 2015-2017 Apcera Inc. All rights reserved.

#include "natsp.h"

#include "mem.h"
#include "url.h"

static void
_freeSrv(natsSrv *srv)
{
    if (srv == NULL)
        return;

    natsUrl_Destroy(srv->url);
    NATS_FREE(srv);
}

static natsStatus
_createSrv(natsSrv **newSrv, char *url, bool implicit)
{
    natsStatus  s = NATS_OK;
    natsSrv     *srv = (natsSrv*) NATS_CALLOC(1, sizeof(natsSrv));

    if (srv == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    srv->isImplicit = implicit;

    s = natsUrl_Create(&(srv->url), url);
    if (s == NATS_OK)
        *newSrv = srv;
    else
        _freeSrv(srv);

    return NATS_UPDATE_ERR_STACK(s);
}

natsSrv*
natsSrvPool_GetCurrentServer(natsSrvPool *pool, const natsUrl *url, int *index)
{
    natsSrv *s = NULL;
    int     i;

    for (i = 0; i < pool->size; i++)
    {
        s = pool->srvrs[i];
        if (s->url == url)
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
natsSrv*
natsSrvPool_GetNextServer(natsSrvPool *pool, natsOptions *opts, const natsUrl *ncUrl)
{
    natsSrv *s = NULL;
    int     i, j;

    s = natsSrvPool_GetCurrentServer(pool, ncUrl, &i);
    if (i < 0)
        return NULL;

    // Shift left servers past current to the current's position
    for (j = i; j < pool->size - 1; j++)
        pool->srvrs[j] = pool->srvrs[j+1];

    if ((opts->maxReconnect < 0)
        || (s->reconnects < opts->maxReconnect))
    {
        // Move the current server to the back of the list
        pool->srvrs[pool->size - 1] = s;
    }
    else
    {
        // Remove the server from the list
        _freeSrv(s);
        pool->size--;
    }

    if (pool->size <= 0)
        return NULL;

    return pool->srvrs[0];
}

void
natsSrvPool_Destroy(natsSrvPool *pool)
{
    natsSrv *srv;
    int     i;

    if (pool == NULL)
        return;

    for (i = 0; i < pool->size; i++)
    {
        srv = pool->srvrs[i];
        _freeSrv(srv);
    }
    natsStrHash_Destroy(pool->urls);
    pool->urls = NULL;

    NATS_FREE(pool->srvrs);
    pool->srvrs = NULL;
    pool->size  = 0;
    NATS_FREE(pool);
}

static natsStatus
_addURLToPool(natsSrvPool *pool, char *sURL, bool implicit)
{
    natsStatus  s;
    natsSrv     *srv = NULL;
    bool        addedToMap = false;
    char        bareURL[256];

    s = _createSrv(&srv, sURL, implicit);
    if (s != NATS_OK)
        return s;

    // In the map, we need to add an URL that is just host:port
    snprintf(bareURL, sizeof(bareURL), "%s:%d", srv->url->host, srv->url->port);
    s = natsStrHash_Set(pool->urls, bareURL, true, (void*)1, NULL);
    if (s == NATS_OK)
    {
        addedToMap = true;
        if (pool->size + 1 > pool->cap)
        {
            natsSrv **newArray  = NULL;
            int     newCap      = 2 * pool->cap;

            newArray = (natsSrv**) NATS_REALLOC(pool->srvrs, newCap * sizeof(char*));
            if (newArray == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);

            if (s == NATS_OK)
            {
                pool->cap = newCap;
                pool->srvrs = newArray;
            }
        }
        if (s == NATS_OK)
            pool->srvrs[pool->size++] = srv;
    }
    if (s != NATS_OK)
    {
        if (addedToMap)
            natsStrHash_Remove(pool->urls, sURL);

        _freeSrv(srv);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_shufflePool(natsSrvPool *pool)
{
    int     i, j;
    natsSrv *tmp;

    if (pool->size <= 1)
        return;

    srand((unsigned int) nats_NowInNanoSeconds());

    for (i = 0; i < pool->size; i++)
    {
        j = rand() % (i + 1);
        tmp = pool->srvrs[i];
        pool->srvrs[i] = pool->srvrs[j];
        pool->srvrs[j] = tmp;
    }
}

natsStatus
natsSrvPool_addNewURLs(natsSrvPool *pool, char **urls, int urlCount, bool doShuffle, bool *added)
{
    natsStatus  s       = NATS_OK;
    char        url[256];
    int         i;
    char        *sport;
    int         portPos;
    bool        found;
    bool        isLH;

    *added = false;

    // If we can shuffle, we shuffle the given array, not the entire pool
    if (urlCount > 0 && doShuffle)
    {
        int     j;
        char    *tmp;

        for (i = 0; i < urlCount; i++)
        {
            j = rand() % (i + 1);
            tmp = urls[i];
            urls[i] = urls[j];
            urls[j] = tmp;
        }
    }

    for (i=0; (s == NATS_OK) && (i<urlCount); i++)
    {
        isLH  = false;
        found = false;

        // Consider localhost:<port>, 127.0.0.1:<port> and [::1]:<port>
        // all the same.
        sport = strrchr(urls[i], ':');
        portPos = (int) (sport - urls[i]);
        if (((nats_strcasestr(urls[i], "localhost") == urls[i]) && (portPos == 9))
                || (strncmp(urls[i], "127.0.0.1", portPos) == 0)
                || (strncmp(urls[i], "[::1]", portPos) == 0))
        {
            isLH = ((urls[i][0] == 'l') || (urls[i][0] == 'L'));

            snprintf(url, sizeof(url), "localhost%s", sport);
            found = (natsStrHash_Get(pool->urls, url) != NULL);
            if (!found)
            {
                snprintf(url, sizeof(url), "127.0.0.1%s", sport);
                found = (natsStrHash_Get(pool->urls, url) != NULL);
            }
            if (!found)
            {
                snprintf(url, sizeof(url), "[::1]%s", sport);
                found = (natsStrHash_Get(pool->urls, url) != NULL);
            }
        }
        else
        {
            found = (natsStrHash_Get(pool->urls, urls[i]) != NULL);
        }

        if (!found)
        {
            // Make sure that localhost URL is always stored in lower case.
            if (isLH)
                snprintf(url, sizeof(url), "nats://localhost%s", sport);
            else
                snprintf(url, sizeof(url), "nats://%s", urls[i]);
            s = _addURLToPool(pool, url, true);
            if (s == NATS_OK)
                *added = true;
        }
    }

    return NATS_UPDATE_ERR_STACK(s);
}

// Create the server pool using the options given.
// We will place a Url option first, followed by any
// Server Options. We will randomize the server pool unlesss
// the NoRandomize flag is set.
natsStatus
natsSrvPool_Create(natsSrvPool **newPool, natsOptions *opts)
{
    natsStatus  s        = NATS_OK;
    natsSrvPool *pool    = NULL;
    int         poolSize;
    int         i;

    poolSize  = (opts->url != NULL ? 1 : 0);
    poolSize += opts->serversCount;

    // If the pool is going to be empty, we will add the default URL.
    if (poolSize == 0)
        poolSize = 1;

    pool = (natsSrvPool*) NATS_CALLOC(1, sizeof(natsSrvPool));
    if (pool == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    pool->srvrs = (natsSrv**) NATS_CALLOC(poolSize, sizeof(natsSrv*));
    if (pool->srvrs == NULL)
    {
        NATS_FREE(pool);
        return nats_setDefaultError(NATS_NO_MEMORY);
    }
    // Set the current capacity. The array of urls may have to grow in
    // the future.
    pool->cap = poolSize;

    // Map that helps find out if an URL is already known.
    s = natsStrHash_Create(&(pool->urls), poolSize);

    // Add URLs from Options' Servers
    for (i=0; (s == NATS_OK) && (i < opts->serversCount); i++)
        s = _addURLToPool(pool, opts->servers[i], false);

    if (s == NATS_OK)
    {
        // Randomize if allowed to
        if (!(opts->noRandomize))
            _shufflePool(pool);
    }

    // Normally, if this one is set, Options.Servers should not be,
    // but we always allowed that, so continue to do so.
    if ((s == NATS_OK) && (opts->url != NULL))
    {
        // Add to the end of the array
        s = _addURLToPool(pool, opts->url, false);
        if ((s == NATS_OK) && (pool->size > 1))
        {
            // Then swap it with first to guarantee that Options.Url is tried first.
            natsSrv *opstUrl = pool->srvrs[pool->size-1];

            pool->srvrs[pool->size-1] = pool->srvrs[0];
            pool->srvrs[0] = opstUrl;
        }
    }
    else if ((s == NATS_OK) && (pool->size == 0))
    {
        // Place default URL if pool is empty.
        s = _addURLToPool(pool, (char*) NATS_DEFAULT_URL, false);
    }

    if (s == NATS_OK)
        *newPool = pool;
    else
        natsSrvPool_Destroy(pool);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSrvPool_GetServers(natsSrvPool *pool, bool implicitOnly, char ***servers, int *count)
{
    natsStatus  s       = NATS_OK;
    char        **srvrs = NULL;
    natsSrv     *srv;
    natsUrl     *url;
    int         i;
    int         discovered = 0;

    if (pool->size == 0)
    {
        *servers = NULL;
        *count   = 0;
        return NATS_OK;
    }

    srvrs = (char **) NATS_CALLOC(pool->size, sizeof(char*));
    if (srvrs == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    for (i=0; ((s == NATS_OK) && (i<pool->size)); i++)
    {
        srv = pool->srvrs[i];
        if (implicitOnly && !srv->isImplicit)
            continue;
        url = srv->url;
        if (nats_asprintf(&(srvrs[discovered]), "nats://%s:%d", url->host, url->port) == -1)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
            discovered++;
    }
    if (s == NATS_OK)
    {
        *servers = srvrs;
        *count   = discovered;
    }
    else
    {
        for (i=0; i<discovered; i++)
            NATS_FREE(srvrs[i]);
        NATS_FREE(srvrs);
    }
    return NATS_UPDATE_ERR_STACK(s);
}
