// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef ASYNCCB_H_
#define ASYNCCB_H_

#include "status.h"

typedef enum
{
    ASYNC_CLOSED          = 0,
    ASYNC_DISCONNECTED,
    ASYNC_RECONNECTED,
    ASYNC_ERROR

} natsAsyncCbType;

struct __natsConnection;
struct __natsSubscription;
struct __natsAsyncCbInfo;

typedef struct __natsAsyncCbInfo
{
    natsAsyncCbType             type;
    struct __natsConnection     *nc;
    struct __natsSubscription   *sub;
    natsStatus                  err;

    struct __natsAsyncCbInfo    *next;

} natsAsyncCbInfo;

void
natsAsyncCb_PostConnHandler(struct __natsConnection *nc, natsAsyncCbType type);

void
natsAsyncCb_PostErrHandler(struct __natsConnection *nc,
                           struct __natsSubscription *sub, natsStatus err);

void
natsAsyncCb_Destroy(natsAsyncCbInfo *info);

#endif /* ASYNCCB_H_ */
