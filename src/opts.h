// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef OPTS_H_
#define OPTS_H_

#include "natsp.h"

#define NATS_OPTS_DEFAULT_MAX_RECONNECT       (60)
#define NATS_OPTS_DEFAULT_TIMEOUT             (2 * 1000)          // 2 seconds
#define NATS_OPTS_DEFAULT_RECONNECT_WAIT      (2 * 1000)          // 2 seconds
#define NATS_OPTS_DEFAULT_PING_INTERVAL       (2 * 60 * 1000)     // 2 minutes
#define NATS_OPTS_DEFAULT_MAX_PING_OUT        (2)
#define NATS_OPTS_DEFAULT_MAX_PENDING_MSGS    (65536)
#define NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE  (8 * 1024 * 1024)   // 8 MB

natsOptions*
natsOptions_clone(natsOptions *opts);

#endif /* OPTS_H_ */
