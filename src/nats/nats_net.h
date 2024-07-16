// Copyright 2015-2023 The NATS Authors
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

#ifndef NATS_NET_H_
#define NATS_NET_H_

#include "nats_base.h"
#include "nats_mem.h"
#include "nats_opts.h"

#if defined(_WIN32)
#include <winsock2.h>
typedef SOCKET natsSock;
#else
typedef int natsSock;
#endif

typedef struct __natsEventLoop_s natsEventLoop;

typedef struct
{
    uint64_t inMsgs;
    uint64_t outMsgs;
    uint64_t inBytes;
    uint64_t outBytes;
    uint64_t reconnects;
} natsConnectionStatistics;

/** \brief Attach this connection to the external event loop.
 *
 * After a connection has (re)connected, this callback is invoked. It should
 * perform the necessary work to start polling the given socket for READ events.
 *
 * @param userData location where the adapter implementation will store the
 * object it created and that will later be passed to all other callbacks. If
 * `*userData` is not `NULL`, this means that this is a reconnect event.
 * @param pool the pool to allocate userData from (nc->lifetimePool, but we
 * can't see inside nc here).
 * @param loop the event loop (as a generic void*) this connection is being
 * attached to.
 * @param nc the connection being attached to the event loop.
 * @param socket the socket to poll for read/write events.
 */
typedef natsStatus (*natsEventLoop_AttachF)(
    void *userData,
    natsEventLoop *ev,
    natsConnection *nc,
    natsSock socket);

/** \brief Read or write event needs to be added or removed.
 *
 * The `NATS` library will invoke this callback to indicate if the event
 * loop should start (`add is `true`) or stop (`add` is `false`) polling
 * for read events on the socket.
 *
 * @param userData the pointer to an user object created in #natsEventLoop_Attach.
 * @param add `true` if the event library should start polling, `false` otherwise.
 */
typedef natsStatus (*natsEventLoop_AddRemoveF)(
    void *userData,
    bool add);

/** \brief Stop polling or detach from the event loop.
 *
 * The `NATS` library will invoke this callback to indicate that the connection
 * no longer needs to be attached to the event loop. User can cleanup some state.
 *
 * @param userData the pointer to an user object created in #natsEventLoop_Attach.
 */
typedef natsStatus (*natsEventLoop_StopF)(
    void *userData);

struct __natsEventLoop_s
{
    // Allocating and freeing the context object is the responsibility of the
    // user (us here). The size comes from this config.
    size_t ctxSize;

    // Each adapter offers a ..._New (e.g. Libevent_New) function that will
    // create a new base loop, and store it here for the callbacks to use.
    void *loop;

    natsEventLoop_AttachF attach;
    natsEventLoop_AddRemoveF read;
    natsEventLoop_AddRemoveF write;
    natsEventLoop_StopF stop;
    natsEventLoop_StopF detach;
};

// Creates a connection object and a socket, attaches to the event loop provided by the caller.
// - ev is required
// - cb is optional, but used often
// - opts is optional, reasonably defaulted
NATS_EXTERN natsStatus nats_AsyncConnectWithOptions(natsConnection **newConn, natsEventLoop *ev, natsOptions *options);
NATS_EXTERN natsStatus nats_AsyncConnectTo(natsConnection **newConn, natsEventLoop *ev, const char *url, natsOnConnectionEventF cb, void *closure);
NATS_EXTERN void nats_CloseConnection(natsConnection *nc);
NATS_EXTERN void nats_DestroyConnection(natsConnection *nc);
NATS_EXTERN const char *nats_GetConnectionError(natsConnection *nc);
NATS_EXTERN void nats_ProcessReadEvent(natsConnection *nc);
NATS_EXTERN void nats_ProcessWriteEvent(natsConnection *nc);

#endif /* NATS_NET_H_ */
