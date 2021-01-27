// Copyright 2015-2019 The NATS Authors
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

#define TYPE_NOT_SET    (0)
#define TYPE_STR        (1)
#define TYPE_BOOL       (2)
#define TYPE_NUM        (3)
#define TYPE_INT        (4)
#define TYPE_LONG       (5)
#define TYPE_DOUBLE     (6)
#define TYPE_ARRAY      (7)
#define TYPE_ULONG      (8)

typedef struct
{
    void    **values;
    int     typ;
    int     eltSize;
    int     size;
    int     cap;

} nats_JSONArray;

typedef struct
{
    char    *name;
    int     typ;
    union
    {
            char            *vstr;
            bool            vbool;
            long double     vdec;
            nats_JSONArray  *varr;
    } value;

} nats_JSONField;

typedef struct
{
    char        *str;
    natsStrHash *fields;

} nats_JSON;

#define nats_IsStringEmpty(s) (((s == NULL) || (s[0] == '\0')) ? true : false)

#define snprintf_truncate(d, szd, f, ...) if (snprintf((d), (szd), (f), __VA_ARGS__) >= (int) (szd)) { \
    int offset = (int) (szd) - 2;         \
    if (offset > 0) (d)[offset--] = '.';  \
    if (offset > 0) (d)[offset--] = '.';  \
    if (offset > 0) (d)[offset--] = '.';  \
}

int64_t
nats_ParseInt64(const char *d, int dLen);

natsStatus
nats_ParsePort(int *port, const char *sport);

natsStatus
nats_Trim(char **pres, const char *s);

natsStatus
nats_ParseControl(natsControl *control, const char *line);

natsStatus
nats_CreateStringFromBuffer(char **newStr, natsBuffer *buf);

const char*
nats_GetBoolStr(bool value);

void
nats_NormalizeErr(char *error);

natsStatus
nats_JSONParse(nats_JSON **json, const char *str, int strLen);

natsStatus
nats_JSONGetField(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField);

natsStatus
nats_JSONGetStr(nats_JSON *json, const char *fieldName, char **value);

natsStatus
nats_JSONGetInt(nats_JSON *json, const char *fieldName, int *value);

natsStatus
nats_JSONGetBool(nats_JSON *json, const char *fieldName, bool *value);

natsStatus
nats_JSONGetLong(nats_JSON *json, const char *fieldName, int64_t *value);

natsStatus
nats_JSONGetULong(nats_JSON *json, const char *fieldName, uint64_t *value);

natsStatus
nats_JSONGetDouble(nats_JSON *json, const char *fieldName, long double *value);

natsStatus
nats_JSONGetArrayField(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField);

natsStatus
nats_JSONGetArrayStr(nats_JSON *json, const char *fieldName, char ***array, int *arraySize);

void
nats_JSONDestroy(nats_JSON *json);

void
nats_Base32_Init(void);

natsStatus
nats_Base32_DecodeString(const char *src, char *dst, int dstMax, int *dstLen);

natsStatus
nats_Base64RawURL_EncodeString(const unsigned char *src, int srcLen, char **pDest);

uint16_t
nats_CRC16_Compute(unsigned char *data, int len);

bool
nats_CRC16_Validate(unsigned char *data, int len, uint16_t expected);

natsStatus
nats_ReadFile(natsBuffer **buffer, int initBufSize, const char *fn);

bool
nats_HostIsIP(const char *host);

natsStatus
nats_GetJWTOrSeed(char **val, const char *content, int item);

void
nats_FreeAddrInfo(struct addrinfo *res);

#endif /* UTIL_H_ */
