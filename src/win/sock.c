// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"

#include <errno.h>
#include <io.h>

#include "../mem.h"

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
