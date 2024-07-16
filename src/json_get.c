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

#include "natsp.h"

#include "hash.h"
#include "json.h"

#define JSON_GET_AS(jt, t)                                \
    natsStatus s = NATS_OK;                               \
    nats_JSONField *field = NULL;                         \
    s = nats_JSONRefField(json, fieldName, (jt), &field); \
    if ((STILL_OK(s)) && (field == NULL))                \
    {                                                     \
        *value = 0;                                       \
        return NATS_OK;                                   \
    }                                                     \
    else if (STILL_OK(s))                                \
    {                                                     \
        switch (field->numTyp)                            \
        {                                                 \
        case TYPE_INT:                                    \
            *value = (t)field->value.vint;                \
            break;                                        \
        case TYPE_UINT:                                   \
            *value = (t)field->value.vuint;               \
            break;                                        \
        default:                                          \
            *value = (t)field->value.vdec;                \
        }                                                 \
    }                                                     \
    return NATS_UPDATE_ERR_STACK(s);

#define JSON_ARRAY_AS(_p, _t)                                        \
    int i;                                                           \
    _t *values = (_t *)natsPool_alloc((_p), arr->size * sizeof(_t)); \
    if (values == NULL)                                              \
        return nats_setDefaultError(NATS_NO_MEMORY);                 \
    for (i = 0; i < arr->size; i++)                                  \
        values[i] = ((_t *)arr->values)[i];                          \
    *array = values;                                                 \
    *arraySize = arr->size;                                          \
    return NATS_OK;

#define JSON_ARRAY_AS_NUM(_p, _t)                                        \
    int i;                                                               \
    _t *values = (_t *)natsPool_alloc((_p), arr->size * sizeof(_t));     \
    if (values == NULL)                                                  \
        return nats_setDefaultError(NATS_NO_MEMORY);                     \
    for (i = 0; i < arr->size; i++)                                      \
    {                                                                    \
        void *ptr = NULL;                                                \
        ptr = (void *)((char *)(arr->values) + (i * JSON_MAX_NUM_SIZE)); \
        values[i] = *(_t *)ptr;                                          \
    }                                                                    \
    *array = values;                                                     \
    *arraySize = arr->size;                                              \
    return NATS_OK;

#define JSON_GET_ARRAY(_p, _t, _f)                           \
    natsStatus s = NATS_OK;                                  \
    nats_JSONField *field = NULL;                            \
    s = nats_JSONRefArray(json, fieldName, (_t), &field);    \
    if ((STILL_OK(s)) && (field == NULL))                   \
    {                                                        \
        *array = NULL;                                       \
        *arraySize = 0;                                      \
        return NATS_OK;                                      \
    }                                                        \
    else if (STILL_OK(s))                                   \
        s = (_f)((_p), field->value.varr, array, arraySize); \
    return NATS_UPDATE_ERR_STACK(s);

natsStatus
nats_JSONRefField(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField)
{
    nats_JSONField *field = NULL;

    field = (nats_JSONField *)natsStrHash_Get(json->fields, (char *)fieldName, nats_strlen(fieldName));
    if ((field == NULL) || (field->typ == TYPE_NULL))
    {
        *retField = NULL;
        return NATS_OK;
    }

    // Check parsed type matches what is being asked.
    switch (fieldType)
    {
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_DOUBLE:
        if (field->typ != TYPE_NUM)
            return nats_setError(NATS_INVALID_ARG,
                                 "Asked for field '%s' as type %d, but got type %d when parsing",
                                 field->name, fieldType, field->typ);
        break;
    case TYPE_BOOL:
    case TYPE_STR:
    case TYPE_OBJECT:
        if (field->typ != fieldType)
            return nats_setError(NATS_INVALID_ARG,
                                 "Asked for field '%s' as type %d, but got type %d when parsing",
                                 field->name, fieldType, field->typ);
        break;
    default:
        return nats_setError(NATS_INVALID_ARG,
                             "Asked for field '%s' as type %d, but this type does not exist",
                             field->name, fieldType);
    }
    *retField = field;
    return NATS_OK;
}

