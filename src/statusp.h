// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef STATUSP_H_
#define STATUSP_H_

#include "status.h"

#define NATS_SET_ERROR(s)   natsSetError((s), __FILE__, __LINE__)

natsStatus
natsSetError(natsStatus s, char* file, int line);

#endif /* STATUSP_H_ */
