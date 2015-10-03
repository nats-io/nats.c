// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef SOCK_H_
#define SOCK_H_

#include "natsp.h"

natsStatus
natsSock_ConnectTcp(natsSock *fd, fd_set *fdSet, natsDeadline *deadline,
                    const char *host, int port);

natsStatus
natsSock_SetBlocking(natsSock fd, bool blocking);

natsStatus
natsSock_CreateFDSet(fd_set **newFDSet);

void
natsSock_DestroyFDSet(fd_set *fdSet);

bool
natsSock_IsConnected(natsSock fd);

natsStatus
natsSock_ReadLine(fd_set *fdSet, natsSock fd, natsDeadline *deadline,
                  char *buffer, size_t maxBufferSize, int *n);

natsStatus
natsSock_Read(natsSock fd, char *buffer, size_t maxBufferSize, int *n);

// Writes 'len' bytes to the socket. Does not return until all bytes
// have been written, unless the socket is closed or an error occurs.
natsStatus
natsSock_WriteFully(fd_set *fdSet, natsSock fd, natsDeadline *deadline,
                    const char *data, int len, int *n);

natsStatus
natsSock_Flush(natsSock fd);

void
natsSock_Close(natsSock fd);


#endif /* SOCK_H_ */