natsStatus
nats_JSONDupStrIfDiff(nats_JSON *json, natsPool *pool, const char *fieldName, const char **value)
{
    natsStatus s = NATS_OK;
    nats_JSONField *field = NULL;

    s = nats_JSONRefField(json, fieldName, TYPE_STR, &field);
    if ((field == NULL) || (field->value.vstr == NULL))
    {
        *value = NULL;
        return NATS_UPDATE_ERR_STACK(s);
    }

    if ((*value != NULL) && (strcmp(*value, field->value.vstr) == 0))
        return NATS_OK;

    char *tmp = nats_pstrdupC(pool, field->value.vstr);
    if (tmp == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
        *value = tmp;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_JSONDupStr(nats_JSON *json, natsPool *pool, const char *fieldName, const char **value)
{
    natsStatus s = NATS_OK;
    nats_JSONField *field = NULL;

    s = nats_JSONRefField(json, fieldName, TYPE_STR, &field);
    if ((field == NULL) || (field->value.vstr == NULL))
    {
        *value = NULL;
        return NATS_UPDATE_ERR_STACK(s);
    }

    char *tmp = nats_pstrdupC(pool, field->value.vstr);
    if (tmp == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
        *value = tmp;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_JSONRefStr(nats_JSON *json, const char *fieldName, const char **str)
{
    natsStatus s;
    nats_JSONField *field = NULL;

    s = nats_JSONRefField(json, fieldName, TYPE_STR, &field);
    if (STILL_OK(s))
    {
        if (field == NULL)
            *str = NULL;
        else
            *str = field->value.vstr;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

// natsStatus
// nats_JSONGetBytes(nats_JSON *json, const char *fieldName, unsigned char **value, int *len)
// {
//     natsStatus s;
//     const char *str = NULL;

//     *value = NULL;
//     *len = 0;

//     s = nats_JSONRefStr(json, fieldName, &str);
//     if ((STILL_OK(s)) && (str != NULL))
//         s = nats_Base64_Decode(str, value, len);
//     return NATS_UPDATE_ERR_STACK(s);
// }

natsStatus
nats_JSONGetInt(nats_JSON *json, const char *fieldName, int *value)
{
    JSON_GET_AS(TYPE_INT, int);
}

natsStatus
nats_JSONGetInt32(nats_JSON *json, const char *fieldName, int32_t *value)
{
    JSON_GET_AS(TYPE_INT, int32_t);
}

natsStatus
nats_JSONGetUInt16(nats_JSON *json, const char *fieldName, uint16_t *value)
{
    JSON_GET_AS(TYPE_UINT, uint16_t);
}

natsStatus
nats_JSONGetBool(nats_JSON *json, const char *fieldName, bool *value)
{
    natsStatus s = NATS_OK;
    nats_JSONField *field = NULL;

    s = nats_JSONRefField(json, fieldName, TYPE_BOOL, &field);
    if (STILL_OK(s))
    {
        *value = (field == NULL ? false : field->value.vbool);
        return NATS_OK;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_JSONGetLong(nats_JSON *json, const char *fieldName, int64_t *value)
{
    JSON_GET_AS(TYPE_INT, int64_t);
}

natsStatus
nats_JSONGetULong(nats_JSON *json, const char *fieldName, uint64_t *value)
{
    JSON_GET_AS(TYPE_UINT, uint64_t);
}

natsStatus
nats_JSONGetDouble(nats_JSON *json, const char *fieldName, long double *value)
{
    JSON_GET_AS(TYPE_DOUBLE, long double);
}

natsStatus
nats_JSONRefObject(nats_JSON *json, const char *fieldName, nats_JSON **value)
{
    natsStatus s = NATS_OK;
    nats_JSONField *field = NULL;

    s = nats_JSONRefField(json, fieldName, TYPE_OBJECT, &field);
    if (STILL_OK(s))
    {
        *value = (field == NULL ? NULL : field->value.vobj);
        return NATS_OK;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

// natsStatus
// nats_JSONGetTime(nats_JSON *json, const char *fieldName, int64_t *timeUTC)
// {
//     natsStatus s = NATS_OK;
//     char *str = NULL;

//     s = nats_JSONGetStr(json, fieldName, &str);
//     if ((STILL_OK(s)) && (str == NULL))
//     {
//         *timeUTC = 0;
//         return NATS_OK;
//     }
//     else if (s != NATS_OK)
//         return NATS_UPDATE_ERR_STACK(s);

//     s = nats_parseTime(str, timeUTC);
//     NATS_FREE(str);
//     return NATS_UPDATE_ERR_STACK(s);
// }

natsStatus
nats_JSONRefArray(nats_JSON *json, const char *fieldName, int fieldType, nats_JSONField **retField)
{
    nats_JSONField *field = NULL;

    field = (nats_JSONField *)natsStrHash_Get(json->fields, (char *)fieldName, nats_strlen(fieldName));
    if ((field == NULL) || (field->typ == TYPE_NULL))
    {
        *retField = NULL;
        return NATS_OK;
    }

    // Check parsed type matches what is being asked.
    if (field->typ != TYPE_ARRAY)
        return nats_setError(NATS_INVALID_ARG,
                             "Field '%s' is not an array, it has type: %d",
                             field->name, field->typ);
    // If empty array, return NULL/OK
    if (field->value.varr->typ == TYPE_NULL)
    {
        *retField = NULL;
        return NATS_OK;
    }
    if (fieldType != field->value.varr->typ)
        return nats_setError(NATS_INVALID_ARG,
                             "Asked for field '%s' as an array of type: %d, but it is an array of type: %d",
                             field->name, fieldType, field->typ);

    *retField = field;
    return NATS_OK;
}

static natsStatus
_jsonArrayAsStringsIfDiff(natsPool *pool, nats_JSONArray *arr, const char ***array, int *arraySize)
{
    int i;
    bool diffSize = (*arraySize != arr->size);
    bool diff = diffSize;

    if (!diffSize)
    {
        for (i = 0; i < arr->size; i++)
            if (strcmp((char *)(arr->values[i]), (*array)[i]) != 0)
            {
                diff = true;
                break;
            }
    }
    if (!diff)
        return NATS_OK;

    const char **values = *array;
    if (*arraySize < arr->size)
    {
        values = (const char **)nats_palloc(pool, arr->size * sizeof(char *));
        if (values == NULL)
            return NATS_UPDATE_ERR_STACK(nats_setDefaultError(NATS_NO_MEMORY));
    }

    for (i = 0; i < arr->size; i++)
    {
        if ((values[i] != NULL) && strcmp((char *)(arr->values[i]), values[i]) != 0)
        {
            values[i] = nats_pstrdupC(pool, (char *)(arr->values[i]));
            if (values[i] == NULL)
                return NATS_UPDATE_ERR_STACK(nats_setDefaultError(NATS_NO_MEMORY));
        }
    }

    *array = values;
    *arraySize = arr->size;
    return NATS_OK;
}

natsStatus
nats_JSONDupStringArrayIfDiff(nats_JSON *json, natsPool *pool, const char *fieldName, const char ***array, int *arraySize)
{
    JSON_GET_ARRAY(pool, TYPE_STR, _jsonArrayAsStringsIfDiff);
}

// static natsStatus
// _jsonArrayAsStringPtrs(natsPool *pool, nats_JSONArray *arr, const char ***array, int *arraySize)
// {
//     *array = (const char **)arr->values;
//     *arraySize = arr->size;
// }

// natsStatus
// nats_JSONGetArrayStrPtr(nats_JSON *json, const char *fieldName, char ***array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_STR, _jsonArrayAsStringPtrs);
// }

// static natsStatus
// _jsonArrayAsBools(natsPool *pool, nats_JSONArray *arr, bool **array, int *arraySize)
// {
//     JSON_ARRAY_AS(pool, bool);
// }

// natsStatus
// nats_JSONGetArrayBool(nats_JSON *json, const char *fieldName, bool **array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_BOOL, _jsonArrayAsBools);
// }

// static natsStatus
// _jsonArrayAsDoubles(natsPool *pool, nats_JSONArray *arr, long double **array, int *arraySize)
// {
//     JSON_ARRAY_AS_NUM(pool, long double);
// }

// natsStatus
// nats_JSONGetArrayDouble(nats_JSON *json, const char *fieldName, long double **array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_NUM, _jsonArrayAsDoubles);
// }

// static natsStatus
// _jsonArrayAsInts(natsPool *pool, nats_JSONArray *arr, int **array, int *arraySize)
// {
//     JSON_ARRAY_AS_NUM(pool, int);
// }

// natsStatus
// nats_JSONGetArrayInt(nats_JSON *json, const char *fieldName, int **array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_NUM, _jsonArrayAsInts);
// }

// static natsStatus
// _jsonArrayAsLongs(natsPool *pool, nats_JSONArray *arr, int64_t **array, int *arraySize)
// {
//     JSON_ARRAY_AS_NUM(pool, int64_t);
// }

// natsStatus
// nats_JSONGetArrayLong(nats_JSON *json, const char *fieldName, int64_t **array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_NUM, _jsonArrayAsLongs);
// }

// static natsStatus
// _jsonArrayAsULongs(natsPool *pool, nats_JSONArray *arr, uint64_t **array, int *arraySize)
// {
//     JSON_ARRAY_AS_NUM(pool, uint64_t);
// }

// natsStatus
// nats_JSONGetArrayULong(nats_JSON *json, const char *fieldName, uint64_t **array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_NUM, _jsonArrayAsULongs);
// }

// static natsStatus
// _jsonArrayAsObjects(natsPool *pool, nats_JSONArray *arr, nats_JSON ***array, int *arraySize)
// {
//     JSON_ARRAY_AS(pool, nats_JSON *);
// }

// natsStatus
// nats_JSONGetArrayObject(nats_JSON *json, const char *fieldName, nats_JSON ***array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_OBJECT, _jsonArrayAsObjects);
// }

// static natsStatus
// _jsonArrayAsArrays(natsPool *pool, nats_JSONArray *arr, nats_JSONArray ***array, int *arraySize)
// {
//     JSON_ARRAY_AS(pool, nats_JSONArray *);
// }

// natsStatus
// nats_JSONGetArrayArray(nats_JSON *json, const char *fieldName, nats_JSONArray ***array, int *arraySize)
// {
//     JSON_GET_ARRAY(TYPE_ARRAY, _jsonArrayAsArrays);
// }

natsStatus
nats_JSONRange(nats_JSON *json, int expectedType, int expectedNumType, jsonRangeCB cb, void *userInfo)
{
    natsStrHashIter iter;
    char *fname = NULL;
    void *val = NULL;
    natsStatus s = NATS_OK;

    natsStrHashIter_Init(&iter, json->fields);
    while ((STILL_OK(s)) && natsStrHashIter_Next(&iter, &fname, &val))
    {
        nats_JSONField *f = (nats_JSONField *)val;

        if (f->typ != expectedType)
            s = nats_setError(NATS_ERR, "field '%s': expected value type of %d, got %d",
                              f->name, expectedType, f->typ);
        else if ((f->typ == TYPE_NUM) && (f->numTyp != expectedNumType))
            s = nats_setError(NATS_ERR, "field '%s': expected numeric type of %d, got %d",
                              f->name, expectedNumType, f->numTyp);
        else
            s = cb(userInfo, (const char *)f->name, f);
    }
    natsStrHashIter_Done(&iter);
    return NATS_UPDATE_ERR_STACK(s);
}
