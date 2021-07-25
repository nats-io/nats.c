// Copyright 2021 The NATS Authors
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

#include <ctype.h>

#include "js.h"
#include "mem.h"
#include "conn.h"
#include "util.h"
#include "opts.h"

#ifdef DEV_MODE
// For type safety

void js_lock(jsCtx *js)   { natsMutex_Lock(js->mu);   }
void js_unlock(jsCtx *js) { natsMutex_Unlock(js->mu); }

static void _retain(jsCtx *js)  { js->refs++; }
static void _release(jsCtx *js) { js->refs--; }

#else

#define _retain(js)         ((js)->refs++)
#define _release(js)        ((js)->refs--)

#endif // DEV_MODE


const char*      jsDefaultAPIPrefix      = "$JS.API";
const int64_t    jsDefaultRequestWait    = 5000;
const int64_t    jsDefaultStallWait      = 200;
const char       *jsDigits               = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const int        jsBase                  = 62;

#define jsReplyTokenSize    (8)
#define jsReplyPrefixLen    (NATS_INBOX_PRE_LEN + (jsReplyTokenSize) + 1)
#define jsDefaultMaxMsgs    (512 * 1024)

#define jsAckPrefix             "$JS.ACK."
#define jsAckPrefixLen          (8)
#define jsLastConsumerSeqHdr    "Nats-Last-Consumer"

static void
_destroyOptions(jsOptions *o)
{
    NATS_FREE((char*) o->Prefix);
    NATS_FREE((char*) o->Stream.Purge.Subject);
}

static void
_freeContext(jsCtx *js)
{
    natsConnection *nc = NULL;

    natsStrHash_Destroy(js->pm);
    natsSubscription_Destroy(js->rsub);
    _destroyOptions(&(js->opts));
    NATS_FREE(js->rpre);
    natsCondition_Destroy(js->cond);
    natsMutex_Destroy(js->mu);
    nc = js->nc;
    NATS_FREE(js);

    natsConn_release(nc);
}

void
js_retain(jsCtx *js)
{
    js_lock(js);
    js->refs++;
    js_unlock(js);
}

void
js_release(jsCtx *js)
{
    bool doFree;

    js_lock(js);
    doFree = (--(js->refs) == 0);
    js_unlock(js);

    if (doFree)
        _freeContext(js);
}

static void
js_unlockAndRelease(jsCtx *js)
{
    bool doFree;

    doFree = (--(js->refs) == 0);
    js_unlock(js);

    if (doFree)
        _freeContext(js);
}

void
jsCtx_Destroy(jsCtx *js)
{
    if (js == NULL)
        return;

    js_lock(js);
    if (js->rsub != NULL)
    {
        natsSubscription_Destroy(js->rsub);
        js->rsub = NULL;
    }
    if ((js->pm != NULL) && natsStrHash_Count(js->pm) > 0)
    {
        natsStrHashIter iter;
        void            *v = NULL;

        natsStrHashIter_Init(&iter, js->pm);
        while (natsStrHashIter_Next(&iter, NULL, &v))
        {
            natsMsg *msg = (natsMsg*) v;
            natsStrHashIter_RemoveCurrent(&iter);
            natsMsg_Destroy(msg);
        }
    }
    js_unlockAndRelease(js);
}

natsStatus
jsOptions_Init(jsOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(jsOptions));
    return NATS_OK;
}

// Parse the JSON represented by the NATS message's payload and returns the JSON object.
// Unmarshal the API response.
natsStatus
js_unmarshalResponse(jsApiResponse *ar, nats_JSON **new_json, natsMsg *resp)
{
    nats_JSON   *json = NULL;
    nats_JSON   *err  = NULL;
    natsStatus  s;

    memset(ar, 0, sizeof(jsApiResponse));

    s = nats_JSONParse(&json, natsMsg_GetData(resp), natsMsg_GetDataLength(resp));
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // Check if there is an "error" field.
    s = nats_JSONGetObject(json, "error", &err);
    if ((s == NATS_OK) && (err != NULL))
    {
        s = nats_JSONGetInt(err, "code", &(ar->Error.Code));
        IFOK(s, nats_JSONGetUInt16(err, "err_code", &(ar->Error.ErrCode)));
        IFOK(s, nats_JSONGetStr(err, "description", &(ar->Error.Description)));
    }

    if (s == NATS_OK)
        *new_json = json;
    else
        nats_JSONDestroy(json);

    return NATS_UPDATE_ERR_STACK(s);
}

void
js_freeApiRespContent(jsApiResponse *ar)
{
    if (ar == NULL)
        return;

    NATS_FREE(ar->Type);
    NATS_FREE(ar->Error.Description);
}

