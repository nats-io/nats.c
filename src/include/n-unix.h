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
#include <netinet/in.h>
#if defined(NATS_USE_X_LOCALE)
#include <xlocale.h>
#else
#include <locale.h>
#endif

typedef pthread_t       natsThread;
typedef pthread_key_t   natsThreadLocal;
typedef pthread_mutex_t natsMutex;
typedef pthread_cond_t  natsCondition;
typedef pthread_once_t  natsInitOnceType;
typedef socklen_t       natsSockLen;
typedef size_t          natsRecvLen;
typedef locale_t        natsLocale;

#define NATS_ONCE_STATIC_INIT   PTHREAD_ONCE_INIT

#define NATS_SOCK_INVALID               (-1)
#define NATS_SOCK_SHUTDOWN(s)           (shutdown((s), SHUT_RDWR))
#define NATS_SOCK_CLOSE(s)              (close((s)))
#define NATS_SOCK_CONNECT_IN_PROGRESS   (EINPROGRESS)
#define NATS_SOCK_WOULD_BLOCK           (EWOULDBLOCK)
#define NATS_SOCK_ERROR                 (-1)
#define NATS_SOCK_GET_ERROR             (errno)

#define __NATS_FUNCTION__ __func__

#define nats_asprintf       asprintf
#define nats_strcasestr     strcasestr
#define nats_vsnprintf      vsnprintf
#define nats_strtold(p, t)  strtold_l((p), (t), (natsLib_getLocale()))

#endif /* N_UNIX_H_ */
