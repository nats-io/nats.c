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

#include "../natsp.h"
#include "../mem.h"
#include "../comsock.h"

void
natsSys_Init(void)
{
    // Would do anything that needs to be initialized when
    // the library loads, specific to unix.
}

natsStatus
natsSock_Init(natsSockCtx *ctx)
{
    memset(ctx, 0, sizeof(natsSockCtx));

    ctx->fd = NATS_SOCK_INVALID;


    return natsSock_CreateFDSet(&ctx->fdSet);
}

void
natsSock_Clear(natsSockCtx *ctx)
{
    natsSock_DestroyFDSet(ctx->fdSet);
}

natsStatus
natsSock_WaitReady(int waitMode, natsSockCtx *ctx)
{
    struct timeval  *timeout = NULL;
    int             res;
    fd_set          *fdSet = ctx->fdSet;
    natsSock        sock = ctx->fd;
    natsDeadline    *deadline = &(ctx->deadline);

    FD_ZERO(fdSet);
    FD_SET(sock, fdSet);

     if (deadline != NULL)
        timeout = natsDeadline_GetTimeout(deadline);

    switch (waitMode)
    {
        case WAIT_FOR_READ:     res = select((int) (sock + 1), fdSet, NULL, NULL, timeout); break;
        case WAIT_FOR_WRITE:    res = select((int) (sock + 1), NULL, fdSet, NULL, timeout); break;
        case WAIT_FOR_CONNECT:  res = select((int) (sock + 1), NULL, fdSet, NULL, timeout); break;
        default: abort();
    }

    if (res == NATS_SOCK_ERROR)
        return nats_setError(NATS_IO_ERROR, "select error: %d", res);

    if ((res == 0) || !FD_ISSET(sock, fdSet))
        return nats_setDefaultError(NATS_TIMEOUT);

    return NATS_OK;
}

natsStatus
natsSock_SetBlocking(natsSock fd, bool blocking)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1)
        return nats_setError(NATS_SYS_ERROR, "fcntl error: %d", errno);

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1)
        return nats_setError(NATS_SYS_ERROR, "fcntl error: %d", errno);

    return NATS_OK;
}

bool
natsSock_IsConnected(natsSock fd)
{
    int         res;
    int         error = 0;
    socklen_t   errorLen = (socklen_t) sizeof(int);

    res = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errorLen);
    if ((res == NATS_SOCK_ERROR) || (error != 0))
        return false;

    return true;
}

natsStatus
natsSock_Flush(natsSock fd)
{
    if (fsync(fd) != 0)
        return nats_setError(NATS_IO_ERROR,
                             "Error flushing socket. Error: %d",
                             NATS_SOCK_GET_ERROR);

    return NATS_OK;
}
