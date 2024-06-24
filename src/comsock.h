// Copyright 2015-2019 The NATS Authors
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

#ifndef SOCK_H_
#define SOCK_H_

#include "natsp.h"
#include "natstime.h"

#define WAIT_FOR_READ (0)
#define WAIT_FOR_WRITE (1)
#define WAIT_FOR_CONNECT (2)

struct __natsSockCtx
{
    natsSock fd;
    bool fdActive;

    natsDeadline readDeadline;
    natsDeadline writeDeadline;

    // During Connect we don't use an external event loop (such as libuv), then
    // set to true.
    bool useEventLoop;

    int orderIP; // possible values: 0,4,6,46,64

    // By default, the list of IPs returned by the hostname resolution will
    // be shuffled. This option, if `true`, will disable the shuffling.
    bool noRandomize;

};

natsStatus
natsSock_Init(natsSockCtx *ctx);


natsStatus
natsSock_WaitReady(int waitMode, natsSockCtx *ctx);

void
natsSock_ShuffleIPs(natsSockCtx *ctx, natsPool *pool, struct addrinfo **ipListHead, int count);

natsStatus
natsSock_ConnectTcp(natsSockCtx *ctx, natsPool *pool, const char *host, int port);

natsStatus
natsSock_SetBlocking(natsSock fd, bool blocking);

bool
natsSock_IsConnected(natsSock fd);

// Reads up to 'maxBufferSize' bytes from the socket and put them in 'buffer'.
// If the socket is blocking, wait until some data is available or the socket
// is closed or an error occurs.
// If the socket is non-blocking, wait up to the optional deadline (set in
// the context). If NULL, behaves like a blocking socket.
// If an external event loop is used, it is possible that this function
// returns NATS_OK with 'n' == 0.
natsStatus
natsSock_Read(natsSockCtx *ctx, uint8_t *buffer, size_t maxBufferSize, size_t *n);

// Writes up to 'len' bytes to the socket. If the socket is blocking,
// wait for some data to be sent. If the socket is non-blocking, wait up
// to the optional deadline (set in ctx).
// If an external event loop is used, it is possible that this function
// returns NATS_OK with 'n' == 0.
natsStatus
natsSock_Write(natsSockCtx *ctx, natsString *buf, size_t *n);

natsStatus
natsSock_Flush(natsSock fd);

void
natsSock_Close(natsSock fd);

natsStatus
natsSock_SetCommonTcpOptions(natsSock fd);

void
natsSock_Shutdown(natsSock fd);

void
natsSock_InitDeadline(natsSockCtx *ctx, int64_t timeout);

natsStatus
natsSock_GetLocalIPAndPort(natsSockCtx *ctx, natsPool *pool, const char **ip, int *port);

#endif /* SOCK_H_ */