static natsStatus
_copyPurgeOptions(jsCtx *js, struct jsOptionsStreamPurge *o)
{
    natsStatus                      s   = NATS_OK;
    struct jsOptionsStreamPurge *po = &(js->opts.Stream.Purge);

    po->Sequence = o->Sequence;
    po->Keep     = o->Keep;

    if (!nats_IsStringEmpty(o->Subject))
    {
        po->Subject = NATS_STRDUP(o->Subject);
        if (po->Subject == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_copyStreamInfoOptions(jsCtx *js, struct jsOptionsStreamInfo *o)
{
    js->opts.Stream.Info.DeletedDetails = o->DeletedDetails;
    return NATS_OK;
}

natsStatus
natsConnection_JetStream(jsCtx **new_js, natsConnection *nc, jsOptions *opts)
{
    jsCtx       *js = NULL;
    natsStatus  s;

    if ((new_js == NULL) || (nc == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (opts != NULL)
    {
        if (opts->Wait < 0)
            return nats_setError(NATS_INVALID_ARG, "option 'Wait' (%" PRId64 ") cannot be negative", opts->Wait);
        if (opts->PublishAsync.StallWait < 0)
            return nats_setError(NATS_INVALID_ARG, "option 'PublishAsyncStallWait' (%" PRId64 ") cannot be negative", opts->PublishAsync.StallWait);
    }

    js = (jsCtx*) NATS_CALLOC(1, sizeof(jsCtx));
    if (js == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    js->refs = 1;
    // Retain the NATS connection and keep track of it so that if we
    // detroy the context, in case of failure to fully initialize,
    // we properly release the NATS connection.
    natsConn_retain(nc);
    js->nc = nc;

    s = natsMutex_Create(&(js->mu));
    if (s == NATS_OK)
    {
        // If Domain is set, use domain to create prefix.
        if ((opts != NULL) && !nats_IsStringEmpty(opts->Domain))
        {
            if (nats_asprintf((char**) &(js->opts.Prefix), "$JS.%.*s.API",
                js_lenWithoutTrailingDot(opts->Domain), opts->Domain) < 0)
            {
                s = nats_setDefaultError(NATS_NO_MEMORY);
            }
        }
        else if ((opts == NULL) || nats_IsStringEmpty(opts->Prefix))
        {
            js->opts.Prefix = NATS_STRDUP(jsDefaultAPIPrefix);
            if (js->opts.Prefix == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else if (nats_asprintf((char**) &(js->opts.Prefix), "%.*s",
                js_lenWithoutTrailingDot(opts->Prefix), opts->Prefix) < 0)
        {
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
    }
    if ((s == NATS_OK) && (opts != NULL))
    {
        struct jsOptionsPublishAsync *pa = &(js->opts.PublishAsync);

        pa->MaxPending          = opts->PublishAsync.MaxPending;
        pa->ErrHandler          = opts->PublishAsync.ErrHandler;
        pa->ErrHandlerClosure   = opts->PublishAsync.ErrHandlerClosure;
        pa->StallWait           = opts->PublishAsync.StallWait;
        js->opts.Wait           = opts->Wait;
    }
    if (js->opts.Wait == 0)
        js->opts.Wait = jsDefaultRequestWait;
    if (js->opts.PublishAsync.StallWait == 0)
        js->opts.PublishAsync.StallWait = jsDefaultStallWait;
    if ((s == NATS_OK) && (opts != NULL))
    {
        s = _copyPurgeOptions(js, &(opts->Stream.Purge));
        IFOK(s, _copyStreamInfoOptions(js, &(opts->Stream.Info)));
    }

    if (s == NATS_OK)
        *new_js = js;
    else
        jsCtx_Destroy(js);

    return NATS_UPDATE_ERR_STACK(s);
}

int
js_lenWithoutTrailingDot(const char *str)
{
    int l = (int) strlen(str);

    if (str[l-1] == '.')
        l--;
    return l;
}

natsStatus
js_setOpts(natsConnection **nc, bool *freePfx, jsCtx *js, jsOptions *opts, jsOptions *resOpts)
{
    natsStatus s = NATS_OK;

    *freePfx = false;
    jsOptions_Init(resOpts);

    if ((opts != NULL) && !nats_IsStringEmpty(opts->Domain))
    {
        char *pfx = NULL;
        if (nats_asprintf(&pfx, "$JS.%.*s.API",
                js_lenWithoutTrailingDot(opts->Domain), opts->Domain) < 0)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            resOpts->Prefix = pfx;
            *freePfx        = true;
        }
    }
    if (s == NATS_OK)
    {
        struct jsOptionsStreamPurge *po = &(js->opts.Stream.Purge);

        js_lock(js);
        // If not set above...
        if (resOpts->Prefix == NULL)
            resOpts->Prefix = (opts == NULL || nats_IsStringEmpty(opts->Prefix)) ? js->opts.Prefix : opts->Prefix;

        // Take provided one or default to context's.
        resOpts->Wait = (opts == NULL || opts->Wait <= 0) ? js->opts.Wait : opts->Wait;

        // Purge options
        if (opts != NULL)
        {
            struct jsOptionsStreamPurge *opo = &(opts->Stream.Purge);

            // If any field is set, use `opts`, otherwise, we will use the
            // context's purge options.
            if ((opo->Subject != NULL) || (opo->Sequence > 0) || (opo->Keep > 0))
                po = opo;
        }
        memcpy(&(resOpts->Stream.Purge), po, sizeof(*po));

        // Stream info options
        resOpts->Stream.Info.DeletedDetails = (opts == NULL ? js->opts.Stream.Info.DeletedDetails : opts->Stream.Info.DeletedDetails);

        *nc = js->nc;
        js_unlock(js);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsPubOptions_Init(jsPubOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(jsPubOptions));
    return NATS_OK;
}

natsStatus
js_Publish(jsPubAck **new_puback, jsCtx *js, const char *subj, const void *data, int dataLen,
           jsPubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;
    natsMsg    msg;

    natsMsg_init(&msg, subj, (const char*) data, dataLen);
    s = js_PublishMsg(new_puback, js, &msg, opts, errCode);
    natsMsg_freeHeaders(&msg);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_setHeadersFromOptions(natsMsg *msg, jsPubOptions *opts)
{
    natsStatus  s        = NATS_OK;
    char        temp[64] = {'\0'};

    if (!nats_IsStringEmpty(opts->MsgId))
        s = natsMsgHeader_Set(msg, jsMsgIdHdr, opts->MsgId);

    if ((s == NATS_OK) && !nats_IsStringEmpty(opts->ExpectLastMsgId))
        s = natsMsgHeader_Set(msg, jsExpectedLastMsgIdHdr, opts->ExpectLastMsgId);

    if ((s == NATS_OK) && !nats_IsStringEmpty(opts->ExpectStream))
        s = natsMsgHeader_Set(msg, jsExpectedStreamHdr, opts->ExpectStream);

    if ((s == NATS_OK) && (opts->ExpectLastSeq > 0))
    {
        snprintf(temp, sizeof(temp), "%" PRIu64, opts->ExpectLastSeq);
        s = natsMsgHeader_Set(msg, jsExpectedLastSeqHdr, temp);
    }

    if ((s == NATS_OK) && (opts->ExpectLastSubjectSeq > 0))
    {
        snprintf(temp, sizeof(temp), "%" PRIu64, opts->ExpectLastSubjectSeq);
        s = natsMsgHeader_Set(msg, jsExpectedLastSubjSeqHdr, temp);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_checkMaxWaitOpt(int64_t *new_ttl, jsPubOptions *opts)
{
    int64_t ttl;

    if ((ttl = opts->MaxWait) < 0)
        return nats_setError(NATS_INVALID_ARG, "option 'MaxWait' (%" PRId64 ") cannot be negative", ttl);

    *new_ttl = ttl;
    return NATS_OK;
}

natsStatus
js_PublishMsg(jsPubAck **new_puback,jsCtx *js, natsMsg *msg,
              jsPubOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    int64_t             ttl     = 0;
    nats_JSON           *json   = NULL;
    natsMsg             *resp   = NULL;
    jsApiResponse   ar;

    if (errCode != NULL)
        *errCode = 0;

    if ((js == NULL) || (msg == NULL) || nats_IsStringEmpty(msg->subject))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (opts != NULL)
    {
        s = _checkMaxWaitOpt(&ttl, opts);
        IFOK(s, _setHeadersFromOptions(msg, opts));
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    // As it would be for a NATS connection, if the context has been destroyed,
    // the memory is invalid and accessing any field of the context could cause
    // a SEGFAULT. But assuming the context is still valid, we can access its
    // options and the NATS connection without locking since they are immutable
    // and the NATS connection has been retained when getting the JS context.

    // If not set through options, default to the context's Wait value.
    if (ttl == 0)
        ttl = js->opts.Wait;

    IFOK_JSR(s, natsConnection_RequestMsg(&resp, js->nc, msg, ttl));
    if (s == NATS_OK)
        s = js_unmarshalResponse(&ar, &json, resp);
    if (s == NATS_OK)
    {
        if (js_apiResponseIsErr(&ar))
        {
             if (errCode != NULL)
                *errCode = (int) ar.Error.ErrCode;
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
        }
        else if (new_puback != NULL)
        {
            // The user wants the jsPubAck object back, so we need to unmarshal it.
            jsPubAck *pa = NULL;

            pa = (jsPubAck*) NATS_CALLOC(1, sizeof(jsPubAck));
            if (pa == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                s = nats_JSONGetStr(json, "stream", &(pa->Stream));
                IFOK(s, nats_JSONGetULong(json, "seq", &(pa->Sequence)));
                IFOK(s, nats_JSONGetBool(json, "duplicate", &(pa->Duplicate)));

                if (s == NATS_OK)
                    *new_puback = pa;
                else
                    jsPubAck_Destroy(pa);
            }
        }
        js_freeApiRespContent(&ar);
        nats_JSONDestroy(json);
    }
    natsMsg_Destroy(resp);
    return NATS_UPDATE_ERR_STACK(s);
}

void
jsPubAck_Destroy(jsPubAck *pa)
{
    if (pa == NULL)
        return;

    NATS_FREE(pa->Stream);
    NATS_FREE(pa);
}

static void
_handleAsyncReply(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    const char      *subject    = natsMsg_GetSubject(msg);
    char            *id         = NULL;
    jsCtx           *js         = NULL;
    natsMsg         *pmsg       = NULL;
    char            errTxt[256] = {'\0'};
    jsPubAckErr     pae;
    struct jsOptionsPublishAsync *opa = NULL;

    if ((subject == NULL) || (int) strlen(subject) <= jsReplyPrefixLen)
    {
        natsMsg_Destroy(msg);
        return;
    }

    id = (char*) (subject+jsReplyPrefixLen);
    js = (jsCtx*) closure;

    js_lock(js);

    pmsg = natsStrHash_Remove(js->pm, id);
    if (pmsg == NULL)
    {
        natsMsg_Destroy(msg);
        js_unlock(js);
        return;
    }

    opa = &(js->opts.PublishAsync);
    if (opa->ErrHandler != NULL)
    {
        natsStatus s = NATS_OK;

        memset(&pae, 0, sizeof(jsPubAckErr));

        // Check for no responders
        if (natsMsg_IsNoResponders(msg))
        {
            s = NATS_NO_RESPONDERS;
        }
        else
        {
            nats_JSON           *json = NULL;
            jsApiResponse       ar;

            // Now unmarshal the API response and check if there was an error.

            s = js_unmarshalResponse(&ar, &json, msg);
            if ((s == NATS_OK) && js_apiResponseIsErr(&ar))
            {
                pae.Err     = NATS_ERR;
                pae.ErrCode = (int) ar.Error.ErrCode;
                snprintf(errTxt, sizeof(errTxt), "%s", ar.Error.Description);
            }
            js_freeApiRespContent(&ar);
            nats_JSONDestroy(json);
        }
        if (s != NATS_OK)
        {
            pae.Err = s;
            snprintf(errTxt, sizeof(errTxt), "%s", natsStatus_GetText(pae.Err));
        }

        // We will invoke CB only if there is any kind of error.
        if (pae.Err != NATS_OK)
        {
            // Associate the message with the pubAckErr object.
            pae.Msg = pmsg;
            // And the error text.
            pae.ErrText = errTxt;
            js_unlock(js);

            (opa->ErrHandler)(js, &pae, opa->ErrHandlerClosure);

            js_lock(js);

            // If the user resent the message, pae->Msg will have been cleared.
            // In this case, do not destroy the message. Do not blindly destroy
            // an address that could have been set, so destroy only if pmsg
            // is same value than pae->Msg.
            if (pae.Msg != pmsg)
                pmsg = NULL;
        }
    }

    // Now that the callback has returned, decrement the number of pending messages.
    js->pmcount--;

    // If there are callers waiting for async pub completion, or stalled async
    // publish calls and we are now below max pending, broadcast to unblock them.
    if (((js->pacw > 0) && (js->pmcount == 0))
        || ((js->stalled > 0) && (js->pmcount <= opa->MaxPending)))
    {
        natsCondition_Broadcast(js->cond);
    }
    js_unlock(js);

    natsMsg_Destroy(pmsg);
    natsMsg_Destroy(msg);
}

static void
_subComplete(void *closure)
{
    js_release((jsCtx*) closure);
}

static natsStatus
_newAsyncReply(char *reply, jsCtx *js)
{
    natsStatus  s           = NATS_OK;

    // Create the internal objects if it is the first time that we are doing
    // an async publish.
    if (js->rsub == NULL)
    {
        s = natsCondition_Create(&(js->cond));
        IFOK(s, natsStrHash_Create(&(js->pm), 64));
        if (s == NATS_OK)
        {
            js->rpre = NATS_MALLOC(jsReplyPrefixLen+1);
            if (js->rpre == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                char tmp[NATS_INBOX_PRE_LEN+NUID_BUFFER_LEN+1];

                natsInbox_init(tmp, sizeof(tmp));
                memcpy(js->rpre, tmp, NATS_INBOX_PRE_LEN);
                memcpy(js->rpre+NATS_INBOX_PRE_LEN, tmp+((int)strlen(tmp)-jsReplyTokenSize), jsReplyTokenSize);
                js->rpre[jsReplyPrefixLen-1] = '.';
                js->rpre[jsReplyPrefixLen]   = '\0';
            }
        }
        if (s == NATS_OK)
        {
            char subj[jsReplyPrefixLen + 2];

            snprintf(subj, sizeof(subj), "%s*", js->rpre);
            s = natsConn_subscribeNoPool(&(js->rsub), js->nc, subj, _handleAsyncReply, (void*) js);
            if (s == NATS_OK)
            {
                _retain(js);
                natsSubscription_SetPendingLimits(js->rsub, -1, -1);
                natsSubscription_SetOnCompleteCB(js->rsub, _subComplete, (void*) js);
            }
        }
        if (s != NATS_OK)
        {
            // Undo the things we created so we retry again next time.
            // It is either that or we have to always check individual
            // objects to know if we have to create them.
            NATS_FREE(js->rpre);
            js->rpre = NULL;
            natsStrHash_Destroy(js->pm);
            js->pm = NULL;
            natsCondition_Destroy(js->cond);
            js->cond = NULL;
        }
    }
    if (s == NATS_OK)
    {
        int64_t l;
        int     i;

        memcpy(reply, js->rpre, jsReplyPrefixLen);
        l = nats_Rand64();
        for (i=0; i < jsReplyTokenSize; i++)
        {
            reply[jsReplyPrefixLen+i] = jsDigits[l%jsBase];
            l /= jsBase;
        }
        reply[jsReplyPrefixLen+jsReplyTokenSize] = '\0';
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_registerPubMsg(natsConnection **nc, char *reply, jsCtx *js, natsMsg *msg)
{
    natsStatus  s       = NATS_OK;
    char        *id     = NULL;
    bool        release = false;
    int64_t     maxp    = 0;

    js_lock(js);

    maxp = js->opts.PublishAsync.MaxPending;

    js->pmcount++;
    s = _newAsyncReply(reply, js);
    if (s == NATS_OK)
        id = reply+jsReplyPrefixLen;
    if ((s == NATS_OK)
            && (maxp > 0)
            && (js->pmcount > maxp))
    {
        int64_t target = nats_setTargetTime(js->opts.PublishAsync.StallWait);

        _retain(js);

        js->stalled++;
        while ((s != NATS_TIMEOUT) && (js->pmcount > maxp))
            s = natsCondition_AbsoluteTimedWait(js->cond, js->mu, target);
        js->stalled--;

        if (s == NATS_TIMEOUT)
            s = nats_setError(s, "%s", "stalled with too many outstanding async published messages");

        release = true;
    }
    if (s == NATS_OK)
        s = natsStrHash_Set(js->pm, id, true, msg, NULL);
    if (s == NATS_OK)
        *nc = js->nc;
    else
        js->pmcount--;
    if (release)
        js_unlockAndRelease(js);
    else
        js_unlock(js);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PublishAsync(jsCtx *js, const char *subj, const void *data, int dataLen,
                jsPubOptions *opts)
{
    natsStatus s;
    natsMsg    *msg = NULL;

    s = natsMsg_Create(&msg, subj, NULL, (const char*) data, dataLen);
    IFOK(s, js_PublishMsgAsync(js, &msg, opts));

    // The `msg` pointer will have been set to NULL if the library took ownership.
    natsMsg_Destroy(msg);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PublishMsgAsync(jsCtx *js, natsMsg **msg, jsPubOptions *opts)
{
    natsStatus      s   = NATS_OK;
    natsConnection  *nc = NULL;
    char            reply[jsReplyPrefixLen + jsReplyTokenSize + 1];

    if ((js == NULL) || (msg == NULL) || (*msg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (opts != NULL)
    {
        s = _setHeadersFromOptions(*msg, opts);
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    // On success, the context will be retained.
    s = _registerPubMsg(&nc, reply, js, *msg);
    if (s == NATS_OK)
    {
        s = natsConn_publish(nc, *msg, (const char*) reply, false);
        if (s != NATS_OK)
        {
            char *id = reply+jsReplyPrefixLen;

            // The message may or may not have been sent, we don't know for sure.
            // We are going to attempt to remove from the map. If we can, then
            // we return the failure and the user owns the message. If we can't
            // it means that its ack has already been processed, so we consider
            // this call a success. If there was a pub ack failure, it is handled
            // with the error callback, but regardless, the library owns the message.
            js_lock(js);
            // If msg no longer in map, Remove() will return NULL.
            if (natsStrHash_Remove(js->pm, id) == NULL)
                s = NATS_OK;
            else
                js->pmcount--;
            js_unlock(js);
        }
    }

    // On success, clear the pointer to the message to indicate that the library
    // now owns it. If user calls natsMsg_Destroy(), it will have no effect since
    // they would call with natsMsg_Destroy(NULL), which is a no-op.
    if (s == NATS_OK)
        *msg = NULL;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PublishAsyncComplete(jsCtx *js, jsPubOptions *opts)
{
    natsStatus  s       = NATS_OK;
    int64_t     ttl     = 0;
    int64_t     target  = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (opts != NULL)
    {
        s = _checkMaxWaitOpt(&ttl, opts);
        if (s != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    js_lock(js);
    if ((js->pm == NULL) || (js->pmcount == 0))
    {
        js_unlock(js);
        return NATS_OK;
    }
    if (ttl > 0)
        target = nats_setTargetTime(ttl);

    _retain(js);
    js->pacw++;
    while ((s != NATS_TIMEOUT) && (js->pmcount > 0))
    {
        if (target > 0)
            s = natsCondition_AbsoluteTimedWait(js->cond, js->mu, target);
        else
            natsCondition_Wait(js->cond, js->mu);
    }
    js->pacw--;

    // Make sure that if we return timeout, there is really
    // still unack'ed publish messages.
    if ((s == NATS_TIMEOUT) && (js->pmcount == 0))
        s = NATS_OK;

    js_unlockAndRelease(js);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PublishAsyncGetPendingList(natsMsgList *pending, jsCtx *js)
{
    natsStatus          s        = NATS_OK;
    int                 count    = 0;

    if ((pending == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(js);
    if ((count = natsStrHash_Count(js->pm)) == 0)
    {
        js_unlock(js);
        return NATS_NOT_FOUND;
    }
    pending->Msgs  = (natsMsg**) NATS_CALLOC(count, sizeof(natsMsg*));
    if (pending->Msgs == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
    {
        natsStrHashIter iter;
        void            *val = NULL;
        int             i    = 0;

        natsStrHashIter_Init(&iter, js->pm);
        while (natsStrHashIter_Next(&iter, NULL, &val))
        {
            pending->Msgs[i++] = (natsMsg*) val;
            natsStrHashIter_RemoveCurrent(&iter);
            if (js->pmcount > 0)
                js->pmcount--;
        }
        *(int*)&(pending->Count) = count;
    }
    js_unlock(js);

    if (s != NATS_OK)
        natsMsgList_Destroy(pending);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsSubOptions_Init(jsSubOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(jsSubOptions));
    return NATS_OK;
}

static natsStatus
_lookupStreamBySubject(const char **stream, natsConnection *nc, const char *subject, jsOptions *jo, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *apiSubj= NULL;
    natsMsg             *resp   = NULL;

    *stream = NULL;

    // Request will be: {"subject":"<subject>"}
    s = natsBuf_Create(&buf, 14 + (int) strlen(subject));
    IFOK(s, natsBuf_Append(buf, "{\"subject\":\"", -1));
    IFOK(s, natsBuf_Append(buf, subject, -1));
    IFOK(s, natsBuf_Append(buf, "\"}", -1));
    if (s == NATS_OK)
    {
        if (nats_asprintf(&apiSubj, jsApiStreams, js_lenWithoutTrailingDot(jo->Prefix), jo->Prefix) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, apiSubj, natsBuf_Data(buf), natsBuf_Len(buf), jo->Wait));
    // If no error, decode response
    if ((s == NATS_OK) && (resp != NULL) && (natsMsg_GetDataLength(resp) > 0))
    {
        nats_JSON   *json     = NULL;
        char        **streams = NULL;
        int         count     = 0;
        int         i;

        s = nats_JSONParse(&json, natsMsg_GetData(resp), natsMsg_GetDataLength(resp));
        IFOK(s, nats_JSONGetArrayStr(json, "streams", &streams, &count));

        if ((s == NATS_OK) && (count > 0))
            *stream = streams[0];
        else
            s = nats_setError(NATS_ERR, "%s", jsErrNoStreamMatchesSubject);

        // Do not free the first one since we want to return it.
        for (i=1; i<count; i++)
            NATS_FREE(streams[i]);
        NATS_FREE(streams);
        nats_JSONDestroy(json);
    }

    NATS_FREE(apiSubj);
    natsBuf_Destroy(buf);
    natsMsg_Destroy(resp);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsSub_free(jsSub *jsi)
{
    jsCtx *js = NULL;

    if (jsi == NULL)
        return;

    js = jsi->js;
    NATS_FREE(jsi->stream);
    NATS_FREE(jsi->consumer);
    NATS_FREE(jsi->fcReply);
    NATS_FREE(jsi);

    js_release(js);
}

static void
_autoAckCB(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    jsSub   *jsi = (jsSub*) closure;
    char    _reply[256];
    char    *reply = NULL;
    bool    frply  = false;

    if (strlen(msg->reply) < sizeof(_reply))
    {
        snprintf(_reply, sizeof(_reply), "%s", msg->reply);
        reply = _reply;
    }
    else
    {
        reply = NATS_STRDUP(msg->reply);
        frply = (reply != NULL ? true : false);
    }

    // Invoke user callback
    (jsi->usrCb)(nc, sub, msg, jsi->usrCbClosure);

    // Ack the message (unless we got a failure copying the reply subject)
    if (reply == NULL)
        return;

    natsConnection_PublishString(nc, reply, jsAckAck);

    if (frply)
        NATS_FREE(reply);
}

natsStatus
jsSub_unsubscribe(jsSub *jsi, bool drainMode)
{
    natsStatus s;

    // We want to cleanup only JS consumer that has been
    // created by the library (case of an ephemeral and
    // without queue sub), but we also don't delete if
    // we are in drain mode.
    if (drainMode || !jsi->delCons)
        return NATS_OK;

    s = js_DeleteConsumer(jsi->js, jsi->stream, jsi->consumer, NULL, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_copyString(char **new_str, const char *str, int l)
{
    *new_str = NATS_MALLOC(l+1);
    if (*new_str == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(*new_str, str, l);
    *(*new_str+l) = '\0';
    return NATS_OK;
}

static natsStatus
_getMetaData(const char *reply,
    char **stream,
    char **consumer,
    uint64_t *numDelivered,
    uint64_t *sseq,
    uint64_t *dseq,
    int64_t *tm,
    uint64_t *numPending,
    int asked)
{
    natsStatus  s    = NATS_OK;
    const char  *p   = reply;
    const char  *str = NULL;
    int         done = 0;
    int64_t     val  = 0;
    int         i, l;

    for (i=0; i<7; i++)
    {
        str = p;
        p = strchr(p, '.');
        if (p == NULL)
        {
            if (i < 6)
                return NATS_ERR;
            p = strrchr(str, '\0');
        }
        l = (int) (p-str);
        if (i > 1)
        {
            val = nats_ParseInt64(str, l);
            // Since we don't expect any negative value,
            // if we get -1, which indicates a parsing error,
            // return this fact.
            if (val == -1)
                return NATS_ERR;
        }
        switch (i)
        {
            case 0:
                if (stream != NULL)
                {
                    if ((s = _copyString(stream, str, l)) != NATS_OK)
                        return NATS_UPDATE_ERR_STACK(s);
                    done++;
                }
                break;
            case 1:
                if (consumer != NULL)
                {
                    if ((s = _copyString(consumer, str, l)) != NATS_OK)
                        return NATS_UPDATE_ERR_STACK(s);
                    done++;
                }
                break;
            case 2:
                if (numDelivered != NULL)
                {
                    *numDelivered = (uint64_t) val;
                    done++;
                }
                break;
            case 3:
                if (sseq != NULL)
                {
                    *sseq = (uint64_t) val;
                    done++;
                }
                break;
            case 4:
                if (dseq != NULL)
                {
                    *dseq = (uint64_t) val;
                    done++;
                }
                break;
            case 5:
                if (tm != NULL)
                {
                    *tm = val;
                    done++;
                }
                break;
            case 6:
                if (numPending != NULL)
                {
                    *numPending = (uint64_t) val;
                    done++;
                }
                break;
        }
        if (done == asked)
            return NATS_OK;
        p++;
    }
    return NATS_OK;
}

natsStatus
jsSub_trackSequences(jsSub *jsi, const char *reply)
{
    natsStatus  s;

    if ((reply == NULL) || (strstr(reply, jsAckPrefix) != reply))
        return NATS_OK;

    s = _getMetaData(reply+jsAckPrefixLen, NULL, NULL, NULL, &jsi->sseq, &jsi->dseq, NULL, NULL, 2);
    if (s != NATS_OK)
    {
        if (s == NATS_ERR)
            return nats_setError(NATS_ERR, "invalid JS ACK: '%s'", reply);
        return NATS_UPDATE_ERR_STACK(s);
    }
    return NATS_OK;
}

natsStatus
jsSub_processSequenceMismatch(natsSubscription *sub, natsMsg *msg, bool *sm)
{
    jsSub       *jsi   = sub->jsi;
    const char  *str   = NULL;
    int64_t     val    = 0;

    *sm = false;

    if (jsi->dseq == 0)
        return NATS_OK;

    // This function is invoked as long as the message does not
    // have any data and has a header status of 100, but does not check
    // if this is an hearbeat (in status description).
    // If it is an HB it should have the following header field. If not
    // present, do not treat this as an error.
    if (natsMsgHeader_Get(msg, jsLastConsumerSeqHdr, &str) != NATS_OK)
        return NATS_OK;

    // Now that we have the field, we parse it. This function returns
    // -1 if there is a parsing error.
    val = nats_ParseInt64(str, (int) strlen(str));
    if (val == -1)
        return nats_setError(NATS_ERR, "invalid last consumer sequence: '%s'", str);

    jsi->ldseq = (uint64_t) val;
    if (jsi->ldseq == jsi->dseq)
    {
        // Sync subs use this flag to get the NextMsg() to error out and
        // return NATS_MISMATCH to indicate that a mismatch was discovered,
        // but immediately switch it off so that remaining NextMsg() work ok.
        // Here we have resolved the mismatch, so we clear this flag (we
        // could check for sync vs async, but no need to bother).
        jsi->sm = false;
        // Clear the suppression flag.
        jsi->ssmn = false;
    }
    else if (!jsi->ssmn)
    {
        // Record the sequence mismatch.
        jsi->sm = true;
        // Prevent following mismatch report until mismatch is resolved.
        jsi->ssmn = true;
        // Only for async subscriptions, indicate that the connection should
        // push a NATS_MISMATCH to the async callback.
        if (sub->msgCb != NULL)
            *sm = true;
    }
    return NATS_OK;
}

natsStatus
jsSub_GetSequenceMismatch(jsConsumerSequenceMismatch *csm, natsSubscription *sub)
{
    jsSub *jsi;

    if ((csm == NULL) || (sub == NULL) || (sub->jsi == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    jsi = sub->jsi;
    if (jsi->dseq == jsi->ldseq)
        return NATS_NOT_FOUND;

    memset(csm, 0, sizeof(jsConsumerSequenceMismatch));
    csm->Stream = jsi->sseq;
    csm->ConsumerClient = jsi->dseq;
    csm->ConsumerServer = jsi->ldseq;
    return NATS_OK;
}

natsStatus
jsSub_scheduleFlowControlResponse(jsSub *jsi, natsSubscription *sub, const char *reply)
{
    NATS_FREE(jsi->fcReply);
    jsi->fcReply = NATS_STRDUP(reply);
    if (jsi->fcReply == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    jsi->fcDelivered = sub->delivered + (uint64_t) sub->msgList.msgs;

    return NATS_OK;
}

static natsStatus
_subscribe(natsSubscription **new_sub, jsCtx *js, const char *subject,
           natsMsgHandler cb, void *cbClosure, bool isPullMode,
           jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus          s           = NATS_OK;
    const char          *stream     = NULL;
    const char          *consumer   = NULL;
    const char          *deliver    = NULL;
    jsErrCode           jerr        = 0;
    jsConsumerInfo      *info       = NULL;
    bool                lookupErr   = false;
    bool                consBound   = false;
    bool                hasFC       = false;
    bool                delCons     = false;
    int64_t             maxMsgs     = jsDefaultMaxMsgs;
    natsConnection      *nc         = NULL;
    bool                freePfx     = false;
    bool                freeStream  = false;
    jsSub               *jsi        = NULL;
    char                inbox[NATS_INBOX_PRE_LEN+NUID_BUFFER_LEN+1];
    jsOptions           jo;
    jsSubOptions        o;

    if ((new_sub == NULL) || (js == NULL) || nats_IsStringEmpty(subject))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = js_setOpts(&nc, &freePfx, js, jsOpts, &jo);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // If `opts` is not specified, point to a stack initialized one so
    // we don't have to keep checking if `opts` is NULL or not.
    if (opts == NULL)
    {
        jsSubOptions_Init(&o);
        opts = &o;
    }

    hasFC    = opts->Config.FlowControl;
    stream   = opts->Stream;
    consumer = opts->Consumer;
    consBound= (!nats_IsStringEmpty(stream) && !nats_IsStringEmpty(consumer));

    // With flow control enabled async subscriptions we will bump msgs
    // limits, and set a larger pending bytes limit by default.
    if (!isPullMode && (cb != NULL) && hasFC)
        maxMsgs *= 16;

    // In case a consumer has not been set explicitly, then the
    // durable name will be used as the consumer name (which
    // will result in `consumer` still be possibly NULL).
    if (nats_IsStringEmpty(consumer))
        consumer = opts->Config.Durable;

    // Find the stream mapped to the subject if not bound to a stream already,
    // that is, if user did not provide a `Stream` name through options).
    if (nats_IsStringEmpty(stream))
    {
        s = _lookupStreamBySubject(&stream, nc, subject, &jo, errCode);
        if (s != NATS_OK)
            goto END;

        freeStream = true;
    }

    // With an explicit durable name, we can lookup the consumer first
    // to which it should be attaching to.
    if (!nats_IsStringEmpty(consumer))
    {
        s = js_GetConsumerInfo(&info, js, stream, consumer, &jo, &jerr);
        lookupErr = (s == NATS_TIMEOUT) || (jerr == JSNotEnabledErr);
    }

    if (info != NULL)
    {
        bool dlvSubjEmpty = false;

        // Attach using the found consumer config.
        jsConsumerConfig *ccfg = info->Config;

        // Make sure this new subject matches or is a subset.
        if (!nats_IsStringEmpty(ccfg->FilterSubject) && (strcmp(subject, ccfg->FilterSubject) != 0))
        {
            s = nats_setError(NATS_ERR, "subject '%s' does not match consumer filter subject '%s'",
                              subject, ccfg->FilterSubject);
            goto END;
        }

        dlvSubjEmpty = nats_IsStringEmpty(ccfg->DeliverSubject);

        // Prevent binding a subscription against incompatible consumer types.
        if (isPullMode && !dlvSubjEmpty)
        {
            s = nats_setError(NATS_ERR, "%s", jsErrPullSubscribeToPushConsumer);
            goto END;
        }
        else if (!isPullMode && dlvSubjEmpty)
        {
            s = nats_setError(NATS_ERR, "%s", jsErrPullSubscribeRequired);
            goto END;
        }

        if (!dlvSubjEmpty)
            deliver = (natsInbox*) ccfg->DeliverSubject;
        else if (!isPullMode)
        {
            natsInbox_init(inbox, sizeof(inbox));
            deliver = (const char*) inbox;
        }
    }
    else if (((s != NATS_OK) && (s != NATS_NOT_FOUND)) || ((s == NATS_NOT_FOUND) && consBound))
    {
        // If the consumer is being bound and got an error on pull subscribe then allow the error.
        if (!(isPullMode && lookupErr && consBound))
            goto END;

        s = NATS_OK;
    }
    else
    {
        jsConsumerConfig cfg;

        // Make a shallow copy of the provided consumer config
        // since we may have to change some fields before calling
        // AddConsumer.
        memcpy(&cfg, &(opts->Config), sizeof(jsConsumerConfig));

        // Attempt to create consumer if not found nor binding.
        natsInbox_init(inbox, sizeof(inbox));
        deliver = (const char*) inbox;

        if (!isPullMode)
            cfg.DeliverSubject = (const char*) deliver;

        // Do filtering always, server will clear as needed.
        cfg.FilterSubject = subject;

        // If we have acks at all and the MaxAckPending is not set go ahead
        // and set to the internal max.
        if ((cfg.MaxAckPending == 0) && (cfg.AckPolicy != js_AckNone))
            cfg.MaxAckPending = maxMsgs;

        // Multiple subscribers could compete in creating the first consumer
        // that will be shared using the same durable name. If this happens, then
        // do a lookup of the consumer info subscribe using the latest info.
        s = js_AddConsumer(&info, js, stream, &cfg, &jo, &jerr);
        if (s != NATS_OK)
        {
            jsConsumerConfig *ccfg = NULL;

            if ((jerr != JSConsumerExistingActiveErr) && (jerr != JSConsumerNameExistErr))
                goto END;

            jsConsumerInfo_Destroy(info);
            info = NULL;

            s = js_GetConsumerInfo(&info, js, stream, consumer, &jo, &jerr);
            if (s != NATS_OK)
                goto END;

            // Attach using the found consumer config.
            ccfg = info->Config;

            // Validate that the original subject does still match.
            if (!nats_IsStringEmpty(ccfg->FilterSubject) && (strcmp(subject, ccfg->FilterSubject) != 0))
            {
                s = nats_setError(NATS_ERR, "subject '%s' does not match consumer filter subject '%s'",
                                  subject, ccfg->FilterSubject);
                goto END;
            }
        }
        else if (nats_IsStringEmpty(opts->Queue) && nats_IsStringEmpty(opts->Config.Durable))
        {
            // Library will delete the consumer on Unsubscribe() only if
            // it is the one that created the consumer and that there is
            // no Queue or Durable name specified.
            delCons = true;
        }
        if (freeStream)
        {
            NATS_FREE((char*) stream);
            freeStream = false;
        }
        stream   = info->Stream;
        consumer = info->Name;
        deliver  = (natsInbox*) info->Config->DeliverSubject;
    }

    if (s == NATS_OK)
    {
        jsi = (jsSub*) NATS_CALLOC(1, sizeof(jsSub));
        if (jsi == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            DUP_STRING(s, jsi->stream, stream);
            IF_OK_DUP_STRING(s, jsi->consumer, consumer);
            if (s == NATS_OK)
            {
                jsi->js       = js;
                jsi->delCons  = delCons;
                jsi->hasHBs   = (opts->Config.Heartbeat > 0 ? true : false);
                jsi->hasFC    = hasFC;
                js_retain(js);

                if ((cb != NULL) && !opts->ManualAck && (opts->Config.AckPolicy != js_AckNone))
                {
                    // Keep track of user provided CB and closure
                    jsi->usrCb          = cb;
                    jsi->usrCbClosure   = cbClosure;
                    // Use our own when creating the NATS subscription.
                    cb          = _autoAckCB;
                    cbClosure   = (void*) jsi;
                }
            }
        }
    }
    IFOK(s, natsConn_subscribeImpl(new_sub, nc, true, (const char*) deliver, opts->Queue, 0, cb, cbClosure, false, jsi));
    IFOK(s, natsSubscription_SetPendingLimits(*new_sub, (int) maxMsgs, NATS_OPTS_DEFAULT_MAX_PENDING_MSGS*1024));

END:
    if (s != NATS_OK)
    {
        if (delCons)
        {
            jsErrCode ljerr = 0;

            js_DeleteConsumer(js, stream, consumer, &jo, &ljerr);
            if (jerr == 0)
                ljerr = jerr;
        }

        jsSub_free(jsi);

        if (errCode != NULL)
            *errCode = jerr;
    }

    // Common cleanup regardless of success or not.
    jsConsumerInfo_Destroy(info);
    if (freePfx)
        NATS_FREE((char*) jo.Prefix);
    if (freeStream)
        NATS_FREE((char*) stream);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_Subscribe(natsSubscription **sub, jsCtx *js, const char *subject,
             natsMsgHandler cb, void *cbClosure,
             jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    if (errCode != NULL)
        *errCode = 0;

    if (cb == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    return _subscribe(sub, js, subject, cb, cbClosure, false, jsOpts, opts, errCode);
}

natsStatus
js_SubscribeSync(natsSubscription **sub, jsCtx *js, const char *subject,
                 jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    if (errCode != NULL)
        *errCode = 0;

    return _subscribe(sub, js, subject, NULL, NULL, false, jsOpts, opts, errCode);
}

natsStatus
js_PullSubscribe(natsSubscription **sub, jsCtx *js, const char *subject,
                 jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    if (errCode != NULL)
        *errCode = 0;

    // Check for invalid ack policy
    if (opts != NULL)
    {
        jsAckPolicy p = (opts->Config.AckPolicy);

        if ((p == js_AckNone) || (p == js_AckAll))
            return nats_setError(NATS_INVALID_ARG,
                                 "invalid ack mode '%s' for pull consumers",
                                 jsAckPolicyStr(p));
    }

    return _subscribe(sub, js, subject, NULL, NULL, true, jsOpts, opts, errCode);
}

static natsStatus
_ackMsg(natsMsg *msg, jsOptions *opts, const char *ackType, bool inProgress, bool sync, jsErrCode *errCode)
{
    natsSubscription    *sub = NULL;
    natsConnection      *nc  = NULL;
    jsCtx               *js  = NULL;
    jsSub               *jsi = NULL;
    natsStatus          s    = NATS_OK;

    if (msg == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (natsMsg_isAcked(msg))
        return NATS_OK;

    if (msg->sub == NULL)
        return nats_setError(NATS_ILLEGAL_STATE, "%s", jsErrMsgNotBound);

    if (nats_IsStringEmpty(msg->reply))
        return nats_setError(NATS_ILLEGAL_STATE, "%s", jsErrMsgNotJS);

    // All these are immutable and don't need locking.
    sub = msg->sub;
    jsi = sub->jsi;
    js = jsi->js;
    nc = sub->conn;

    if (sync)
    {
        natsMsg *rply   = NULL;
        int64_t wait    = (opts != NULL ? opts->Wait : 0);

        if (wait == 0)
        {
            js_lock(js);
            wait = js->opts.Wait;
            if (wait == 0)
                wait = jsDefaultRequestWait;
            js_unlock(js);
        }
        IFOK_JSR(s, natsConnection_RequestString(&rply, nc, msg->reply, ackType, wait));
        natsMsg_Destroy(rply);
    }
    else
    {
        s = natsConnection_PublishString(nc, msg->reply, ackType);
    }
    // Indicate that we have ack'ed the message
    if ((s == NATS_OK) && !inProgress)
        natsMsg_setAcked(msg);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_Ack(natsMsg *msg, jsOptions *opts)
{
    return _ackMsg(msg, opts, jsAckAck, false, false, NULL);
}

natsStatus
natsMsg_AckSync(natsMsg *msg, jsOptions *opts, jsErrCode *errCode)
{
    return _ackMsg(msg, opts, jsAckAck, false, true, errCode);
}

natsStatus
natsMsg_Nak(natsMsg *msg, jsOptions *opts)
{
    return _ackMsg(msg, opts, jsAckNak, false, false, NULL);
}

natsStatus
natsMsg_InProgress(natsMsg *msg, jsOptions *opts)
{
    return _ackMsg(msg, opts, jsAckInProgress, true, false, NULL);
}

natsStatus
natsMsg_Term(natsMsg *msg, jsOptions *opts)
{
    return _ackMsg(msg, opts, jsAckTerm, false, false, NULL);
}

natsStatus
natsMsg_GetMetaData(jsMsgMetaData **new_meta, natsMsg *msg)
{
    jsMsgMetaData   *meta = NULL;
    natsStatus      s;

    if ((new_meta == NULL) || (msg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

     if (msg->sub == NULL)
        return nats_setError(NATS_ILLEGAL_STATE, "%s", jsErrMsgNotBound);

    if (nats_IsStringEmpty(msg->reply))
        return nats_setError(NATS_ILLEGAL_STATE, "%s", jsErrMsgNotJS);

    if (strstr(msg->reply, jsAckPrefix) != msg->reply)
        return nats_setError(NATS_ERR, "invalid meta data '%s'", msg->reply);

    meta = (jsMsgMetaData*) NATS_CALLOC(1, sizeof(jsMsgMetaData));
    if (meta == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = _getMetaData(msg->reply+jsAckPrefixLen,
                        &(meta->Stream),
                        &(meta->Consumer),
                        &(meta->NumDelivered),
                        &(meta->Sequence.Stream),
                        &(meta->Sequence.Consumer),
                        &(meta->Timestamp),
                        &(meta->NumPending),
                        7);
    if (s == NATS_ERR)
        s = nats_setError(NATS_ERR, "invalid meta data '%s'", msg->reply);

    if (s == NATS_OK)
        *new_meta = meta;
    else
        jsMsgMetaData_Destroy(meta);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsMsgMetaData_Destroy(jsMsgMetaData *meta)
{
    if (meta == NULL)
        return;

    NATS_FREE(meta->Stream);
    NATS_FREE(meta->Consumer);
    NATS_FREE(meta);
}

bool
natsMsg_isJSCtrl(natsMsg *msg, int *ctrlType)
{
    char *p = NULL;

    *ctrlType = 0;

    if ((msg->dataLen > 0) || (msg->hdrLen <= 0))
        return false;

    if (strstr(msg->hdr, HDR_LINE_PRE) != msg->hdr)
        return false;

    p = msg->hdr + HDR_LINE_PRE_LEN;
    if (*p != ' ')
        return false;

    while ((*p != '\0') && isspace(*p))
        p++;

    if ((*p == '\r') || (*p == '\n') || (*p == '\0'))
        return false;

    if (strstr(p, CTRL_STATUS) != p)
        return false;

    p += HDR_STATUS_LEN;

    if (!isspace(*p))
        return false;

    while (isspace(*p))
        p++;

    if (strstr(p, "Idle") == p)
        *ctrlType = jsCtrlHeartbeat;
    else if (strstr(p, "Flow") == p)
        *ctrlType = jsCtrlFlowControl;

    return true;
}
