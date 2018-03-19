// Copyright 2015-2018 The NATS Authors
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "util.h"
#include "mem.h"

#define ASCII_0 (48)
#define ASCII_9 (57)

// parseInt64 expects decimal positive numbers. We
// return -1 to signal error
int64_t
nats_ParseInt64(const char *d, int dLen)
{
    int     i;
    char    dec;
    int64_t n = 0;

    if (dLen == 0)
        return -1;

    for (i=0; i<dLen; i++)
    {
        dec = d[i];
        if ((dec < ASCII_0) || (dec > ASCII_9))
            return -1;

        n = (n * 10) + ((int64_t)dec - ASCII_0);
    }

    return n;
}

natsStatus
nats_ParseControl(natsControl *control, const char *line)
{
    natsStatus  s           = NATS_OK;
    char        *tok        = NULL;
    int         len         = 0;

    if ((line == NULL) || (line[0] == '\0'))
        return nats_setDefaultError(NATS_PROTOCOL_ERROR);

    tok = strchr(line, (int) ' ');
    if (tok == NULL)
    {
        control->op = NATS_STRDUP(line);
        if (control->op == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);

        return NATS_OK;
    }

    len = (int) (tok - line);
    control->op = NATS_MALLOC(len + 1);
    if (control->op == NULL)
    {
        s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    else
    {
        memcpy(control->op, line, len);
        control->op[len] = '\0';
    }

    if (s == NATS_OK)
    {
        // Discard all spaces and the like in between the next token
        while ((tok[0] != '\0')
               && ((tok[0] == ' ')
                   || (tok[0] == '\r')
                   || (tok[0] == '\n')
                   || (tok[0] == '\t')))
        {
            tok++;
        }
    }

    // If there is a token...
    if (tok[0] != '\0')
    {
        char *tmp;

        len = (int) strlen(tok);
        tmp = &(tok[len - 1]);

        // Remove trailing spaces and the like.
        while ((tmp[0] != '\0')
                && ((tmp[0] == ' ')
                    || (tmp[0] == '\r')
                    || (tmp[0] == '\n')
                    || (tmp[0] == '\t')))
        {
            tmp--;
            len--;
        }

        // We are sure that len is > 0 because of the first while() loop.

        control->args = NATS_MALLOC(len + 1);
        if (control->args == NULL)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            memcpy(control->args, tok, len);
            control->args[len] = '\0';
        }
    }

    if (s != NATS_OK)
    {
        NATS_FREE(control->op);
        control->op = NULL;

        NATS_FREE(control->args);
        control->args = NULL;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_CreateStringFromBuffer(char **newStr, natsBuffer *buf)
{
    char    *str = NULL;
    int     len  = 0;

    if ((buf == NULL) || ((len = natsBuf_Len(buf)) == 0))
        return NATS_OK;

    str = NATS_MALLOC(len + 1);
    if (str == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(str, natsBuf_Data(buf), len);
    str[len] = '\0';

    *newStr = str;

    return NATS_OK;
}

void
nats_Sleep(int64_t millisec)
{
#ifdef _WIN32
    Sleep((DWORD) millisec);
#else
    usleep(millisec * 1000);
#endif
}

const char*
nats_GetBoolStr(bool value)
{
    if (value)
        return "true";

    return "false";
}

void
nats_NormalizeErr(char *error)
{
    int start = 0;
    int end   = 0;
    int len   = (int) strlen(error);
    int i;

    if (strncmp(error, _ERR_OP_, _ERR_OP_LEN_) == 0)
        start = _ERR_OP_LEN_;

    for (i=start; i<len; i++)
    {
        if ((error[i] != ' ') && (error[i] != '\''))
            break;
    }

    start = i;
    if (start == len)
    {
        error[0] = '\0';
        return;
    }

    for (end=len-1; end>0; end--)
        if ((error[end] != ' ') && (error[end] != '\''))
            break;

    if (end <= start)
    {
        error[0] = '\0';
        return;
    }

    len = end - start + 1;
    memmove(error, error + start, len);
    error[len] = '\0';
}

static natsStatus
_jsonCreateField(nats_JSONField **newField, char *fieldName)
{
    nats_JSONField *field = NULL;

    field = (nats_JSONField*) NATS_CALLOC(1, sizeof(nats_JSONField));
    if (field == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    field->name = fieldName;
    field->typ  = TYPE_NOT_SET;

    *newField = field;

    return NATS_OK;
}

static void
_jsonFreeField(nats_JSONField *field)
{
    if (field->typ == TYPE_ARRAY)
    {
        NATS_FREE(field->value.varr->values);
        NATS_FREE(field->value.varr);
    }
    NATS_FREE(field);
}

static char*
_jsonTrimSpace(char *ptr)
{
    while ((*ptr != '\0')
            && ((*ptr == ' ') || (*ptr == '\t') || (*ptr == '\r') || (*ptr == '\n')))
    {
        ptr += 1;
    }
    return ptr;
}

static natsStatus
_jsonGetStr(char **ptr, char **value)
{
    char *p = *ptr;

    do
    {
        while ((*p != '\0') && (*p != '"'))
            p += 1;
    }
    while ((*p != '\0') && (*p == '"') && (*(p - 1) == '\\') && (p += 1));

    if (*p != '\0')
    {
        *value = *ptr;
        *p = '\0';
        *ptr = (char*) (p + 1);
        return NATS_OK;
    }
    return nats_setError(NATS_INVALID_ARG, "%s",
                         "error parsing string: unexpected end of JSON input");
}

static natsStatus
_jsonGetNum(char **ptr, long double *val)
{
    char        *tail = NULL;
    long double lval  = 0;

    errno = 0;

    lval = nats_strtold(*ptr, &tail);
    if (errno != 0)
        return nats_setError(NATS_INVALID_ARG,
                             "error parsing numeric: %d", errno);

    *ptr = tail;
    *val = lval;
    return NATS_OK;
}

static natsStatus
_jsonGetBool(char **ptr, bool *val)
{
    if (strncmp(*ptr, "true", 4) == 0)
    {
        *val = true;
        *ptr += 4;
        return NATS_OK;
    }
    else if (strncmp(*ptr, "false", 5) == 0)
    {
        *val = false;
        *ptr += 5;
        return NATS_OK;
    }
    return nats_setError(NATS_INVALID_ARG,
                         "error parsing boolean, got: '%s'", *ptr);
}

static natsStatus
_jsonGetArray(char **ptr, nats_JSONArray **newArray)
{
    natsStatus      s       = NATS_OK;
    char            *p      = *ptr;
    char            *val    = NULL;
    bool            end     = false;
    nats_JSONArray  array;

    // Initialize our stack variable
    memset(&array, 0, sizeof(nats_JSONArray));

    // We support only string array for now
    array.typ     = TYPE_STR;
    array.eltSize = sizeof(char*);
    array.size    = 0;
    array.cap     = 4;
    array.values  = NATS_CALLOC(array.cap, array.eltSize);

    while ((s == NATS_OK) && (*p != '\0'))
    {
        p = _jsonTrimSpace(p);

        // We support only array of strings for now
        if (*p != '"')
        {
            s = nats_setError(NATS_NOT_PERMITTED,
                              "only string arrays supported, got '%s'", p);
            break;
        }

        p += 1;

        s = _jsonGetStr(&p, &val);
        if (s != NATS_OK)
            break;

        if (array.size + 1 > array.cap)
        {
            char **newValues  = NULL;
            int newCap      = 2 * array.cap;

            newValues = (char**) NATS_REALLOC(array.values, newCap * sizeof(char*));
            if (newValues == NULL)
            {
                s = nats_setDefaultError(NATS_NO_MEMORY);
                break;
            }
            array.values = (void**) newValues;
            array.cap    = newCap;
        }
        ((char**)array.values)[array.size++] = val;

        p = _jsonTrimSpace(p);
        if (*p == '\0')
            break;

        if (*p == ']')
        {
            end = true;
            break;
        }
        else if (*p == ',')
        {
            p += 1;
        }
        else
        {
            s = nats_setError(NATS_ERR, "expected ',' got '%s'", p);
        }
    }
    if ((s == NATS_OK) && !end)
    {
        s = nats_setError(NATS_ERR,
                          "unexpected end of array: '%s'",
                          (*p != '\0' ? p : "NULL"));
    }
    if (s == NATS_OK)
    {
        *newArray = NATS_MALLOC(sizeof(nats_JSONArray));
        if (*newArray == NULL)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            memcpy(*newArray, &array, sizeof(nats_JSONArray));
            *ptr = (char*) (p + 1);
        }
    }
    if (s != NATS_OK)
    {
        int i;
        for (i=0; i<array.size; i++)
        {
            p = ((char**)array.values)[i];
            *(p + strlen(p)) = '"';
        }
        NATS_FREE(array.values);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static char*
_jsonSkipUnknownType(char *ptr)
{
    char    *p = ptr;
    int     skip = 0;
    bool    quoteOpen = false;

    while (*p != '\0')
    {
        if (((*p == ',') || (*p == '}')) && (skip == 0))
            break;
        else if ((*p == '{') || (*p == '['))
            skip++;
        else if ((*p == '}') || (*p == ']'))
            skip--;
        else if ((*p == '"') && (*(p-1) != '\\'))
        {
            if (quoteOpen)
            {
                quoteOpen = false;
                skip--;
            }
            else
            {
                quoteOpen = true;
                skip++;
            }
        }
        p += 1;
    }
    return p;
}

#define JSON_STATE_START        (0)
#define JSON_STATE_NO_FIELD_YET (1)
#define JSON_STATE_FIELD        (2)
#define JSON_STATE_SEPARATOR    (3)
#define JSON_STATE_VALUE        (4)
#define JSON_STATE_NEXT_FIELD   (5)
#define JSON_STATE_END          (6)

natsStatus
nats_JSONParse(nats_JSON **newJSON, const char *jsonStr, int jsonLen)
{
    natsStatus      s         = NATS_OK;
    nats_JSON       *json     = NULL;
    nats_JSONField  *field    = NULL;
    nats_JSONField  *oldField = NULL;
    char            *ptr;
    char            *fieldName = NULL;
    int             state;
    bool            gotEnd    = false;

    if (jsonLen < 0)
    {
        if (jsonStr == NULL)
            return nats_setDefaultError(NATS_INVALID_ARG);

        jsonLen = (int) strlen(jsonStr);
    }

    json = NATS_CALLOC(1, sizeof(nats_JSON));
    if (json == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = natsStrHash_Create(&(json->fields), 4);
    if (s == NATS_OK)
    {
        json->str = NATS_MALLOC(jsonLen + 1);
        if (json->str == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (s == NATS_OK)
        {
            memcpy(json->str, jsonStr, jsonLen);
            json->str[jsonLen] = '\0';
        }
    }
    if (s != NATS_OK)
    {
        nats_JSONDestroy(json);
        return NATS_UPDATE_ERR_STACK(s);
    }

    ptr = json->str;
    state = JSON_STATE_START;

    while ((s == NATS_OK) && (*ptr != '\0'))
    {
        ptr = _jsonTrimSpace(ptr);
        if (*ptr == '\0')
            break;
        switch (state)
        {
            case JSON_STATE_START:
            {
                // Should be the start of the JSON string
                if (*ptr != '{')
                {
                    s = nats_setError(NATS_ERR, "incorrect JSON string: '%s'", ptr);
                    break;
                }
                ptr += 1;
                state = JSON_STATE_NO_FIELD_YET;
                break;
            }
            case JSON_STATE_NO_FIELD_YET:
            case JSON_STATE_FIELD:
            {
                // Check for end, which is valid only in state == JSON_STATE_NO_FIELD_YET
                if (*ptr == '}')
                {
                    if (state == JSON_STATE_NO_FIELD_YET)
                    {
                        ptr += 1;
                        state = JSON_STATE_END;
                        break;
                    }
                    s = nats_setError(NATS_ERR,
                                      "expected beginning of field, got: '%s'",
                                      ptr);
                    break;
                }
                // Check for
                // Should be the first quote of a field name
                if (*ptr != '"')
                {
                    s = nats_setError(NATS_ERR, "missing quote: '%s'", ptr);
                    break;
                }
                ptr += 1;
                s = _jsonGetStr(&ptr, &fieldName);
                if (s != NATS_OK)
                {
                    s = nats_setError(NATS_ERR, "invalid field name: '%s'", ptr);
                    break;
                }
                s = _jsonCreateField(&field, fieldName);
                if (s != NATS_OK)
                {
                    NATS_UPDATE_ERR_STACK(s);
                    break;
                }
                s = natsStrHash_Set(json->fields, fieldName, false, (void*) field, (void**)&oldField);
                if (s != NATS_OK)
                {
                    NATS_UPDATE_ERR_STACK(s);
                    break;
                }
                if (oldField != NULL)
                {
                    NATS_FREE(oldField);
                    oldField = NULL;
                }
                state = JSON_STATE_SEPARATOR;
                break;
            }
            case JSON_STATE_SEPARATOR:
            {
                // Should be the separation between field name and value.
                if (*ptr != ':')
                {
                    s = nats_setError(NATS_ERR, "missing value for field '%s': '%s'", fieldName, ptr);
                    break;
                }
                ptr += 1;
                state = JSON_STATE_VALUE;
                break;
            }
            case JSON_STATE_VALUE:
            {
                // Parsing value here. Determine the type based on first character.
                if (*ptr == '"')
                {
                    field->typ = TYPE_STR;
                    ptr += 1;
                    s = _jsonGetStr(&ptr, &field->value.vstr);
                    if (s != NATS_OK)
                        s = nats_setError(NATS_ERR,
                                          "invalid string value for field '%s': '%s'",
                                          fieldName, ptr);
                }
                else if ((*ptr == 't') || (*ptr == 'f'))
                {
                    field->typ = TYPE_BOOL;
                    s = _jsonGetBool(&ptr, &field->value.vbool);
                    if (s != NATS_OK)
                        s = nats_setError(NATS_ERR,
                                          "invalid boolean value for field '%s': '%s'",
                                          fieldName, ptr);
                }
                else if (((*ptr >= 48) && (*ptr <= 57)) || (*ptr == '-'))
                {
                    field->typ = TYPE_NUM;
                    s = _jsonGetNum(&ptr, &field->value.vdec);
                    if (s != NATS_OK)
                        s = nats_setError(NATS_ERR,
                                          "invalid numeric value for field '%s': '%s'",
                                          fieldName, ptr);
                }
                else if ((*ptr == '[') || (*ptr == '{'))
                {
                    bool doSkip = true;

                    if (*ptr == '[')
                    {
                        ptr += 1;
                        s = _jsonGetArray(&ptr, &field->value.varr);
                        if (s == NATS_OK)
                        {
                            field->typ = TYPE_ARRAY;
                            doSkip = false;
                        }
                        else  if (s == NATS_NOT_PERMITTED)
                        {
                            // This is an array but we don't support the
                            // type of elements, so skip.
                            s = NATS_OK;
                            // Clear error stack
                            nats_clearLastError();
                            // Need to go back to the '[' character.
                            ptr -= 1;
                        }
                    }
                    if ((s == NATS_OK) && doSkip)
                    {
                        // Don't support, skip until next field.
                        ptr = _jsonSkipUnknownType(ptr);
                        // Destroy the field that we have created
                        natsStrHash_Remove(json->fields, fieldName);
                        _jsonFreeField(field);
                        field = NULL;
                    }
                }
                else
                {
                    s = nats_setError(NATS_ERR,
                                      "looking for value, got: '%s'", ptr);
                }
                if (s == NATS_OK)
                    state = JSON_STATE_NEXT_FIELD;
                break;
            }
            case JSON_STATE_NEXT_FIELD:
            {
                // We should have a ',' separator or be at the end of the string
                if ((*ptr != ',') && (*ptr != '}'))
                {
                    s =  nats_setError(NATS_ERR, "missing separator: '%s' (%s)", ptr, jsonStr);
                    break;
                }
                if (*ptr == ',')
                    state = JSON_STATE_FIELD;
                else
                    state = JSON_STATE_END;
                ptr += 1;
                break;
            }
            case JSON_STATE_END:
            {
                // If we are here it means that there was a character after the '}'
                // so that's considered a failure.
                s = nats_setError(NATS_ERR,
                                  "invalid characters after end of JSON: '%s'",
                                  ptr);
                break;
            }
        }
    }
    if (s == NATS_OK)
    {
        if (state != JSON_STATE_END)
            s = nats_setError(NATS_ERR, "%s", "JSON string not properly closed");
    }
    if (s == NATS_OK)
        *newJSON = json;
    else
        nats_JSONDestroy(json);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_JSONGetValue(nats_JSON *json, const char *fieldName, int fieldType, void **addr)
{
    nats_JSONField *field = NULL;

    field = (nats_JSONField*) natsStrHash_Get(json->fields, (char*) fieldName);
    // If unknown field, just ignore
    if (field == NULL)
        return NATS_OK;

    // Check parsed type matches what is being asked.
    if ((((fieldType == TYPE_INT) || (fieldType == TYPE_LONG)) && (field->typ != TYPE_NUM))
        || ((field->typ != TYPE_NUM) && (fieldType != field->typ)))
    {
        return nats_setError(NATS_INVALID_ARG,
                             "Asked for field '%s' as type %d, but got type %d when parsing",
                             field->name, fieldType, field->typ);
    }
    switch (fieldType)
    {
        case TYPE_STR:
        {
            if (field->value.vstr == NULL)
            {
                (*(char**)addr) = NULL;
            }
            else
            {
                char *tmp = NATS_STRDUP(field->value.vstr);
                if (tmp == NULL)
                    return nats_setDefaultError(NATS_NO_MEMORY);
                (*(char**)addr) = tmp;
            }
            break;
        }
        case TYPE_BOOL:     (*(bool*)addr) = field->value.vbool;                break;
        case TYPE_INT:      (*(int*)addr) = (int)field->value.vdec;             break;
        case TYPE_LONG:     (*(int64_t*)addr) = (int64_t) field->value.vdec;    break;
        case TYPE_DOUBLE:   (*(long double*)addr) = field->value.vdec;          break;
        default:
        {
            return nats_setError(NATS_NOT_FOUND,
                                 "Unknown field type for field '%s': %d",
                                 field->name, fieldType);
        }
    }
    return NATS_OK;
}

natsStatus
nats_JSONGetArrayValue(nats_JSON *json, const char *fieldName, int fieldType, void ***array, int *arraySize)
{
    natsStatus      s        = NATS_OK;
    nats_JSONField  *field   = NULL;
    void            **values = NULL;

    field = (nats_JSONField*) natsStrHash_Get(json->fields, (char*) fieldName);
    // If unknown field, just ignore
    if (field == NULL)
        return NATS_OK;

    // Check parsed type matches what is being asked.
    if (field->typ != TYPE_ARRAY)
        return nats_setError(NATS_INVALID_ARG,
                             "Field '%s' is not an array, it has type: %d",
                             field->name, field->typ);
    if (fieldType != field->value.varr->typ)
        return nats_setError(NATS_INVALID_ARG,
                             "Asked for field '%s' as an array of type: %d, but it is an array of type: %d",
                             field->name, fieldType, field->typ);

    values = NATS_CALLOC(field->value.varr->size, field->value.varr->eltSize);
    if (values == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (fieldType == TYPE_STR)
    {
        int i;

        for (i=0; i<field->value.varr->size; i++)
        {
            values[i] = NATS_STRDUP((char*)(field->value.varr->values[i]));
            if (values[i] == NULL)
            {
                s = nats_setDefaultError(NATS_NO_MEMORY);
                break;
            }
        }
        if (s != NATS_OK)
        {
            int j;

            for (j=0; j<i; j++)
                NATS_FREE(values[i]);

            NATS_FREE(values);
        }
    }
    else
    {
        s = nats_setError(NATS_INVALID_ARG, "%s",
                          "Only string arrays are supported");
    }
    if (s == NATS_OK)
    {
        *array     = values;
        *arraySize = field->value.varr->size;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

void
nats_JSONDestroy(nats_JSON *json)
{
    natsStrHashIter iter;
    nats_JSONField  *field;

    if (json == NULL)
        return;

    natsStrHashIter_Init(&iter, json->fields);
    while (natsStrHashIter_Next(&iter, NULL, (void**)&field))
    {
        natsStrHashIter_RemoveCurrent(&iter);
        _jsonFreeField(field);
    }
    natsStrHash_Destroy(json->fields);
    NATS_FREE(json->str);
    NATS_FREE(json);
}
