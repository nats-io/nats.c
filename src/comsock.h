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

#ifndef SOCK_H_
#define SOCK_H_

#include "natsp.h"

natsStatus
natsSock_Init(natsSockCtx *ctx);

void
natsSock_Clear(natsSockCtx *ctx);

natsStatus
natsSock_WaitReady(int waitMode, natsSockCtx *ctx);

natsStatus
natsSock_ConnectTcp(natsSockCtx *ctx, const char *host, int port);

natsStatus
natsSock_SetBlocking(natsSock fd, bool blocking);

natsStatus
natsSock_CreateFDSet(fd_set **newFDSet);

void
natsSock_DestroyFDSet(fd_set *fdSet);

bool
natsSock_IsConnected(natsSock fd);

// Reads a line from the socket and returns it without the line-ending characters.
// This call blocks until the line is complete, or the socket is closed or an
// error occurs.
// Handles blocking and non-blocking sockets. For the later, an optional 'deadline'
// indicates how long it can wait for the full read to complete.
//
// NOTE: 'buffer[0]' must be set to '\0' prior to the very first call. If the
// peer is sending multiple lines, it is possible that this function reads the
// next line(s) (or partials) in a single call. In this case, the caller needs
// to repeat the call with the same buffer to "read" the next line.
natsStatus
natsSock_ReadLine(natsSockCtx *ctx, char *buffer, size_t maxBufferSize);

// Reads up to 'maxBufferSize' bytes from the socket and put them in 'buffer'.
// If the socket is blocking, wait until some data is available or the socket
// is closed or an error occurs.
// If the socket is non-blocking, wait up to the optional deadline (set in
// the context). If NULL, behaves like a blocking socket.
// If an external event loop is used, it is possible that this function
// returns NATS_OK with 'n' == 0.
natsStatus
natsSock_Read(natsSockCtx *ctx, char *buffer, size_t maxBufferSize, int *n);

// Writes up to 'len' bytes to the socket. If the socket is blocking,
// wait for some data to be sent. If the socket is non-blocking, wait up
// to the optional deadline (set in ctx).
// If an external event loop is used, it is possible that this function
// returns NATS_OK with 'n' == 0.
natsStatus
natsSock_Write(natsSockCtx *ctx, const char *data, int len, int *n);

// Writes 'len' bytes to the socket. Does not return until all bytes
// have been written, unless the socket is closed or an error occurs.
natsStatus
natsSock_WriteFully(natsSockCtx *ctx, const char *data, int len);

natsStatus
natsSock_Flush(natsSock fd);

void
natsSock_Close(natsSock fd);

natsStatus
natsSock_SetCommonTcpOptions(natsSock fd);

void
natsSock_Shutdown(natsSock fd);


#endif /* SOCK_H_ */
