// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include "mem.h"
#include "url.h"
#include "util.h"

static void
_freeSrv(natsSrv *srv)
{
    if (srv == NULL)
        return;

    natsUrl_Destroy(srv->url);
    NATS_FREE(srv);
}

static natsStatus
_createSrv(natsSrv **newSrv, char *url)
{
    natsStatus  s = NATS_OK;
    natsSrv     *srv = (natsSrv*) NATS_CALLOC(1, sizeof(natsSrv));

    if (srv == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

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

    NATS_FREE(pool->srvrs);
    pool->srvrs = NULL;
    pool->size  = 0;
    NATS_FREE(pool);
}

// Create the server pool using the options given.
// We will place a Url option first, followed by any
// Server Options. We will randomize the server pool unlesss
// the NoRandomize flag is set.
natsStatus
natsSrvPool_Create(natsSrvPool **newPool, natsOptions *opts)
{
    natsStatus  s        = NATS_OK;
    int         *indexes = NULL;
    natsSrvPool *pool    = NULL;
    natsSrv     *srv     = NULL;
    int         poolSize;

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

    if (opts->url != NULL)
    {
        s = _createSrv(&srv, opts->url);
        if (s == NATS_OK)
        {
            pool->srvrs[0] = srv;
            pool->size++;
        }
    }

    if ((s == NATS_OK) && (opts->serversCount > 0))
    {
        indexes = (int*) NATS_CALLOC(opts->serversCount, sizeof(int));
        if (indexes == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
    }

    if ((s == NATS_OK) && (opts->serversCount > 0))
    {
        int     i;
        char    *url;

        for (i = 0; i < opts->serversCount; i++)
            indexes[i] = i;

        if (!(opts->noRandomize))
            nats_Randomize(indexes, opts->serversCount);

        for (i = 0; (s == NATS_OK) && (i < opts->serversCount); i++)
        {
            url = opts->servers[indexes[i]];

            s = _createSrv(&srv, url);
            if (s == NATS_OK)
                pool->srvrs[pool->size++] = srv;
        }
    }

    if ((s == NATS_OK) && (pool->size == 0))
    {
        // Place default URL if pool is empty.
        s = _createSrv(&srv, (char*) NATS_DEFAULT_URL);
        if (s == NATS_OK)
            pool->srvrs[pool->size++] = srv;
    }

    if (s == NATS_OK)
        *newPool = pool;
    else
        natsSrvPool_Destroy(pool);

    NATS_FREE(indexes);

    return NATS_UPDATE_ERR_STACK(s);
}
