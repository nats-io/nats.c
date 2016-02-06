// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef UTIL_H_
#define UTIL_H_

#include "natsp.h"

int64_t
nats_ParseInt64(const char *d, int dLen);

natsStatus
nats_ParseControl(natsControl *control, const char *line);

natsStatus
nats_CreateStringFromBuffer(char **newStr, natsBuffer *buf);

void
nats_Randomize(int *array, int arraySize);

const char*
nats_GetBoolStr(bool value);

void
nats_NormalizeErr(char *error);

#endif /* UTIL_H_ */
