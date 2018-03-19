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

#ifdef _WIN32
#include <sys/timeb.h>
#endif
#include <time.h>

int64_t
nats_Now(void)
{
#ifdef _WIN32
    struct _timeb now;
    _ftime_s(&now);
    return (((int64_t)now.time) * 1000 + now.millitm);
#elif defined CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        abort();
    return ((int64_t)ts.tv_sec) * 1000 + (((int64_t)ts.tv_nsec) / 1000000);
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        abort();
    return ((int64_t)tv.tv_sec) * 1000 + (((int64_t)tv.tv_usec) / 1000);
#endif
}

int64_t
nats_NowInNanoSeconds(void)
{
#ifdef _WIN32
    struct _timeb now;
    _ftime_s(&now);
    return (((int64_t)now.time) * 1000 + now.millitm) * 1000000L;
#elif defined CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        abort();
    return ((int64_t)ts.tv_sec) * 1000000000L + ((int64_t)ts.tv_nsec);
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        abort();
    return ((int64_t)tv.tv_sec) * 1000000000L + (((int64_t)tv.tv_usec) * 1000);
#endif
}

void
natsDeadline_Init(natsDeadline *deadline, int64_t timeout)
{
    deadline->active          = true;
    deadline->absoluteTime    = nats_Now() + timeout;
    deadline->timeout.tv_sec  = (long) timeout / 1000;
    deadline->timeout.tv_usec = (timeout % 1000) * 1000;
}

void
natsDeadline_Clear(natsDeadline *deadline)
{
    deadline->active = false;
}

struct timeval*
natsDeadline_GetTimeout(natsDeadline *deadline)
{
    int64_t timeout;

    if (!(deadline->active))
        return NULL;

    timeout = deadline->absoluteTime - nats_Now();

    deadline->timeout.tv_sec  = (long) (timeout / 1000);
    deadline->timeout.tv_usec = (timeout % 1000) * 1000;

    return &(deadline->timeout);
}
