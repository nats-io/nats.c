// Copyright 2015-2021 The NATS Authors
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

// Do this after including natsp.h in order to have some of the
// GNU specific flag set first.
#include <string.h>

#include "hash.h"
#include "conn.h"
#include "msg.h"

int natsMessageHeader_encodedLen(natsMessage *msg)
{
    natsStrHashIter iter;
    char *key = NULL;
    void *p = NULL;
    int hl = 0;

    // Special case: if needsLift is true, it means that this is a message
    // that was received but never lifted (so for sure no header was added,
    // modified or removed. So return the current len of the encoded headers.
    // if (natsMessage_needsLift(msg))
    //     return msg->hdr.len;

    // Here it could be that a message was created, some headers added but
    // then all were removed before the send. Returning 0 here means that
    // the publish will send PUB instead of HPUB. We could choose to return
    // hdrLinePreLen + _CRLF_LEN_ + _CRLF_LEN_ instead, which means that
    // we would encode "NATS/1.0\r\n\r\n" only.
    if (msg->headers == NULL)
        return 0;

    hl = nats_NATS10.len + _CRLF_LEN_;
    natsStrHashIter_Init(&iter, msg->headers);
    while (natsStrHashIter_Next(&iter, &key, &p))
    {
        natsHeaderValue *v = (natsHeaderValue *)p;
        natsHeaderValue *c;

        for (c = v; c != NULL; c = c->next)
        {
            hl += (int)strlen(key) + 2; // 2 for ": "
            hl += (int)strlen(c->value) + _CRLF_LEN_;
        }
    }
    natsStrHashIter_Done(&iter);
    hl += _CRLF_LEN_;

    return hl;
}

