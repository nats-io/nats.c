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
        return nats_setError(NATS_SYS_ERROR, "setsockopt TCP_NO_DELAY error: %d",
                             NATS_SOCK_GET_ERROR);

    yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*) &yes, sizeof(yes)) == -1)
        return nats_setError(NATS_SYS_ERROR, "setsockopt SO_REUSEADDR error: %d",
                             NATS_SOCK_GET_ERROR);

    l.l_onoff  = 1;
    l.l_linger = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (void*)&l, sizeof(l)) == -1)
        return nats_setError(NATS_SYS_ERROR, "setsockopt SO_LINGER error: %d",
                             NATS_SOCK_GET_ERROR);

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
        return nats_setDefaultError(NATS_NO_MEMORY);

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
natsSock_WaitReady(bool forWrite, natsSockCtx *ctx)
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

    res = select((int) (sock + 1),
                 (forWrite ? NULL : fdSet),
                 (forWrite ? fdSet : NULL),
                 NULL,
                 timeout);

    if (res == NATS_SOCK_ERROR)
        return nats_setError(NATS_IO_ERROR, "select error: %d", res);

    if ((res == 0) || !FD_ISSET(sock, fdSet))
        return nats_setDefaultError(NATS_TIMEOUT);

    return NATS_OK;
}

natsStatus
natsSock_ConnectTcp(natsSockCtx *ctx, const char *host, int port)
{
    natsStatus      s    = NATS_OK;
    int             res;
    char            sport[6];
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *p;
    bool            waitForConnect = false;
    bool            error = false;

    if (host == NULL)
        return nats_setError(NATS_ADDRESS_MISSING, "%s", "No host specified");

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
         {
             s = nats_setError(NATS_SYS_ERROR, "getaddrinfo error: %s",
                               gai_strerror(res));
         }
    }
    for (p = servinfo; (s == NATS_OK) && (p != NULL); p = p->ai_next)
    {
        ctx->fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (ctx->fd == NATS_SOCK_INVALID)
            continue;

        error = false;

        s = natsSock_SetBlocking(ctx->fd, false);
        if (s != NATS_OK)
            break;

        res = connect(ctx->fd, p->ai_addr, (natsSockLen) p->ai_addrlen);
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
                && ((natsSock_WaitReady(true, ctx) != NATS_OK)
                    || !natsSock_IsConnected(ctx->fd)))
            {
                error = true;
            }
        }

        if (error)
        {
            _closeFd(ctx->fd);
            ctx->fd = NATS_SOCK_INVALID;
            continue;
        }

        s = natsSock_SetCommonTcpOptions(ctx->fd);
    }

    if (s == NATS_OK)
    {
        if (ctx->fd == NATS_SOCK_INVALID)
            s = nats_setDefaultError(NATS_NO_SERVER);
    }

    if (s != NATS_OK)
    {
        _closeFd(ctx->fd);
        ctx->fd = NATS_SOCK_INVALID;
    }

    freeaddrinfo(servinfo);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSock_ReadLine(natsSockCtx *ctx, char *buffer, size_t maxBufferSize)
{
    natsStatus  s           = NATS_OK;
    int         readBytes   = 0;
    size_t      totalBytes  = 0;
    char        *p          = buffer;
    char        *eol;

    // By contract, the caller needs to set buffer[0] to '\0' before the first
    // call.
    if (*p != '\0')
    {
        // We assume that this is not the first call with the given buffer.
        // Move possible data after the first line to the beginning of the
        // buffer.
        char    *nextLine;
        size_t  nextStart;
        size_t  len = 0;

        // The start of the next line will be the length of the line at the
        // start of the buffer + 2, which is the number of characters
        // representing CRLF.
        nextStart = strlen(buffer) + 2;
        nextLine  = (char*) (buffer + nextStart);

        // There is some data...
        if (*nextLine != '\0')
        {
            // The next line (even if partial) is guaranteed to be NULL
            // terminated.
            len = strlen(nextLine);

            // Move to the beginning of the buffer (and include the NULL char)
            memmove(buffer, nextLine, len + 1);

            // Now, if the string contains a CRLF, we don't even need to read
            // from the socket. Update the buffer and return.
            if ((eol = strstr(buffer, _CRLF_)) != NULL)
            {
                // Replace the '\r' with '\0' to NULL terminate the string.
                *eol = '\0';

                // We are done!
                return NATS_OK;
            }

            // This is a partial, we need to read more data until we get to
            // the end of the line (\r\n).
            p = (char*) (p + len);
        }
        else
        {
            *p = '\0';
        }
    }

    while (1)
    {
        s = natsSock_Read(ctx, p, (maxBufferSize - totalBytes), &readBytes);
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);

        if (totalBytes + readBytes == maxBufferSize)
            return nats_setDefaultError(NATS_LINE_TOO_LONG);

        // We need to append a NULL character after what we have received.
        *(p + readBytes) = '\0';

        if ((eol = strstr(p, _CRLF_)) != NULL)
        {
            *eol = '\0';
            return NATS_OK;
        }

        p          += readBytes;
        totalBytes += readBytes;
    }
}

