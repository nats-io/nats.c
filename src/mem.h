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

#ifndef MEM_H_
#define MEM_H_

#include <stdlib.h>

#define NATS_MALLOC(s)      malloc((s))
#define NATS_CALLOC(c,s)    calloc((c), (s))
#define NATS_REALLOC(p, s)  realloc((p), (s))

#ifdef _WIN32
#define NATS_STRDUP(s)      _strdup((s))
#else
#define NATS_STRDUP(s)      strdup((s))
#endif
#define NATS_FREE(p)        free((p))

// **Note** does not free the array itself.
static void NATS_FREE_STRINGS(char **strings, int count)
{
    if (strings == NULL)
        return;
    for (int i = 0; i < count; i++)
        NATS_FREE((char *)strings[i]);
}

// GNU C Library version 2.25 or later.
#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#define HAVE_EXPLICIT_BZERO 1
#endif

// Newlib
#if defined(__NEWLIB__)
#define HAVE_EXPLICIT_BZERO 1
#endif

// FreeBSD version 11.0 or later.
#if defined(__FreeBSD__) && __FreeBSD_version >= 1100037
#define HAVE_EXPLICIT_BZERO 1
#endif

// OpenBSD version 5.5 or later.
#if defined(__OpenBSD__) && OpenBSD >= 201405
#define HAVE_EXPLICIT_BZERO 1
#endif

// NetBSD version 7.2 or later.
#if defined(__NetBSD__) && __NetBSD_Version__ >= 702000000
#define HAVE_EXPLICIT_MEMSET 1
#endif

#endif /* MEM_H_ */