natsStatus
natsMessageHeader_encode(natsBuf *buf, natsMessage *msg)
{
    natsStrHashIter iter;
    natsStatus s = NATS_OK;
    char *key = NULL;
    void *p = NULL;

    // See explanation in natsMessageHeader_encodedLen()
    // if (natsMessage_needsLift(msg))
    // {
    //     s = natsBuf_addString(buf, &msg->hdr);
    //     return NATS_UPDATE_ERR_STACK(s);
    // }
    // Based on decision in natsMessageHeader_encodedLen(),
    // getting here with NULL headers is likely a bug.
    if (msg->headers == NULL)
        return nats_setError(NATS_ERR, "%s", "trying to encode headers while there is none");

    IFOK(s, natsBuf_addString(buf, &nats_NATS10));
    IFOK(s, natsBuf_addString(buf, &nats_CRLF));
    IFOK(s, ALWAYS_OK(natsStrHashIter_Init(&iter, msg->headers)));
    while ((STILL_OK(s)) && natsStrHashIter_Next(&iter, &key, &p))
    {
        natsHeaderValue *v = (natsHeaderValue *)p;
        natsHeaderValue *c;

        for (c = v; (STILL_OK(s)) && (c != NULL); c = c->next)
        {
            IFOK(s, natsBuf_addCString(buf, key));
            IFOK(s, natsBuf_addB(buf, ':'));
            IFOK(s, natsBuf_addB(buf, ' '));
            if (STILL_OK(s))
            {
                int vl = (int)strlen(c->value);
                int pos = natsBuf_len(buf);
                s = natsBuf_addBB(buf, (const uint8_t *)c->value, vl);
                if (STILL_OK(s))
                {
                    uint8_t *ch = natsBuf_data(buf) + pos;
                    int i;
                    for (i = 0; i < vl; i++)
                    {
                        if ((*ch == '\r') || (*ch == '\n'))
                            *ch = ' ';
                        ch++;
                    }
                }
            }
            IFOK(s, natsBuf_addString(buf, &nats_CRLF));
        }
    }
    natsStrHashIter_Done(&iter);
    IFOK(s, natsBuf_addString(buf, &nats_CRLF));
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsHeaderValue_create(natsHeaderValue **retV, const char *value, bool makeCopy)
{
    // natsStatus      s   = NATS_OK;
    // char            *cv = (char*) value;
    // natsHeaderValue *v  = NULL;

    // *retV = NULL;

    // v = NATS_MALLOC(sizeof(natsHeaderValue));
    // if (v == NULL)
    //     return nats_setDefaultError(NATS_NO_MEMORY);

    // if (makeCopy && value != NULL)
    // {
    //     DUP_STRING(s, cv, value);
    //     if (s != NATS_OK)
    //     {
    //         NATS_FREE(v);
    //         return NATS_UPDATE_ERR_STACK(s);
    //     }
    // }
    // v->value    = cv;
    // v->needFree = makeCopy;
    // v->next     = NULL;
    // *retV       = v;

    return NATS_OK;
}

static natsStatus
_checkMsgAndKey(natsMessage *msg, const char *key)
{
    if (msg == NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", "message cannot be NULL");

    if ((key == NULL) || (key[0] == '\0'))
        return nats_setError(NATS_INVALID_ARG, "%s", "key cannot be NULL nor empty");

    return NATS_OK;
}

static char *
_moveToLF(char *end, char *ptr)
{
    while (ptr != end)
    {
        if ((*ptr == '\r') && (*(ptr + 1) == '\n'))
            return ++ptr;
        else
            ptr++;
    }
    return ptr;
}

static natsStatus
_processKeyValue(int line, natsMessage *msg, char *endPtr, char **pPtr, char **lastKey)
{
    natsStatus s = NATS_OK;
    // char            *ptr = *pPtr;
    // char            *col = NULL;
    // char            *key = NULL;
    // char            *val = NULL;
    // natsHeaderValue *v   = NULL;
    // bool            ml   = false;
    // char            *start;
    // char            *endval;

    // start = ptr;
    // if (*ptr == '\r')
    // {
    //     if ((++ptr == endPtr) || ((*ptr == '\n') && (++ptr == endPtr)))
    //     {
    //         *pPtr = ptr;
    //         return NATS_OK;
    //     }
    //     return nats_setError(NATS_PROTOCOL_ERROR, "invalid start of a key: %s", start);
    // }
    // if (isspace((unsigned char) *ptr))
    // {
    //     if (line == 0)
    //         return nats_setError(NATS_PROTOCOL_ERROR, "key cannot start with a space: %s", ptr);

    //     key = *lastKey;
    //     ml = true;
    // }
    // else
    // {
    //     col = strchr((const char*) ptr, (int) ':');
    //     if (col == NULL)
    //         return nats_setError(NATS_PROTOCOL_ERROR, "column delimiter not found: %s", ptr);

    //     // Replace column char with \0 to terminate the key string.
    //     key = ptr;
    //     ptr = col+1;
    //     (*col) = '\0';
    // }

    // while ((ptr != endPtr) && (isspace((unsigned char) *ptr)))
    //     ptr++;

    // if (ptr == endPtr)
    //     return nats_setError(NATS_PROTOCOL_ERROR, "no value found for key %s", key);

    // val = ptr;
    // // Now find the \r\n for this value
    // ptr = _moveToLF(endPtr, ptr);
    // if (ptr == endPtr)
    //     return nats_setError(NATS_PROTOCOL_ERROR, "no CRLF found for value of key %s", key);

    // // Trim right spaces and set to \0 to terminate the value string.
    // endval = ptr;
    // // Backtrack to \r and any space characters. Make sure we don't go
    // // past the beginning of the value pointer.
    // endval--;
    // if (*endval == '\r')
    //     endval--;
    // while ((endval != val) && (isspace((unsigned char) *endval)))
    //     endval--;
    // endval++;
    // *(endval) = '\0';

    // if (ml)
    // {
    //     char *newValue = NULL;

    //     natsHeaderValue *cur = natsStrHash_Get(msg->headers, key);
    //     if (cur == NULL)
    //         return nats_setError(NATS_PROTOCOL_ERROR, "unable to process folding lines for key %s", key);

    //     for (; cur->next != NULL; )
    //         cur = cur->next;

    //     if (nats_asprintf(&newValue, "%s %s", cur->value, val) == -1)
    //         return nats_setDefaultError(NATS_NO_MEMORY);

    //     if (cur->needFree)
    //         NATS_FREE(cur->value);

    //     cur->value    = newValue;
    //     cur->needFree = true;
    // }
    // else
    // {
    //     s = natsHeaderValue_create(&v, (const char*) val, false);
    //     if (STILL_OK(s))
    //     {
    //         natsHeaderValue *cur = natsStrHash_Get(msg->headers, key);
    //         if (cur != NULL)
    //         {
    //             for (; cur->next != NULL; )
    //                 cur = cur->next;

    //             cur->next = v;
    //         }
    //         else
    //             s = natsStrHash_Set(msg->headers, (char*) key, false, (void*) v, NULL);
    //     }
    // }

    // if (STILL_OK(s))
    // {
    //     ptr++;
    //     *pPtr = ptr;
    //     *lastKey = key;
    // }
    return NATS_UPDATE_ERR_STACK(s);
}

// static natsStatus
// _liftHeaders(natsMessage *msg, bool setOrAdd)
// {
//     natsStatus s       = NATS_OK;
//     char       *ptr    = NULL;
//     char       *sts    = NULL;
//     char       *endPtr = NULL;
//     char       *lk     = NULL;
//     int        i;

//     // If there is no header map and needsLift is false, and this is not
//     // an action to set or add a header, then simply return.
//     if (!setOrAdd && (msg->headers == NULL) && !natsMessage_needsLift(msg))
//         return NATS_OK;

//     // For set or add operations, possibly create the headers map.
//     if (msg->headers == NULL)
//     {
//         s = natsStrHash_Create(&(msg->headers), 4);
//         if (s != NATS_OK)
//             return NATS_UPDATE_ERR_STACK(s);
//     }

//     // In all cases, if there is no need to lift, we are done.
//     if (!natsMessage_needsLift(msg))
//         return NATS_OK;

//     // If hdrLen is less than what we need for NATS/1.0\r\n, then
//     // clearly this is a bad header.
//     if ((msg->hdrLen < HDR_LINE_LEN) || (strstr(msg->hdr, HDR_LINE_PRE) != msg->hdr))
//         return nats_setError(NATS_PROTOCOL_ERROR, "header prefix missing: %s", msg->hdr);

//     endPtr = (char*) (msg->hdr + msg->hdrLen);

//     sts = (char*) (msg->hdr + HDR_LINE_PRE_LEN);
//     while ((sts != endPtr) && (*sts == ' '))
//         sts++;

//     ptr = sts;
//     ptr = _moveToLF(endPtr, ptr);
//     if (ptr != endPtr)
//     {
//         char *stsEnd = ptr;

//         ptr++;
//         while ((stsEnd != sts) && (*stsEnd != '\r'))
//             stsEnd--;

//         // Terminate the status.
//         *stsEnd = '\0';
//     }

//     if (ptr == endPtr)
//         return nats_setError(NATS_PROTOCOL_ERROR, "early termination of headers: %s", msg->hdr);

//     for (i=0; ((STILL_OK(s)) && (ptr != endPtr)); i++)
//         s = _processKeyValue(i, msg, endPtr, &ptr, &lk);

//     if (STILL_OK(s))
//     {
//         // At this point we have had no protocol error lifting the header
//         // so we clear this flag so that we don't attempt to lift again.
//         natsMessage_clearNeedsLift(msg);

//         // Furthermore, we need the flag to be cleared should we need to
//         // add the no responders header (otherwise we would recursively
//         // try to lift headers).
//         // If adding the field fails, it is likely due to memory issue,
//         // so it is fine to keep "needsLift" as false.

//         // Check if we have an inlined status.
//         if ((sts != NULL) && (*sts != '\0'))
//         {
//             // There could be a description...
//             if (strlen(sts) > HDR_STATUS_LEN)
//             {
//                 char *desc = (char*) (sts + HDR_STATUS_LEN);
//                 char descb = 0;

//                 // Save byte that starts the description
//                 descb = *desc;
//                 // Replace with '\0' to end the status.
//                 *desc = '\0';

//                 // Set status value (this will make a copy)
//                 s = natsMessageHeader_Set(msg, STATUS_HDR, (const char*) sts);
//                 if (STILL_OK(s))
//                 {
//                     char *desce = NULL;

//                     // Restore character of starting description
//                     *desc = descb;
//                     // Trim left spaces
//                     while ((*desc != '\0') && isspace((unsigned char) *desc))
//                         desc++;

//                     // If we are not at the end of description
//                     if (*desc != '\0')
//                     {
//                         // Go to end of description and walk back to trim right.
//                         desce = (char*) (desc + (int) strlen(desc) - 1);
//                         while ((desce != desc) && isspace((unsigned char) *desce))
//                         {
//                             *desce = '\0';
//                             desce--;
//                         }
//                     }
//                     // If there is a description, set the value (this will make a copy)
//                     if (*desc != '\0')
//                         s = natsMessageHeader_Set(msg, DESCRIPTION_HDR, (const char*) desc);
//                 }
//             }
//             else
//                 s = natsMessageHeader_Set(msg, STATUS_HDR, (const char*) sts);
//         }
//     }

//     return NATS_UPDATE_ERR_STACK(s);
// }

natsStatus
natsMessageHeader_Set(natsMessage *msg, const char *key, const char *value)
{
    natsStatus s = NATS_OK;

    // if ((s = _checkMsgAndKey(msg, key)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((s = _liftHeaders(msg, true)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if (STILL_OK(s))
    // {
    //     natsHeaderValue *v = NULL;

    //     s = natsHeaderValue_create(&v, value, true);
    //     if (STILL_OK(s))
    //     {
    //         void *p = NULL;

    //         s = natsStrHash_Set(msg->headers, (char*) key, true, (void*) v, &p);
    //         if (s != NATS_OK)
    //             natsHeaderValue_free(v, false);
    //         else if (p != NULL)
    //         {
    //             natsHeaderValue *old = (natsHeaderValue*) p;
    //             natsHeaderValue_free(old, true);
    //         }
    //     }
    // }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMessageHeader_Add(natsMessage *msg, const char *key, const char *value)
{
    natsStatus s = NATS_OK;

    // if ((s = _checkMsgAndKey(msg, key)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((s = _liftHeaders(msg, true)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if (STILL_OK(s))
    // {
    //     natsHeaderValue *v = NULL;

    //     s = natsHeaderValue_create(&v, value, true);
    //     if (STILL_OK(s))
    //     {
    //         natsHeaderValue *cur = natsStrHash_Get(msg->headers, (char*) key);
    //         if (cur != NULL)
    //         {
    //             for (; cur->next != NULL; )
    //                 cur = cur->next;

    //             cur->next = v;
    //         }
    //         else
    //             s = natsStrHash_Set(msg->headers, (char*) key, true, (void*) v, NULL);
    //     }
    // }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMessageHeader_Get(natsMessage *msg, const char *key, const char **value)
{
    natsStatus s = NATS_OK;
    // natsHeaderValue *v  = NULL;

    // if ((s = _checkMsgAndKey(msg, key)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if (value == NULL)
    //     return nats_setError(NATS_INVALID_ARG, "%s", "value cannot be NULL");

    // *value = NULL;

    // if ((s = _liftHeaders(msg, false)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((msg->headers == NULL) || natsStrHash_Count(msg->headers) == 0)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // v = natsStrHash_Get(msg->headers, (char*) key);
    // if (v == NULL)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // *value = (const char*) v->value;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMessageHeader_Values(natsMessage *msg, const char *key, const char ***values, int *count)
{
    natsStatus s = NATS_OK;
    // int             c       = 0;
    // natsHeaderValue *cur    = NULL;
    // const char*     *strs   = NULL;
    // natsHeaderValue *v      = NULL;

    // if ((s = _checkMsgAndKey(msg, key)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((values == NULL) || (count == NULL))
    //     return nats_setError(NATS_INVALID_ARG, "%s", "value or count cannot be NULL");

    // *values = NULL;
    // *count  = 0;

    // if ((s = _liftHeaders(msg, false)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((msg->headers == NULL) || natsStrHash_Count(msg->headers) == 0)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // v = natsStrHash_Get(msg->headers, (char*) key);
    // if (v == NULL)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // for (cur=v; cur != NULL; cur = cur->next)
    //     c++;

    // strs = NATS_CALLOC(c, sizeof(char*));
    // if (strs == NULL)
    //     s = nats_setDefaultError(NATS_NO_MEMORY);
    // else
    // {
    //     int i = 0;

    //     for (cur=v; cur != NULL; cur = cur->next)
    //         strs[i++] = (const char*) cur->value;

    //     *values = strs;
    //     *count  = c;
    // }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMessageHeader_Keys(natsMessage *msg, const char ***keys, int *count)
{
    natsStatus s = NATS_OK;
    // const char* *strs = NULL;
    // int         c     = 0;

    // if (msg == NULL)
    //     return nats_setError(NATS_INVALID_ARG, "%s", "message cannot be NULL");

    // if ((keys == NULL) || (count == NULL))
    //     return nats_setError(NATS_INVALID_ARG, "%s", "keys or count cannot be NULL");

    // *keys  = NULL;
    // *count = 0;

    // if ((s = _liftHeaders(msg, false)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((msg->headers == NULL) || (c = natsStrHash_Count(msg->headers)) == 0)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // strs = NATS_CALLOC(c, sizeof(char*));
    // if (strs == NULL)
    //     s = nats_setDefaultError(NATS_NO_MEMORY);
    // else
    // {
    //     natsStrHashIter iter;
    //     char            *hk = NULL;
    //     int             i;

    //     natsStrHashIter_Init(&iter, msg->headers);
    //     for (i=0; natsStrHashIter_Next(&iter, &hk, NULL); i++)
    //     {
    //         strs[i] = (const char*) hk;
    //     }
    //     natsStrHashIter_Done(&iter);

    //     *keys  = strs;
    //     *count = c;
    // }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMessageHeader_Delete(natsMessage *msg, const char *key)
{
    natsStatus s = NATS_OK;
    // natsHeaderValue *v = NULL;

    // if ((s = _checkMsgAndKey(msg, key)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((s = _liftHeaders(msg, false)) != NATS_OK)
    //     return NATS_UPDATE_ERR_STACK(s);

    // if ((msg->headers == NULL) || natsStrHash_Count(msg->headers) == 0)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // v = natsStrHash_Remove(msg->headers, (char*) key);
    // if (v == NULL)
    //     return NATS_NOT_FOUND; // normal error, so don't update error stack

    // natsHeaderValue_free(v, true);

    return s;
}

const char *
natsMessage_GetSubject(const natsMessage *msg)
{
    // if (msg == NULL)
    return NULL;

    // return (const char*) msg->subject;
}

const char *
natsMessage_GetReply(const natsMessage *msg)
{
    // if (msg == NULL)
    return NULL;

    // return (const char*) msg->reply;
}

const char *
natsMessage_GetData(const natsMessage *msg)
{
    // if (msg == NULL)
    return NULL;

    // return (const char*) msg->data;
}

int natsMessage_GetDataLength(const natsMessage *msg)
{
    // if (msg == NULL)
    return 0;

    // return msg->data.len;
}

// uint64_t
// natsMessage_GetSequence(natsMessage *msg)
// {
//     if (msg == NULL)
//         return 0;

//     return msg->seq;
// }

int64_t
natsMessage_GetTime(natsMessage *msg)
{
    if (msg == NULL)
        return 0;

    return msg->time;
}

natsStatus
nats_CreateMessage(natsMessage **newm, natsConnection *nc, const char *subj)
{
    size_t subjLen = nats_strlen(subj);
    if (!nats_isSubjectValid((const uint8_t *)subj, subjLen, false))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsStatus s = NATS_OK;
    natsMessage *m = NULL;
    natsPool *pool = NULL;

    s = nats_createPool(&pool, &nc->opts->mem, "msg");
    IFOK(s, CHECK_NO_MEMORY(m = nats_palloc(pool, sizeof(natsMessage))));
    IFOK(s, CHECK_NO_MEMORY(m->subject = nats_pstrdupU(pool, subj)));
    if (NOT_OK(s))
    {
        nats_releasePool(pool);
        return NATS_UPDATE_ERR_STACK(s);
    }
    m->pool = pool;
    *newm = m;
    return NATS_OK;
}

bool natsMessage_IsNoResponders(natsMessage *m)
{
    const char *val = NULL;

    // To be a "no responders" message, it has to be of 0 length,
    // and have a "Status" header with "503" as a value.
    return ((m != NULL) && (natsMessage_GetDataLength(m) == 0) && (natsMessageHeader_Get(m, STATUS_HDR, &val) == NATS_OK) && (val != NULL) && (strncmp(val, NO_RESP_STATUS, HDR_STATUS_LEN) == 0));
}

natsStatus nats_SetOnMessageCleanup(natsMessage *m, void (*f)(void *), void *closure)
{
    if (m == NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", "message cannot be NULL");

    m->freef = f;
    m->freeClosure = closure;

    return NATS_OK;
}

natsStatus nats_SetOnMessagePublished(natsMessage *m, natsOnMessagePublishedF f, void *closure)
{
    if (m == NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", "message cannot be NULL");

    m->donef = f;
    m->doneClosure = closure;

    return NATS_OK;
}

natsStatus nats_SetMessagePayload(natsMessage *m, const void *data, size_t dataLen)
{
    if ((m == NULL) || (dataLen == 0) || (data == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);
    if ((m->out != NULL) || (m->pool == NULL))
        return nats_setError(NATS_ILLEGAL_STATE, "%s", "message payload already set");

    m->out = nats_palloc(m->pool, sizeof(natsString));
    if (m->out == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);
    m->out->data = (uint8_t*) data;
    m->out->len = dataLen;

    return NATS_OK;
}

void nats_ReleaseMessage(natsMessage *m)
{
    if (m == NULL)
        return;

    nats_releasePool(m->pool);
}

const uint8_t *nats_GetMessageData(natsMessage *msg)
{
    if ((msg == NULL) || (msg->out == NULL))
        return NULL;

    return (const uint8_t *)msg->out->data;
}

size_t nats_GetMessageDataLen(natsMessage *msg)
{
    if ((msg == NULL) || (msg->out == NULL))
        return 0;

    return msg->out->len;
}
