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

#include <errno.h>
#include <io.h>

#include "../mem.h"
#include "../comsock.h"

void
natsSys_Init(void)
{
    WSADATA wsaData;
    int     errorno;

    /* Initialize winsock */
    errorno = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (errorno != 0)
    {
        printf("FATAL: Unable to initialize winsock!\n");
        abort();
    }
}

natsStatus
natsSock_Init(natsSockCtx *ctx)
{
    natsStatus  s;

    memset(ctx, 0, sizeof(natsSockCtx));

    ctx->fd = NATS_SOCK_INVALID;

    s = natsSock_CreateFDSet(&ctx->fdSet);
    if (s == NATS_OK)
    {
        s = natsSock_CreateFDSet(&ctx->errSet);
        if (s != NATS_OK)
        {
            natsSock_DestroyFDSet(ctx->fdSet);
            ctx->fdSet = NULL;
        }
    }
    return s;
}

void
natsSock_Clear(natsSockCtx *ctx)
{
    natsSock_DestroyFDSet(ctx->fdSet);
    natsSock_DestroyFDSet(ctx->errSet);
}

natsStatus
natsSock_WaitReady(int waitMode, natsSockCtx *ctx)
{
    struct timeval  *timeout = NULL;
    int             res;
    fd_set          *fdSet = ctx->fdSet;
    fd_set          *errSet = ctx->errSet;
    natsSock        sock = ctx->fd;
    natsDeadline    *deadline = &(ctx->deadline);

    FD_ZERO(fdSet);
    FD_SET(sock, fdSet);

    FD_ZERO(errSet);
    FD_SET(sock, errSet);

    if (deadline != NULL)
        timeout = natsDeadline_GetTimeout(deadline);

    switch (waitMode)
    {
        case WAIT_FOR_READ:     res = select((int) (sock + 1), fdSet, NULL, NULL, timeout); break;
        case WAIT_FOR_WRITE:    res = select((int) (sock + 1), NULL, fdSet, NULL, timeout); break;
        // On Windows, we will know if the non-blocking connect has failed
        // by using the exception set, not the write set.
        case WAIT_FOR_CONNECT:  res = select((int) (sock + 1), NULL, fdSet, errSet, timeout); break;
        default: abort();
    }

    if (res == NATS_SOCK_ERROR)
        return nats_setError(NATS_IO_ERROR, "select error: %d", res);

    // Not ready if select returned no socket, the socket is not in the
    // given fdSet, or for connect, the socket is set in the error set.
    if ((res == 0)
            || !FD_ISSET(sock, fdSet)
            || ((waitMode == WAIT_FOR_CONNECT) && FD_ISSET(sock, errSet)))
    {
        return nats_setDefaultError(NATS_TIMEOUT);
    }

    return NATS_OK;
}

natsStatus
natsSock_SetBlocking(natsSock fd, bool blocking)
{
    u_long iMode = 0;

    // If iMode = 0, blocking is enabled;
    // If iMode != 0, non-blocking mode is enabled.
    if (!blocking)
        iMode = 1;

    if (ioctlsocket(fd, FIONBIO, &iMode) != NO_ERROR)
        return nats_setError(NATS_SYS_ERROR,
                             "ioctlsocket error: %d",
                             NATS_SOCK_GET_ERROR);

    return NATS_OK;
}

bool
natsSock_IsConnected(natsSock fd)
{
    int res;
    int error = 0;
    int errorLen = (socklen_t) sizeof(int);

    res = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &errorLen);
    if ((res == NATS_SOCK_ERROR) || (error != 0))
        return false;

    return true;
}

natsStatus
natsSock_Flush(natsSock fd)
{
    HANDLE fh = (HANDLE)_get_osfhandle((int) fd);

    if (fh == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return nats_setError(NATS_INVALID_ARG, "%s",
                             "Error setting flushing socket. Invalid handle");
    }

    if (!FlushFileBuffers(fh))
    {
        DWORD code = GetLastError();

        if (code == ERROR_INVALID_HANDLE)
            errno = EINVAL;
        else
            errno = EIO;

        return nats_setError(NATS_IO_ERROR,
                             "Error setting flushing socket. Error: %d",
                             NATS_SOCK_GET_ERROR);
    }

    return NATS_OK;
}
