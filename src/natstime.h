// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef NATSTIME_H_
#define NATSTIME_H_

#include "natsp.h"

typedef struct __natsDeadline
{
    int64_t             absoluteTime;
    struct timeval      timeout;
    bool                active;

} natsDeadline;

int64_t
nats_Now(void);

int64_t
nats_NowInNanoSeconds(void);

void
natsDeadline_Init(natsDeadline *deadline, int64_t timeout);

struct timeval*
natsDeadline_GetTimeout(natsDeadline *deadline);

void
natsDeadline_Clear(natsDeadline *deadline);


#endif /* NATSTIME_H_ */
