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

NATS_EXTERN natsStatus
natsOptions_Create(natsOptions **newOpts);

natsOptions*
natsOptions_clone(natsOptions *opts);

NATS_EXTERN natsStatus
natsOptions_SetURL(natsOptions *opts, const char *url);

NATS_EXTERN natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount);

NATS_EXTERN natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize);

NATS_EXTERN natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout);

NATS_EXTERN natsStatus
natsOptions_SetName(natsOptions *opts, const char *name);

NATS_EXTERN natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose);

NATS_EXTERN natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic);

NATS_EXTERN natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval);

NATS_EXTERN natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPignsOut);

NATS_EXTERN natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow);

NATS_EXTERN natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect);

NATS_EXTERN natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait);

NATS_EXTERN natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending);

NATS_EXTERN natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure);

NATS_EXTERN natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure);

NATS_EXTERN natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure);

NATS_EXTERN natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure);

NATS_EXTERN void
natsOptions_Destroy(natsOptions *opts);

#endif /* OPTS_H_ */
