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

#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>

//#include "natsp.h"
#include "status.h"

struct __natsTimer;

// Callback signature for timer
typedef void (*natsTimerCb)(struct __natsTimer *timer, void* closure);

// Callback invoked when the timer has been stopped and guaranteed
// not to be in the timer callback.
typedef void (*natsTimerStopCb)(struct __natsTimer *timer, void* closure);

typedef struct __natsTimer
{
    struct __natsTimer  *prev;
    struct __natsTimer  *next;

    natsMutex           *mu;
    int                 refs;

    natsTimerCb         cb;
    natsTimerStopCb     stopCb;
    void*               closure;

    int64_t             interval;
    int64_t             absoluteTime;

    bool                stopped;
    bool                inCallback;

} natsTimer;

natsStatus
natsTimer_Create(natsTimer **timer, natsTimerCb timerCb, natsTimerStopCb stopCb,
                 int64_t interval, void* closure);

void
natsTimer_Stop(natsTimer *timer);

void
natsTimer_Reset(natsTimer *timer, int64_t interval);

void
natsTimer_Release(natsTimer *t);

void
natsTimer_Destroy(natsTimer *timer);


#endif /* TIMER_H_ */
