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

#ifndef SERVERS_H_
#define SERVERS_H_

#include "url.h"

// Tracks individual backend servers.
struct __natsServer_s
{
    natsUrl *url;
    bool didConnect;
    bool isImplicit;
    int reconnects;
    char *tlsName;
    int lastAuthErrCode;
};

struct __natsServers_s
{
    natsPool *pool;
    natsServer **srvrs;
    int size;
    int cap;
    bool randomize;
    char *user;
    char *pwd;
};

#define natsServers_Count(p) ((p)->size)
#define natsServers_Get(p, i) ((p)->srvrs[(i)])
#define natsServers_SetSrvDidConnect(p, i, c) (natsServers_Get((p), (i))->didConnect = (c))
#define natsServers_SetSrvReconnects(p, i, r) (natsServers_Get((p), (i))->reconnects = (r))

// Create a servers list using the options given. We will place a Url option
// first, followed by any Server Options. We will randomize the list unlesss the
// NoRandomize flag is set.
natsStatus
natsServers_Create(natsServers **newServers, natsPool *pool, struct __natsOptions *opts);

// Return the server corresponding to given `cur` with current position
// in the list.
natsServer *
natsServers_GetCurrentServer(natsServers *servers, const natsServer *cur, int *index);

// Pop the current server and put onto the end of the list. Select head of list as long
// as number of reconnect attempts under MaxReconnect.
natsServer *
natsServers_GetNextServer(natsServers *servers, struct __natsOptions *opts, const natsServer *cur);

// Go through the list of the given URLs and add them to the list if not already
// present.
natsStatus
natsServers_addNewURLs(natsServers *servers, const natsUrl *curUrl, const char **urls, int urlCount, const char *tlsName, bool *added);

// Returns an array of servers (as a copy). User is responsible to free the memory.
natsStatus
natsServers_GetServers(natsServers *servers, bool implicitOnly, char ***out, int *count);

#endif /* SERVERS_H_ */
