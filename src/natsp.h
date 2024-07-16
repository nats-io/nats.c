// Copyright 2015-2022 The NATS Authors
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

#ifndef NATSP_H_
#define NATSP_H_

#if defined(_WIN32)
#include "include/n-win.h"
#else
#include "include/n-unix.h"
#endif

#include "nats/nats.h"
#include "dev_mode.h"

#define SSL void *
#define SSL_free(c) \
    {               \
        (c) = NULL; \
    }
#define SSL_CTX void *
#define SSL_CTX_free(c) \
    {                   \
        (c) = NULL;     \
    }
#define NO_SSL_ERR "The library was built without SSL support!"

#define LIB_NATS_VERSION_STRING NATS_VERSION_STRING
#define LIB_NATS_VERSION_NUMBER NATS_VERSION_NUMBER
#define LIB_NATS_VERSION_REQUIRED_NUMBER NATS_VERSION_REQUIRED_NUMBER

#define CLangString "C"

#define STILL_OK(_s) ((_s) == NATS_OK)
#define NOT_OK(_s) ((_s) != NATS_OK)
#define ALWAYS_OK(_e) ((_e), NATS_OK)
#define IFOK(s, c)  \
    if (STILL_OK(s)) \
    {               \
        s = (c);    \
    }
#define IFNULL(_e, _err) ((_e) == NULL ? (_err) : NATS_OK)
#define CHECK_NO_MEMORY(_e) IFNULL((_e), NATS_NO_MEMORY)

#define NATS_MILLIS_TO_NANOS(d) (((int64_t)d) * (int64_t)1E6)
#define NATS_SECONDS_TO_NANOS(d) (((int64_t)d) * (int64_t)1E9)

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

//-----------------------------------------------------------------------------
// Types

typedef struct __nats_JSON_s nats_JSON;
typedef struct __natsJSONParser_s natsJSONParser;
typedef struct __natsHash natsHash;
typedef struct __natsHashIter natsHashIter;
typedef struct __natsParser natsParser;
typedef struct __natsServer_s natsServer;
typedef struct __natsServerInfo natsServerInfo;
typedef struct __natsServers_s natsServers;
typedef struct __natsSockCtx natsSockCtx;
typedef struct __natsStrHash natsStrHash;
typedef struct __natsStrHashIter natsStrHashIter;

//-----------------------------------------------------------------------------
// Library

natsHeap *nats_globalHeap(void);
natsPool *nats_globalPool(void);
int64_t nats_now(void);
int64_t nats_nowInNanoSeconds(void);
natsStatus nats_open(void);
void nats_releaseGlobalLib(void);
void nats_retainGlobalLib(void);
int64_t nats_setTargetTime(int64_t timeout);
void nats_sleep(int64_t sleepTime);
void nats_sysInit(void);

//-----------------------------------------------------------------------------
// Other includes

#include "err.h"
#include "util.h"
#include "mem.h"

#define _CRLF_ "\r\n"
#define _SPC_ " "

#define _CRLF_LEN_ (sizeof(_CRLF_) - 1)
#define _SPC_LEN_ (sizeof(_SPC_) - 1)

// These depend on mem.h but are used elsewhere, so define here.
static const natsString nats_CRLF = NATS_STR(_CRLF_);
static const natsString nats_SPACE = NATS_STR(_SPC_);
// static const natsString nats_OK = NATS_STR("+OK");
static const natsString nats_ERR = NATS_STR("-ERR");
// static const natsString nats_MSG = NATS_STR("MSG");
static const natsString nats_PING_CRLF = NATS_STR("PING" _CRLF_);
static const natsString nats_PONG_CRLF = NATS_STR("PONG" _CRLF_);
// static const natsString nats_INFO = NATS_STR("INFO");
static const natsString nats_PUB = NATS_STR("PUB");
static const natsString nats_HPUB = NATS_STR("HPUB");

#endif /* NATSP_H_ */
