// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef MEM_H_
#define MEM_H_

#include <stdlib.h>

#define NATS_MALLOC(s)      malloc((s))
#define NATS_CALLOC(c,s)    calloc((c), (s))
#define NATS_REALLOC(p, s)  realloc((p), (s))
#define NATS_STRDUP(s)      strdup((s))
#define NATS_FREE(p)        free((p))


#endif /* MEM_H_ */
