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

natsStatus
natsOptions_Create(natsOptions **newOpts);

natsOptions*
natsOptions_clone(natsOptions *opts);

natsStatus
natsOptions_SetURL(natsOptions *opts, const char *url);

natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount);

natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize);

natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout);

natsStatus
natsOptions_SetName(natsOptions *opts, const char *name);

natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose);

natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic);

natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval);

natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPignsOut);

natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow);

natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect);

natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait);

natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending);

natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure);

natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure);

natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure);

natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure);

void
natsOptions_Destroy(natsOptions *opts);

#endif /* OPTS_H_ */
