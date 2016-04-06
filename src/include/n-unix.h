// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef N_UNIX_H_
#define N_UNIX_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef DARWIN
#define FD_SETSIZE  (32768)
#else
#include <sys/types.h>
#undef __FD_SETSIZE
#define __FD_SETSIZE (32768)
#include <sys/select.h>
#endif

#include <sys/time.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

typedef pthread_t       natsThread;
typedef pthread_key_t   natsThreadLocal;
typedef pthread_mutex_t natsMutex;
typedef pthread_cond_t  natsCondition;
typedef pthread_once_t  natsInitOnceType;
typedef socklen_t       natsSockLen;
typedef size_t          natsRecvLen;

#define NATS_ONCE_STATIC_INIT   PTHREAD_ONCE_INIT

#define NATS_SOCK_INVALID               (-1)
#define NATS_SOCK_SHUTDOWN(s)           (shutdown((s), SHUT_RDWR))
#define NATS_SOCK_CLOSE(s)              (close((s)))
#define NATS_SOCK_CONNECT_IN_PROGRESS   (EINPROGRESS)
#define NATS_SOCK_WOULD_BLOCK           (EWOULDBLOCK)
#define NATS_SOCK_ERROR                 (-1)
#define NATS_SOCK_GET_ERROR             (errno)

#define nats_asprintf       asprintf
#define nats_strcasestr     strcasestr
#define nats_strcasecmp     strcasecmp

#endif /* N_UNIX_H_ */
