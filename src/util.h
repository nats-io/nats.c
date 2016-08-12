// Copyright 2015 Apcera Inc. All rights reserved.

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

int64_t
nats_ParseInt64(const char *d, int dLen);

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
nats_JSONGetValue(nats_JSON *json, const char *fieldName, int fieldType, void **addr);

natsStatus
nats_JSONGetArrayValue(nats_JSON *json, const char *fieldName, int fieldType, void ***array, int *arraySize);

void
nats_JSONDestroy(nats_JSON *json);

#endif /* UTIL_H_ */
