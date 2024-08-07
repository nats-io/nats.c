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
nats_initForOS(void)
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
natsSock_WaitReady(int waitMode, natsSockCtx *ctx)
{
    natsDeadline    *deadline = &ctx->writeDeadline;
    struct timeval  timeout_tv= {0};
    struct timeval  *timeout  = NULL;
    natsSock        sock      = ctx->fd;
    fd_set          fdSet;
    fd_set          errSet;
    int             res;

    FD_ZERO(&fdSet);
    FD_SET(sock, &fdSet);

    FD_ZERO(&errSet);
    FD_SET(sock, &errSet);

    if (waitMode == WAIT_FOR_READ)
        deadline = &ctx->readDeadline;

    if (deadline != NULL)
    {
        int timeoutMS = natsDeadline_GetTimeout(deadline);
        if (timeoutMS != -1)
        {
            timeout_tv.tv_sec = (long) timeoutMS / 1000;
            timeout_tv.tv_usec = (timeoutMS % 1000) * 1000;
            timeout = &timeout_tv;
        }
    }

    // On Windows, we will know if the non-blocking connect has failed
    // by using the exception set, not the write set.
    switch (waitMode)
    {
        case WAIT_FOR_READ:
            res = select(0, &fdSet, NULL, &errSet, timeout);
            break;
        case WAIT_FOR_WRITE:
        case WAIT_FOR_CONNECT:
            res = select(0, NULL, &fdSet, &errSet, timeout);
            break;
        default: abort();
    }

    if (res == NATS_SOCK_ERROR)
        return nats_setError(NATS_IO_ERROR, "select error: %d", NATS_SOCK_GET_ERROR);

    // Not ready...
    if (res == 0)
        return nats_setDefaultError(NATS_TIMEOUT);

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