natsStatus
natsSock_Read(natsSockCtx *ctx, char *buffer, size_t maxBufferSize, int *n)
{
    natsStatus  s         = NATS_OK;
    int         readBytes = 0;
    bool        needRead  = true;

    while (needRead)
    {
#if defined(NATS_HAS_TLS)
        if (ctx->ssl != NULL)
            readBytes = SSL_read(ctx->ssl, buffer, (int) maxBufferSize);
        else
#endif
            readBytes = recv(ctx->fd, buffer, (natsRecvLen) maxBufferSize, 0);

        if (readBytes == 0)
        {
            return NATS_CONNECTION_CLOSED;
        }
        else if (readBytes == NATS_SOCK_ERROR)
        {
#if defined(NATS_HAS_TLS)
            if (ctx->ssl != NULL)
            {
                int sslErr = SSL_get_error(ctx->ssl, readBytes);

                if ((sslErr != SSL_ERROR_WANT_READ)
                    && (sslErr != SSL_ERROR_WANT_WRITE))
                {
                    return nats_setError(NATS_IO_ERROR, "SSL_read error: %s",
                                         NATS_SSL_ERR_REASON_STRING);
                }

                if (ctx->fdSet == NULL)
                    continue;
            }
            else
#endif
                if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
            {
                return nats_setError(NATS_IO_ERROR, "recv error: %d",
                                     NATS_SOCK_GET_ERROR);
            }

            // For non-blocking sockets, if the read would block, we need to
            // wait up to the deadline.
            s = natsSock_WaitReady(false, ctx);
            if (s != NATS_OK)
                return NATS_UPDATE_ERR_STACK(s);

            continue;
        }

        if (n != NULL)
            *n = readBytes;

        needRead = false;
    }

    return NATS_OK;
}

natsStatus
natsSock_WriteFully(natsSockCtx *ctx, const char *data, int len)
{
    natsStatus  s     = NATS_OK;
    int         bytes = 0;

    do
    {
#if defined(NATS_HAS_TLS)
        if (ctx->ssl != NULL)
            bytes = SSL_write(ctx->ssl, data, len);
        else
#endif
            bytes = send(ctx->fd, data, len, 0);

        if (bytes == 0)
        {
            return NATS_CONNECTION_CLOSED;
        }
        else if (bytes == NATS_SOCK_ERROR)
        {
#if defined(NATS_HAS_TLS)
            if (ctx->ssl != NULL)
            {
                int sslErr = SSL_get_error(ctx->ssl, bytes);
                if ((sslErr != SSL_ERROR_WANT_READ)
                    && (sslErr != SSL_ERROR_WANT_WRITE))
                {
                    return nats_setError(NATS_IO_ERROR, "SSL_write error: %s",
                                         NATS_SSL_ERR_REASON_STRING);
                }

                if (ctx->fdSet == NULL)
                    continue;
            }
            else
#endif
                if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
            {
                return nats_setError(NATS_IO_ERROR, "send error: %d",
                                     NATS_SOCK_GET_ERROR);
            }

            // For non-blocking sockets, if the write would block, we need to
            // wait up to the deadline.
            s = natsSock_WaitReady(true, ctx);
            if (s != NATS_OK)
                return NATS_UPDATE_ERR_STACK(s);

            continue;
        }

        data += bytes;
        len -= bytes;
    }
    while (len != 0);

    return NATS_OK;
}
