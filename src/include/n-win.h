// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef N_WIN_H_
#define N_WIN_H_

// May need this:
//#ifndef WIN32_LEAN_AND_MEAN
//#define WIN32_LEAN_AND_MEAN
//#endif

#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>


typedef HANDLE              natsThread;
typedef CRITICAL_SECTION    natsMutex;
typedef CONDITION_VARIABLE  natsCondition;
typedef SOCKET              natsSock;

#define NATS_ONCE_TYPE          INIT_ONCE
#define NATS_ONCE_STATIC_INIT   INIT_ONCE_STATIC_INIT

#define NATS_SOCK_INVALID               (INVALID_SOCKET)
#define NATS_SOCK_SHUTDOWN              shutdown((s))
#define NATS_SOCK_CLOSE(s)              closesocket((s))
#define NATS_SOCK_CONNECT_IN_PROGRESS   (WSAEWOULDBLOCK)
#define NATS_SOCK_WOULD_BLOCK           (WSAEWOULDBLOCK)
#define NATS_SOCK_ERROR                 (SOCKET_ERROR)
#define NATS_SOCK_GET_ERROR             WSAGetLastError()

#endif /* N_WIN_H_ */
