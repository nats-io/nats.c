// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef SRVPOOL_H_
#define SRVPOOL_H_

#include "status.h"

// Tracks individual backend servers.
typedef struct __natsSrv
{
    natsUrl     *url;
    bool        didConnect;
    int         reconnects;
    int64_t     lastAttempt;

} natsSrv;

typedef struct __natsSrvPool
{
    natsSrv     **srvrs;
    int         size;

} natsSrvPool;

// This is defined in natsp.h, but natsp.h includes this file. Alternatively,
// we would need to move the defs above in natsp.h.
struct __natsOptions;

#define natsSrvPool_GetSize(p)              ((p)->size)
#define natsSrvPool_GetSrv(p,i)             ((p)->srvrs[(i)])
#define natsSrvPool_GetSrvUrl(p,i)          (natsSrvPool_GetSrv((p),(i))->url)
#define natsSrvPool_SetSrvDidConnect(p,i,c) (natsSrvPool_GetSrv((p),(i))->didConnect=(c))
#define natsSrvPool_SetSrvReconnects(p,i,r) (natsSrvPool_GetSrv((p),(i))->reconnects=(r))

// Create the server pool using the options given.
// We will place a Url option first, followed by any
// Server Options. We will randomize the server pool unlesss
// the NoRandomize flag is set.
natsStatus
natsSrvPool_Create(natsSrvPool **newPool, struct __natsOptions *opts);

// Return the server from the pool that has the given 'url'. If the 'index'
// pointer is not NULL, will place at this location the index of the returned
// server in the pool.
natsSrv*
natsSrvPool_GetCurrentServer(natsSrvPool *pool, const natsUrl *url, int *index);

// Pop the current server and put onto the end of the list. Select head of list as long
// as number of reconnect attempts under MaxReconnect.
natsSrv*
natsSrvPool_GetNextServer(natsSrvPool *pool, struct __natsOptions *opts, const natsUrl *ncUrl);

// Destroy the pool, freeing up all memory used.
void
natsSrvPool_Destroy(natsSrvPool *pool);


#endif /* SRVPOOL_H_ */
