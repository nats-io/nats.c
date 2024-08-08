// Copyright 2015-2024 The NATS Authors
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

#ifndef GLIB_H_
#define GLIB_H_

typedef struct __natsDispatcherPool natsDispatcherPool;

// Functions that are used internally by the library.

void natsLib_Retain(void);
void natsLib_Release(void);

natsStatus nats_assignSubToDispatch(natsSubscription *sub);
natsStatus nats_closeLib(bool wait, int64_t timeout);
natsStatus nats_initSSL(void);
natsStatus nats_openLib(natsClientConfig *config);
natsStatus nats_postAsyncCbInfo(natsAsyncCbInfo *info);
natsStatus nats_setMessageDispatcherPoolCap(int max);

void nats_initForOS(void);
void nats_overrideDefaultOptionsWithConfig(natsOptions *opts);
void nats_resetTimer(natsTimer *t, int64_t newInterval);
void nats_stopTimer(natsTimer *t);

// Returns the number of timers that have been created and not stopped.
int nats_getTimersCount(void);

// Returns the number of timers actually in the list. This should be
// equal to nats_getTimersCount() or nats_getTimersCount() - 1 when a
// timer thread is invoking a timer's callback.
int nats_getTimersCountInList(void);

#endif // GLIB_H_
