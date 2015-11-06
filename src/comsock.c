// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "status.h"
#include "comsock.h"
#include "mem.h"

static void
_closeFd(natsSock fd)
{
    if (fd != NATS_SOCK_INVALID)
        NATS_SOCK_CLOSE(fd);
}

void
natsSock_Close(natsSock fd)
{
    _closeFd(fd);
}

void
natsSock_Shutdown(natsSock fd)
{
    if (fd != NATS_SOCK_INVALID)
        NATS_SOCK_SHUTDOWN(fd);
}

natsStatus
natsSock_SetCommonTcpOptions(natsSock fd)
{
    struct linger   l;
    int             yes = 1;

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*) &yes, sizeof(yes)) == -1)
        return NATS_SYS_ERROR;

    yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*) &yes, sizeof(yes)) == -1)
        return NATS_SYS_ERROR;

    l.l_onoff  = 1;
    l.l_linger = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (void*)&l, sizeof(l)) == -1)
        return NATS_SYS_ERROR;

    return NATS_OK;
}

natsStatus
natsSock_CreateFDSet(fd_set **newFDSet)
{
    fd_set  *fdSet = NULL;

#ifdef _WIN32
#else
    assert(FD_SETSIZE == 32768);
#endif

    fdSet = (fd_set*) NATS_MALLOC(sizeof(fd_set));

    if (fdSet == NULL)
        return NATS_NO_MEMORY;

    FD_ZERO(fdSet);

    *newFDSet = fdSet;

    return NATS_OK;
}

void
natsSock_DestroyFDSet(fd_set *fdSet)
{
    if (fdSet == NULL)
        return;

    NATS_FREE(fdSet);
}

natsStatus
natsSock_WaitReady(bool forWrite, fd_set *fdSet, natsSock sock,
                   natsDeadline *deadline)
{
    struct timeval  *timeout = NULL;
    int             res;

    FD_ZERO(fdSet);
    FD_SET(sock, fdSet);

    if (deadline != NULL)
        timeout = natsDeadline_GetTimeout(deadline);

    res = select((int) (sock + 1),
                 (forWrite ? NULL : fdSet),
                 (forWrite ? fdSet : NULL),
                 NULL,
                 timeout);

    if (res == NATS_SOCK_ERROR)
        return NATS_IO_ERROR;

    if ((res == 0) || !FD_ISSET(sock, fdSet))
        return NATS_TIMEOUT;

    return NATS_OK;
}

natsStatus
natsSock_ConnectTcp(natsSock *fd, fd_set *fdSet, natsDeadline *deadline,
                    const char *host, int port)
{
    natsStatus      s    = NATS_OK;
    natsSock        sock = NATS_SOCK_INVALID;
    int             res;
    char            sport[6];
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *p;
    bool            waitForConnect = false;
    bool            error = false;

    if (host == NULL)
        return NATS_ADDRESS_MISSING;

    snprintf(sport, sizeof(sport), "%d", port);

    memset(&hints,0,sizeof(hints));

    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    // Start with IPv6, if it fails, try IPv4.
    // TODO: Should this be some kind of option that would dictate the order?
    //       This would be beneficial performance wise.
    if ((res = getaddrinfo(host, sport, &hints, &servinfo)) != 0)
    {
         hints.ai_family = AF_INET;

         if ((res = getaddrinfo(host, sport, &hints, &servinfo)) != 0)
             s = NATS_SYS_ERROR;
    }
    for (p = servinfo; (s == NATS_OK) && (p != NULL); p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == NATS_SOCK_INVALID)
            continue;

        error = false;

        s = natsSock_SetBlocking(sock, false);
        if (s != NATS_OK)
            break;

        res = connect(sock, p->ai_addr, (natsSockLen) p->ai_addrlen);
        if ((res == NATS_SOCK_ERROR)
            && (NATS_SOCK_GET_ERROR != NATS_SOCK_CONNECT_IN_PROGRESS))
        {
            error = true;
        }
        else if (res == NATS_SOCK_ERROR)
        {
            waitForConnect = true;
        }

        if (!error)
        {
            if (waitForConnect
                && ((natsSock_WaitReady(true, fdSet, sock, deadline) != NATS_OK)
                    || !natsSock_IsConnected(sock)))
            {
                error = true;
            }
        }

        if (error)
        {
            _closeFd(sock);
            sock = NATS_SOCK_INVALID;
            continue;
        }

        s = natsSock_SetCommonTcpOptions(sock);
    }

    if (s == NATS_OK)
    {
        if (sock != NATS_SOCK_INVALID)
            *fd = sock;
        else
            s = NATS_NO_SERVER;
    }

    if (s != NATS_OK)
        _closeFd(sock);

    freeaddrinfo(servinfo);

    return s;
}

// Reads a line from the socket and returns it without the line-ending characters.
// This call blocks until the line is complete, or the socket is closed or an
// error occurs.
// Assumes the socket is blocking.
natsStatus
natsSock_ReadLine(fd_set *fdSet, natsSock fd, natsDeadline *deadline,
                  char *buffer, size_t maxBufferSize, int *n)
{
    natsStatus  s           = NATS_OK;
    int         readBytes   = 0;
    size_t      totalBytes  = 0;
    int         i;
    char        *p;

    while (1)
    {
        readBytes = recv(fd, buffer, (natsRecvLen) (maxBufferSize - totalBytes), 0);
        if (readBytes == 0)
        {
            return NATS_CONNECTION_CLOSED;
        }
        else if (readBytes == NATS_SOCK_ERROR)
        {
            if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
                return NATS_IO_ERROR;

            s = natsSock_WaitReady(false, fdSet, fd, deadline);
            if (s != NATS_OK)
                return s;

            continue;
        }

        if (n != NULL)
            *n += readBytes;

        p = buffer;

        for (i = 0; i < readBytes; i++)
        {
            if ((*p == '\n')
                && ((totalBytes > 0) || (i > 0))
                && (*(p - 1) == '\r'))
            {
                *(p - 1) = '\0';
                return NATS_OK;
            }
            p++;
        }

        buffer     += readBytes;
        totalBytes += readBytes;

        if (totalBytes == maxBufferSize)
            return NATS_LINE_TOO_LONG;
    }
}

natsStatus
natsSock_Read(natsSock fd, char *buffer, size_t maxBufferSize, int *n)
{
    int readBytes = 0;

    readBytes = recv(fd, buffer, (natsRecvLen) maxBufferSize, 0);
    if (readBytes == 0)
        return NATS_CONNECTION_CLOSED;
    else if (readBytes == NATS_SOCK_ERROR)
        return NATS_IO_ERROR;

    if (n != NULL)
        *n = readBytes;

    return NATS_OK;
}

natsStatus
natsSock_WriteFully(fd_set *fdSet, natsSock fd, natsDeadline *deadline,
                    const char *data, int len)
{
    natsStatus  s     = NATS_OK;
    int         bytes = 0;

    do
    {
        bytes = send(fd, data, len, 0);
        if (bytes == 0)
        {
            return NATS_CONNECTION_CLOSED;
        }
        else if (bytes == NATS_SOCK_ERROR)
        {
            if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
                return NATS_IO_ERROR;

            s = natsSock_WaitReady(true, fdSet, fd, deadline);
            if (s != NATS_OK)
                return s;

            continue;
        }

        data += bytes;
        len -= bytes;
    }
    while (len != 0);

    return NATS_OK;
}
