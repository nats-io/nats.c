// Copyright 2024 The NATS Authors
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

#ifndef JSON_H_
#define JSON_H_

#include "natsp.h"

#include "mem.h"

#define JSON_MAX_NEXTED 100

extern int jsonMaxNested;

#define TYPE_NOT_SET (0)
#define TYPE_STR (1)
#define TYPE_BOOL (2)
#define TYPE_NUM (3)
#define TYPE_INT (4)
#define TYPE_UINT (5)
#define TYPE_DOUBLE (6)
#define TYPE_ARRAY (7)
#define TYPE_OBJECT (8)
#define TYPE_NULL (9)

// A long double memory size is larger (or equal to) u/int64_t, so use that
// as the maximum size of a num element in an array.
#define JSON_MAX_NUM_SIZE ((int)sizeof(long double))

typedef struct
{
    void **values;
    int typ;
    int eltSize;
    int size;
    int cap;

} nats_JSONArray;

struct __nats_JSON_s
{
    natsStrHash *fields;
    nats_JSONArray *array;
    natsPool *pool;
};

typedef struct
{
    char *name;
    int typ;
    union
    {
        char *vstr;
        bool vbool;
        uint64_t vuint;
        int64_t vint;
        long double vdec;
        nats_JSONArray *varr;
        nats_JSON *vobj;
    } value;
    int numTyp;

} nats_JSONField;

natsStatus
natsJSONParser_Create(natsJSONParser **parser, natsPool *pool);

// Should be called repeatedly until newJSON is initialized. If there are no
// errors, artial parsing returns NATS_OK, jsonObj set to NULL, and consumes the
// entire buf.
natsStatus
natsJSONParser_Parse(nats_JSON **jsonObj, natsJSONParser *parser, const uint8_t *data, const uint8_t *end, size_t *consumed);

natsStatus
nats_JSONRefField(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField);

natsStatus
nats_JSONDupStr(nats_JSON *json, natsPool *pool, const char *fieldName, const char **value);

natsStatus
nats_JSONDupStrIfDiff(nats_JSON *json, natsPool *pool, const char *fieldName, const char **value);

natsStatus
nats_JSONRefStr(nats_JSON *json, const char *fieldName, const char **str);

natsStatus
nats_JSONDupBytes(nats_JSON *json, natsPool *pool, const char *fieldName, unsigned char **value, int *len);

natsStatus
nats_JSONGetInt(nats_JSON *json, const char *fieldName, int *value);

natsStatus
nats_JSONGetInt32(nats_JSON *json, const char *fieldName, int32_t *value);

natsStatus
nats_JSONGetUInt16(nats_JSON *json, const char *fieldName, uint16_t *value);

natsStatus
nats_JSONGetBool(nats_JSON *json, const char *fieldName, bool *value);

natsStatus
nats_JSONGetLong(nats_JSON *json, const char *fieldName, int64_t *value);

natsStatus
nats_JSONGetULong(nats_JSON *json, const char *fieldName, uint64_t *value);

natsStatus
nats_JSONGetDouble(nats_JSON *json, const char *fieldName, long double *value);

natsStatus
nats_JSONRefObject(nats_JSON *json, const char *fieldName, nats_JSON **value);

natsStatus
nats_JSONGetTime(nats_JSON *json, const char *fieldName, int64_t *timeUTC);

natsStatus
nats_JSONRefArray(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField);

natsStatus
nats_JSONDupStringArrayIfDiff(nats_JSON *json, natsPool *pool, const char *fieldName, const char ***array, int *arraySize);

natsStatus
nats_JSONArrayAsBools(nats_JSONArray *arr, bool **array, int *arraySize);

natsStatus
nats_JSONGetArrayBool(nats_JSON *json, const char *fieldName, bool **array, int *arraySize);

natsStatus
nats_JSONArrayAsDoubles(nats_JSONArray *arr, long double **array, int *arraySize);

natsStatus
nats_JSONGetArrayDouble(nats_JSON *json, const char *fieldName, long double **array, int *arraySize);

natsStatus
nats_JSONArrayAsInts(nats_JSONArray *arr, int **array, int *arraySize);

natsStatus
nats_JSONGetArrayInt(nats_JSON *json, const char *fieldName, int **array, int *arraySize);

natsStatus
nats_JSONArrayAsLongs(nats_JSONArray *arr, int64_t **array, int *arraySize);

natsStatus
nats_JSONGetArrayLong(nats_JSON *json, const char *fieldName, int64_t **array, int *arraySize);

natsStatus
nats_JSONArrayAsULongs(nats_JSONArray *arr, uint64_t **array, int *arraySize);

natsStatus
nats_JSONGetArrayULong(nats_JSON *json, const char *fieldName, uint64_t **array, int *arraySize);

natsStatus
nats_JSONArrayAsObjects(nats_JSONArray *arr, nats_JSON ***array, int *arraySize);

natsStatus
nats_JSONGetArrayObject(nats_JSON *json, const char *fieldName, nats_JSON ***array, int *arraySize);

natsStatus
nats_JSONArrayAsArrays(nats_JSONArray *arr, nats_JSONArray ***array, int *arraySize);

natsStatus
nats_JSONGetArrayArray(nats_JSON *json, const char *fieldName, nats_JSONArray ***array, int *arraySize);

typedef natsStatus (*jsonRangeCB)(void *userInfo, const char *fieldName, nats_JSONField *f);

natsStatus
nats_JSONRange(nats_JSON *json, int expectedType, int expectedNumType, jsonRangeCB cb, void *userInfo);

natsStatus
nats_EncodeTimeUTC(char *buf, size_t bufLen, int64_t timeUTC);

natsStatus
nats_marshalLong(natsBuf *buf, bool comma, const char *fieldName, int64_t lval);

natsStatus
nats_marshalULong(natsBuf *buf, bool comma, const char *fieldName, uint64_t uval);

natsStatus
nats_marshalDuration(natsBuf *out_buf, bool comma, const char *field_name, int64_t d);

natsStatus nats_marshalConnect(natsString **out, natsConnection *nc, const char *user,
                               const char *pwd, const char *token, const char *name,
                               bool hdrs, bool noResponders);

natsStatus
nats_unmarshalServerInfo(nats_JSON *json, natsPool *pool, natsServerInfo *info);

#ifdef DEV_MODE_JSON
#define JSONDEBUG(str) DEVDEBUG("JSON", str)
#define JSONDEBUGf(fmt, ...) DEVDEBUGf("JSON", fmt, __VA_ARGS__)
#else
#define JSONDEBUG DEVNOLOG
#define JSONDEBUGf DEVNOLOGf
#endif

#endif /* JSON_H_ */
