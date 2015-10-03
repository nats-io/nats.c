// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"
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
        return NATS_SYS_ERROR;

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

