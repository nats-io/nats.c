// Copyright 2015-2023 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef UTIL_H_
#define UTIL_H_

#include "natsp.h"

#include "mem.h"

int64_t
nats_ParseInt64(const char *d, int dLen);

natsStatus
nats_Trim(char **pres, natsPool *pool, const char *s);

const char*
nats_GetBoolStr(bool value);

void
nats_NormalizeErr(char *error);

void
nats_Base32_Init(void);

natsStatus
nats_Base32_DecodeString(const char *src, char *dst, int dstMax, int *dstLen);

natsStatus
nats_Base64RawURL_EncodeString(const unsigned char *src, int srcLen, char **pDest);

natsStatus
nats_Base64_Encode(const unsigned char *src, int srcLen, char **pDest);

natsStatus
nats_Base64_DecodeLen(const char *src, int *srcLen, int *dstLen);

void
nats_Base64_DecodeInPlace(const char *src, int l, unsigned char *dst);

natsStatus
nats_Base64_Decode(const char *src, unsigned char **dst, int *dstLen);

uint16_t
nats_CRC16_Compute(unsigned char *data, int len);

bool
nats_CRC16_Validate(unsigned char *data, int len, uint16_t expected);

natsStatus
nats_ReadFile(natsBuf **buffer, int initBufSize, const char *fn);

bool
nats_HostIsIP(const char *host);

void
nats_FreeAddrInfo(struct addrinfo *res);

bool
nats_isSubjectValid(const uint8_t *subject, size_t len, bool wcAllowed);

natsStatus
nats_formatStringArray(char **out, const char **strings, int count);

natsStatus
nats_parseTime(char *str, int64_t *timeUTC);

#endif /* UTIL_H_ */
