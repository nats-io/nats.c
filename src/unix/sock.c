// Copyright 2015 Apcera Inc. All rights reserved.

#include "../natsp.h"
#include "../mem.h"

void
natsSys_Init(void)
{
    // Would do anything that needs to be initialized when
    // the libary loads, specific to unix.
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
