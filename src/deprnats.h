// Copyright 2017 Apcera Inc. All rights reserved.

#ifndef SRC_DEPRNATS_H_
#define SRC_DEPRNATS_H_

#ifndef _WIN32
#warning "`#include <nats.h>` is deprecated. Please use `#include <nats/nats.h>`"
#else
#pragma message ("`#include <nats.h>` is deprecated. Please use `#include <nats/nats.h>`")
#endif

#include <nats/nats.h>

#endif /* SRC_DEPRNATS_H_ */
