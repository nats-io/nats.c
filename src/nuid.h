// Copyright 2016 Apcera Inc. All rights reserved.

#ifndef NUID_H_
#define NUID_H_

#include "status.h"

#define NUID_BUFFER_LEN (12 + 10)

// Seed sequential random with math/random and current time and generate crypto prefix.
natsStatus
natsNUID_init(void);

// Generate the next NUID string from the global locked NUID instance.
natsStatus
natsNUID_Next(char *buffer, int bufferLen);

void
natsNUID_free(void);

#endif /* NUID_H_ */
