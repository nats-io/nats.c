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

#ifndef MEM_STRING_H_
#define MEM_STRING_H_

#include <stddef.h>

#define NATS_STR(_str) \
    {                  \
        .len = sizeof(_str) - 1, .data = (uint8_t *)(_str)}
#define NATS_STRC(_str) \
    {                   \
        .len = strlen(_str), .data = (uint8_t *)(_str)}
#define NATS_EMPTY_STR \
    {                  \
        0, NULL}

static inline bool natsString_Equal(natsString *str1, natsString *str2)
{
    if (str1 == str2)
        return true;
    return (str1 != NULL) && (str2 != NULL) &&
           (str1->len == str2->len) &&
           (strncmp((const char *)str1->data, (const char *)str2->data, str1->len) == 0);
}

static inline bool natsString_equalC(const natsString *str1, const char *lit)
{
    if ((str1 == NULL) && (lit == NULL))
        return true;
    return (str1 != NULL) && (lit != NULL) &&
           (str1->len == strlen((const char *)lit)) &&
           (strncmp((const char *)str1->data, lit, str1->len) == 0);
}


static inline bool nats_isCStringEmpty(const char *p) { return (p == NULL) || (*p == '\0'); }

#define nats_toLower(c) (uint8_t)((c >= 'A' && c <= 'Z') ? (c | 0x20) : c)
#define nats_toUpper(c) (uint8_t)((c >= 'a' && c <= 'z') ? (c & ~0x20) : c)

static inline size_t nats_strlen(const char *s) { return nats_isCStringEmpty(s) ? 0 : strlen(s); }
static inline uint8_t *nats_strchr(const uint8_t *s, uint8_t find) { return (uint8_t *)strchr((const char *)s, (int)(find)); }
static inline uint8_t *nats_strrchr(const uint8_t *s, uint8_t find) { return (uint8_t *)strrchr((const char *)s, (int)(find)); }
static inline const uint8_t *nats_strstr(const uint8_t *s, const char *find) { return (const uint8_t *)strstr((const char *)s, find); }
static inline int nats_strcmp(const uint8_t *s1, const char *s2) { return strcmp((const char *)s1, s2); }

static inline int nats_strarray_find(const char **array, int count, const char *str)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(array[i], str) == 0)
            return i;
    }
    return -1;
}

static inline size_t nats_strarray_remove(char **array, int count, const char *str)
{
    int i = nats_strarray_find((const char **)array, count, str);
    if (i < 0)
        return count;

    for (int j = i + 1; j < count; j++)
        array[j - 1] = array[j];

    return count - 1;
}

#endif /* MEM_STRING_H_ */
