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

#define MAX_HOST_NAME   (256)

natsStatus
natsSock_ConnectTcp(natsSockCtx *ctx, const char *phost, int port)
{
    natsStatus      s    = NATS_OK;
    int             res;
    char            sport[6];
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *p;
    bool            waitForConnect = false;
    bool            error = false;
    int             i;
    int             max = 2;
    char            hosta[MAX_HOST_NAME];
    int             hostLen;
    char            *host;

    if (phost == NULL)
        return nats_setError(NATS_ADDRESS_MISSING, "%s", "No host specified");

    hostLen = (int) strlen(phost);
    if ((hostLen == 0) || ((hostLen == 1) && phost[0] == '['))
        return nats_setError(NATS_INVALID_ARG, "Invalid host name: %s", phost);

    if (phost[0] == '[')
    {
        snprintf(hosta, sizeof(hosta), "%.*s", hostLen - 2, phost + 1);
        host = (char*) hosta;
    }
    else
        host = (char*) phost;

    snprintf(sport, sizeof(sport), "%d", port);

    if ((ctx->orderIP == 4) || (ctx->orderIP == 6))
        max = 1;

    for (i=0; i<max; i++)
    {
        memset(&hints,0,sizeof(hints));
        hints.ai_socktype = SOCK_STREAM;

        switch (ctx->orderIP)
        {
            case  4: hints.ai_family = AF_INET; break;
            case  6: hints.ai_family = AF_INET6; break;
            case 46: hints.ai_family = (i == 0 ? AF_INET : AF_INET6); break;
            case 64: hints.ai_family = (i == 0 ? AF_INET6 : AF_INET); break;
            default: hints.ai_family = AF_UNSPEC;
        }

        s = NATS_OK;
        if ((res = getaddrinfo(host, sport, &hints, &servinfo)) != 0)
        {
            s = nats_setError(NATS_SYS_ERROR, "getaddrinfo error: %s",
                              gai_strerror(res));
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
                && (NATS_SOCK_GET_ERROR == NATS_SOCK_CONNECT_IN_PROGRESS))
            {
                if ((natsSock_WaitReady(WAIT_FOR_CONNECT, ctx) != NATS_OK)
                    || !natsSock_IsConnected(ctx->fd))
                {
                    error = true;
                }
            }
            else if (res == NATS_SOCK_ERROR)
            {
                error = true;
            }

            if (error)
            {
                _closeFd(ctx->fd);
                ctx->fd = NATS_SOCK_INVALID;
                continue;
            }

            s = natsSock_SetCommonTcpOptions(ctx->fd);
            if (s == NATS_OK)
                break;
        }

        if (s == NATS_OK)
        {
            if (ctx->fd == NATS_SOCK_INVALID)
                s = nats_setDefaultError(NATS_NO_SERVER);
        }

        freeaddrinfo(servinfo);
        servinfo = NULL;

        if (s == NATS_OK)
        {
            // Clear the error stack in case we got errors in the loop until
            // being able to successfully connect.
            nats_clearLastError();
            break;
        }
        else
        {
            _closeFd(ctx->fd);
            ctx->fd = NATS_SOCK_INVALID;
        }
    }

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
                else
                {
                    // SSL requires that we go back with the same buffer
                    // and size. We can't return until SSL_read returns
                    // success (bytes read) or a different error.
                    continue;
                }
            }
            else
#endif
                if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
            {
                return nats_setError(NATS_IO_ERROR, "recv error: %d",
                                     NATS_SOCK_GET_ERROR);
            }
            else if (ctx->useEventLoop)
            {
                // When using an external event loop, we are done. We will be
                // called again...
                if (n != NULL)
                    *n = 0;

                return NATS_OK;
            }

            // For non-blocking sockets, if the read would block, we need to
            // wait up to the deadline.
            s = natsSock_WaitReady(WAIT_FOR_READ, ctx);
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
natsSock_Write(natsSockCtx *ctx, const char *data, int len, int *n)
{
    natsStatus  s         = NATS_OK;
    int         bytes     = 0;
    bool        needWrite = true;

    while (needWrite)
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
                else
                {
                    // SSL requires that we go back with the same buffer
                    // and size. We can't return until SSL_write returns
                    // success (bytes written) a different error.
                    continue;
                }
            }
            else
#endif
                if (NATS_SOCK_GET_ERROR != NATS_SOCK_WOULD_BLOCK)
            {
                return nats_setError(NATS_IO_ERROR, "send error: %d",
                                     NATS_SOCK_GET_ERROR);
            }
            else if (ctx->useEventLoop)
            {
                // With external event loop, we are done now, we will be
                // called later for more.
                if (n != NULL)
                    *n = 0;

                return NATS_OK;
            }

            // For non-blocking sockets, if the write would block, we need to
            // wait up to the deadline.
            s = natsSock_WaitReady(WAIT_FOR_WRITE, ctx);
            if (s != NATS_OK)
                return NATS_UPDATE_ERR_STACK(s);

            continue;
        }

        if (n != NULL)
            *n = bytes;

        needWrite = false;
    }

    return NATS_OK;
}

natsStatus
natsSock_WriteFully(natsSockCtx *ctx, const char *data, int len)
{
    natsStatus  s     = NATS_OK;
    int         bytes = 0;
    int         n     = 0;

    do
    {
        s = natsSock_Write(ctx, data, len, &n);
        if (s == NATS_OK)
        {
            if (n > 0)
            {
                data += n;
                len  -= n;
            }

            // We use an external event loop and got nothing, or we have
            // sent the whole buffer. Return.
            if ((n == 0) || (len == 0))
                return NATS_OK;
        }
    }
    while (s == NATS_OK);

    return NATS_UPDATE_ERR_STACK(s);
}
