// Copyright 2021-2024 The NATS Authors
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
#include <limits.h>

#include "js.h"
#include "mem.h"
#include "conn.h"
#include "util.h"
#include "opts.h"
#include "sub.h"
#include "dispatch.h"
#include "glib/glib.h"

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
const int64_t    jsOrderedHBInterval     = NATS_SECONDS_TO_NANOS(5);

#define jsReplyTokenSize    (8)
#define jsDefaultMaxMsgs    (512 * 1024)

#define jsLastConsumerSeqHdr    "Nats-Last-Consumer"

// Forward declarations
static void _hbTimerFired(natsTimer *timer, void* closure);
static void _releaseSubWhenStopped(natsTimer *timer, void* closure);

typedef struct __jsOrderedConsInfo
{
    int64_t             osid;
    int64_t             nsid;
    uint64_t            sseq;
    natsConnection      *nc;
    natsSubscription    *sub;
    char                *ndlv;
    natsThread          *thread;
    int                 max;
    bool                done;

} jsOrderedConsInfo;

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
    natsTimer_Destroy(js->pmtmr);
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

    if (js == NULL)
        return;

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

static void
_destroyPMInfo(pmInfo *pmi)
{
    if (pmi == NULL)
        return;

    NATS_FREE(pmi->subject);
    NATS_FREE(pmi);
}

void
jsCtx_Destroy(jsCtx *js)
{
    pmInfo *pm;

    if (js == NULL)
        return;

    js_lock(js);
    if (js->closed)
    {
        js_unlock(js);
        return;
    }
    js->closed = true;
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
    while ((pm = js->pmHead) != NULL)
    {
        js->pmHead = pm->next;
        _destroyPMInfo(pm);
    }
    if (js->pmtmr != NULL)
        natsTimer_Stop(js->pmtmr);
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
    // This will be immutable and is computed based on the possible custom
    // inbox prefix length (or the default "_INBOX.")
    js->rpreLen = nc->inboxPfxLen+jsReplyTokenSize+1;

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
        // This takes precedence to error handler
        if (opts->PublishAsync.AckHandler != NULL)
        {
            pa->AckHandler          = opts->PublishAsync.AckHandler;
            pa->AckHandlerClosure   = opts->PublishAsync.AckHandlerClosure;
        }
        else
        {
            pa->ErrHandler          = opts->PublishAsync.ErrHandler;
            pa->ErrHandlerClosure   = opts->PublishAsync.ErrHandlerClosure;
        }
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
        resOpts->Stream.Info.SubjectsFilter = (opts == NULL ? js->opts.Stream.Info.SubjectsFilter : opts->Stream.Info.SubjectsFilter);

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
    if (s == NATS_OK)
    {
        if (opts->ExpectNoMessage)
        {
            s = natsMsgHeader_Set(msg, jsExpectedLastSubjSeqHdr, "0");
        }
        else if (opts->ExpectLastSubjectSeq > 0)
        {
            snprintf(temp, sizeof(temp), "%" PRIu64, opts->ExpectLastSubjectSeq);
            s = natsMsgHeader_Set(msg, jsExpectedLastSubjSeqHdr, temp);
        }
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
    jsApiResponse       ar      = JS_EMPTY_API_RESPONSE;

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
                IFOK(s, nats_JSONGetStr(json, "domain", &(pa->Domain)));

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
    NATS_FREE(pa->Domain);
    NATS_FREE(pa);
}

static void
_freePubAck(jsPubAck *pa)
{
    if (pa == NULL)
        return;

    NATS_FREE(pa->Stream);
    NATS_FREE(pa->Domain);
}

static natsStatus
_parsePubAck(natsMsg *msg, jsPubAck *pa, jsPubAckErr *pae, char *errTxt, size_t errTxtSize)
{
    natsStatus  s       = NATS_OK;
    jsErrCode   jerr    = 0;

    if (natsMsg_isTimeout(msg))
    {
        s = NATS_TIMEOUT;
    }
    else if (natsMsg_IsNoResponders(msg))
    {
        s = NATS_NO_RESPONDERS;
    }
    else
    {
        nats_JSON           *json = NULL;
        jsApiResponse       ar;

        // Now unmarshal the API response and check if there was an error.
        s = js_unmarshalResponse(&ar, &json, msg);
        if (s == NATS_OK)
        {
            if (js_apiResponseIsErr(&ar))
            {
                s = NATS_ERR;
                jerr = (jsErrCode) ar.Error.ErrCode;
                snprintf(errTxt, errTxtSize, "%s", ar.Error.Description);
            }
            // If it is not an error and caller wants to decode the jsPubAck...
            else if (pa != NULL)
            {
                memset(pa, 0, sizeof(jsPubAck));
                s = nats_JSONGetStr(json, "stream", &(pa->Stream));
                IFOK(s, nats_JSONGetULong(json, "seq", &(pa->Sequence)));
                IFOK(s, nats_JSONGetBool(json, "duplicate", &(pa->Duplicate)));
                IFOK(s, nats_JSONGetStr(json, "domain", &(pa->Domain)));
            }

            js_freeApiRespContent(&ar);
            nats_JSONDestroy(json);
        }
    }
    // This will handle the no responder or timeout cases, or if we had errors
    // trying to unmarshal the response.
    if (s != NATS_OK)
    {
        // Set the error text only if not already done.
        if (errTxt[0] == '\0')
            snprintf(errTxt, errTxtSize, "%s", natsStatus_GetText(s));

        memset(pae, 0, sizeof(jsPubAckErr));
        pae->Err = s;
        pae->ErrCode = jerr;
        pae->ErrText = errTxt;
    }
    return s;
}

static void
_handleAsyncReply(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    const char      *subject    = natsMsg_GetSubject(msg);
    char            *id         = NULL;
    jsCtx           *js         = (jsCtx*) closure;
    natsMsg         *pmsg       = NULL;
    char            errTxt[256] = {'\0'};
    jsPubAckErr     pae;
    jsPubAck        pa;
    struct jsOptionsPublishAsync *opa = NULL;

    if ((subject == NULL) || (int) strlen(subject) <= js->rpreLen)
    {
        natsMsg_Destroy(msg);
        return;
    }

    id = (char*) (subject+js->rpreLen);

    js_lock(js);

    pmsg = natsStrHash_Remove(js->pm, id);
    if (pmsg == NULL)
    {
        js_unlock(js);
        natsMsg_Destroy(msg);
        return;
    }

    opa = &(js->opts.PublishAsync);
    if (opa->AckHandler)
    {
        jsPubAckErr *ppae   = NULL;
        jsPubAck    *ppa    = NULL;

        // If _parsePubAck returns an error, we will set the pointer ppae to
        // our stack variable 'pae', otherwise, set the pointer ppa to the
        // stack variable 'pa', which is the jsPubAck (positive ack).
        if (_parsePubAck(msg, &pa, &pae, errTxt, sizeof(errTxt)) != NATS_OK)
            ppae = &pae;
        else
            ppa = &pa;

        // Invoke the handler with pointer to either jsPubAck or jsPubAckErr.
        js_unlock(js);

        (opa->AckHandler)(js, pmsg, ppa, ppae, opa->AckHandlerClosure);

        js_lock(js);

        _freePubAck(ppa);
        // Set pmsg to NULL because user was responsible for destroying the message.
        pmsg = NULL;
    }
    else if ((opa->ErrHandler != NULL) && (_parsePubAck(msg, NULL, &pae, errTxt, sizeof(errTxt)) != NATS_OK))
    {
        // We will invoke CB only if there is any kind of error.
        // Associate the message with the pubAckErr object.
        pae.Msg = pmsg;
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
            js->rpre = NATS_MALLOC(js->rpreLen+1);
            if (js->rpre == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                char nuid[NUID_BUFFER_LEN+1];

                s = natsNUID_Next(nuid, sizeof(nuid));
                if (s == NATS_OK)
                {
                    memcpy(js->rpre, js->nc->inboxPfx, js->nc->inboxPfxLen);
                    memcpy(js->rpre+js->nc->inboxPfxLen, nuid+((int)strlen(nuid)-jsReplyTokenSize), jsReplyTokenSize);
                    js->rpre[js->rpreLen-1] = '.';
                    js->rpre[js->rpreLen]   = '\0';
                }
            }
        }
        if (s == NATS_OK)
        {
            char *subj = NULL;

            if (nats_asprintf(&subj, "%s*", js->rpre) < 0)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
                s = natsConn_subscribeNoPool(&(js->rsub), js->nc, subj, _handleAsyncReply, (void*) js);
            if (s == NATS_OK)
            {
                _retain(js);
                natsSubscription_SetPendingLimits(js->rsub, -1, -1);
                natsSubscription_SetOnCompleteCB(js->rsub, _subComplete, (void*) js);
            }
            NATS_FREE(subj);
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

        memcpy(reply, js->rpre, js->rpreLen);
        l = nats_Rand64();
        for (i=0; i < jsReplyTokenSize; i++)
        {
            reply[js->rpreLen+i] = jsDigits[l%jsBase];
            l /= jsBase;
        }
        reply[js->rpreLen+jsReplyTokenSize] = '\0';
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_timeoutPubAsync(natsTimer *t, void *closure)
{
    jsCtx   *js = (jsCtx*) closure;
    pmInfo  *pm = NULL;
    int64_t now = nats_Now();
    int64_t next= 0;

    js_lock(js);
    if (js->closed)
    {
        js_unlock(js);
        return;
    }

    while (((pm = js->pmHead) != NULL) && (pm->deadline <= now))
    {
        natsMsg *m = NULL;

        if (natsMsg_Create(&m, pm->subject, NULL, NULL, 0) != NATS_OK)
            break;

        natsMsg_setTimeout(m);

        // Best attempt, ignore NATS_SLOW_CONSUMER errors which may be returned
        // here.
        nats_lockSubAndDispatcher(js->rsub);
        natsSub_enqueueUserMessage(js->rsub, m);
        nats_unlockSubAndDispatcher(js->rsub);

        js->pmHead = pm->next;
        _destroyPMInfo(pm);
    }

    if (js->pmHead == NULL)
    {
        if (js->pmTail != NULL)
            js->pmTail = NULL;

        next = 60*60*1000;
    }
    else
    {
        next = js->pmHead->deadline - now;
        if (next <= 0)
            next = 1;
    }
    natsTimer_Reset(js->pmtmr, next);

    js_unlock(js);
}

static void
_timeoutPubAsyncComplete(natsTimer *t, void *closure)
{
    jsCtx *js = (jsCtx*) closure;
    js_release(js);
}

static natsStatus
_trackPublishAsyncTimeout(jsCtx *js, char *subject, int64_t mw)
{
    natsStatus  s       = NATS_OK;
    pmInfo      *pm     = NULL;
    pmInfo      *pmi    = NATS_CALLOC(1, sizeof(pmInfo));

    if (pmi == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    pmi->subject = NATS_STRDUP(subject);
    if (pmi->subject == NULL)
    {
        NATS_FREE(pmi);
        return nats_setDefaultError(NATS_NO_MEMORY);
    }
    pmi->deadline = nats_Now() + mw;

    // Check if we can add at the end of the list
    if (((pm = js->pmTail) != NULL) && (pmi->deadline >= pm->deadline))
    {
        js->pmTail->next = pmi;
        js->pmTail = pmi;
    }
    // Or before the first
    else if (((pm = js->pmHead) != NULL) && (pmi->deadline < pm->deadline))
    {
        pmi->next = js->pmHead;
        js->pmHead = pmi;
        natsTimer_Reset(js->pmtmr, mw);
    }
    // If the list was empty
    else if (js->pmHead == NULL)
    {
        js->pmHead = pmi;
        js->pmTail = pmi;
        if (js->pmtmr == NULL)
        {
            s = natsTimer_Create(&js->pmtmr, _timeoutPubAsync, _timeoutPubAsyncComplete, mw, (void*) js);
            if (s == NATS_OK)
                js_retain(js);
        }
        else
            natsTimer_Reset(js->pmtmr, mw);
    }
    else
    {
        // Guaranteed to be somewhere in the list (not first, not last, and at
        // least 2 elements).
        pm = js->pmHead;
        while ((pm != NULL) && (pm->next != NULL) && (pmi->deadline > pm->next->deadline))
            pm = pm->next;

        pmi->next = pm->next;
        pm->next = pmi;
    }
    if (s != NATS_OK)
        _destroyPMInfo(pmi);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_registerPubMsg(natsConnection **nc, char *reply, jsCtx *js, natsMsg *msg, int64_t mw)
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
        id = reply+js->rpreLen;
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
    if ((s == NATS_OK) && (mw > 0))
        s = _trackPublishAsyncTimeout(js, reply, mw);
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
    char            replyBuf[32 + jsReplyTokenSize + 1];
    char            *reply = replyBuf;
    int64_t         mw = 0;

    if ((js == NULL) || (msg == NULL) || (*msg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (js->rpreLen > 32)
    {
        reply = NATS_MALLOC(js->rpreLen + jsReplyTokenSize + 1);
        if (reply == NULL)
            return nats_setDefaultError(NATS_NO_MEMORY);
    }

    if (opts != NULL)
    {
        mw = opts->MaxWait;
        s = _setHeadersFromOptions(*msg, opts);
    }

    // On success, the context will be retained.
    IFOK(s, _registerPubMsg(&nc, reply, js, *msg, mw));
    if (s == NATS_OK)
    {
        s = natsConn_publish(nc, *msg, (const char*) reply, false);
        if (s != NATS_OK)
        {
            char *id = reply+js->rpreLen;

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

    if (reply != replyBuf)
        NATS_FREE(reply);

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
        pending->Count = count;
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
    opts->Config.AckPolicy      = -1;
    opts->Config.DeliverPolicy  = -1;
    opts->Config.ReplayPolicy   = -1;
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

static void
_destroyFetch(jsFetch *fetch)
{
    if (fetch == NULL)
        return;

    if (fetch->expiresTimer != NULL)
        natsTimer_Destroy(fetch->expiresTimer);

    NATS_FREE(fetch);
}

void
jsSub_free(jsSub *jsi)
{
    jsCtx *js = NULL;

    if (jsi == NULL)
        return;

    _destroyFetch(jsi->fetch);

    js = jsi->js;
    natsTimer_Destroy(jsi->hbTimer);

    NATS_FREE(jsi->stream);
    NATS_FREE(jsi->consumer);
    NATS_FREE(jsi->nxtMsgSubj);
    NATS_FREE(jsi->cmeta);
    NATS_FREE(jsi->fcReply);
    NATS_FREE(jsi->psubj);
    js_destroyConsumerConfig(jsi->ocCfg);
    NATS_FREE(jsi);

    js_release(js);
}

static void
_autoAckCB(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    jsSub   *jsi = (jsSub*) closure;

    natsMsg_setNoDestroy(msg);

    // Invoke user callback
    (jsi->usrCb)(nc, sub, msg, jsi->usrCbClosure);

    natsMsg_Ack(msg, NULL);
    natsMsg_clearNoDestroy(msg);
    natsMsg_Destroy(msg);
}

natsStatus
jsSub_deleteConsumer(natsSubscription *sub)
{
    jsCtx       *js       = NULL;
    const char  *stream   = NULL;
    const char  *consumer = NULL;
    natsStatus  s;

    natsSub_Lock(sub);
    if ((sub->jsi != NULL) && (sub->jsi->dc))
    {
        js       = sub->jsi->js;
        stream   = sub->jsi->stream;
        consumer = sub->jsi->consumer;
        // For drain, we could be trying to delete from
        // the thread that checks for drain, or from the
        // user checking from drain completion. So we
        // switch off if we are going to delete now.
        sub->jsi->dc = false;
    }
    natsSub_Unlock(sub);

    if ((js == NULL) || (stream == NULL) || (consumer == NULL))
        return NATS_OK;

    s = js_DeleteConsumer(js, stream, consumer, NULL, NULL);
    if (s == NATS_NOT_FOUND)
        s = nats_setError(s, "failed to delete consumer '%s': not found", consumer);
    return NATS_UPDATE_ERR_STACK(s);
}

// Runs under the subscription lock, but lock will be released,
// the connection lock will be possibly acquired/released, then
// the subscription lock reacquired.
void
jsSub_deleteConsumerAfterDrain(natsSubscription *sub)
{
    natsConnection  *nc       = NULL;
    const char      *consumer = NULL;
    natsStatus      s;

    if ((sub->jsi == NULL) || !sub->jsi->dc)
        return;

    nc       = sub->conn;
    consumer = sub->jsi->consumer;

    // Need to release sub lock since deletion of consumer
    // will require the connection lock, etc..
    natsSub_Unlock(sub);

    s = jsSub_deleteConsumer(sub);
    if (s != NATS_OK)
    {
        char tmp[256];
        natsConn_Lock(nc);
        snprintf(tmp, sizeof(tmp), "failed to delete consumer '%s': %u (%s)",
                 consumer, s, natsStatus_GetText(s));
        natsAsyncCb_PostErrHandler(nc, sub, s, NATS_STRDUP(tmp));
        natsConn_Unlock(nc);
    }

    // Reacquire the lock before returning.
    natsSub_Lock(sub);
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

natsStatus
js_getMetaData(const char *reply,
    char **domain,
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
    const char  *np  = NULL;
    const char  *str = NULL;
    int         done = 0;
    int64_t     val  = 0;
    int         nt   = 0;
    int         i, l;
    struct token {
        const char* start;
        int         len;
    };
    struct token tokens[9];

    memset(tokens, 0, sizeof(tokens));

    // v1 version of subject is total of 9 tokens:
    //
    // $JS.ACK.<stream name>.<consumer name>.<num delivered>.<stream sequence>.<consumer sequence>.<timestamp>.<num pending>
    //
    // Since we are here with the 2 first token stripped, the number of tokens is 7.
    //
    // v2 version of subject total tokens is 12:
    //
    // $JS.ACK.<domain>.<account hash>.<stream name>.<consumer name>.<num delivered>.<stream sequence>.<consumer sequence>.<timestamp>.<num pending>.<a token with a random value>
    //
    // Again, since "$JS.ACK." has already been stripped, this is 10 tokens.
    // However, the library does not care about anything after the num pending,
    // so it would be 9 tokens.

    // Find tokens but stop when we have at most 9 tokens.
    while ((nt < 9) && ((np = strchr(p, '.')) != NULL))
    {
        tokens[nt].start = p;
        tokens[nt].len   = (int) (np - p);
        nt++;
        p = (const char*) (np+1);
    }
    if (np == NULL)
    {
        tokens[nt].start = p;
        tokens[nt].len   = (int) (strlen(p));
        nt++;
    }

    // It is invalid if less than 7 or if it has more than 7, it has to have
    // at least 9 to be valid.
    if ((nt < 7) || ((nt > 7) && (nt < 9)))
        return NATS_ERR;

    // If it is 7 tokens (the v1), then insert 2 empty tokens at the beginning.
    if (nt == 7)
    {
        memmove(&(tokens[2]), &(tokens[0]), nt*sizeof(struct token));
        tokens[0].start = NULL;
        tokens[0].len = 0;
        tokens[1].start = NULL;
        tokens[1].len = 0;
        // We work with knowledge that we have now 9 tokens.
        nt = 9;
    }

    for (i=0; i<nt; i++)
    {
        str = tokens[i].start;
        l   = tokens[i].len;
        // For numeric tokens, anything after the consumer name token,
        // which is index 3 (starting at 0).
        if (i > 3)
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
                if (domain != NULL)
                {
                    // A domain "_" will be sent by new server to indicate
                    // that there is no domain, but to make the number of tokens
                    // constant.
                    if ((str == NULL) || ((l == 1) && (str[0] == '_')))
                        *domain = NULL;
                    else if ((s = _copyString(domain, str, l)) != NATS_OK)
                        return NATS_UPDATE_ERR_STACK(s);
                    done++;
                }
                break;
            case 1:
                // acc hash, ignore.
                break;
            case 2:
                if (stream != NULL)
                {
                    if ((s = _copyString(stream, str, l)) != NATS_OK)
                        return NATS_UPDATE_ERR_STACK(s);
                    done++;
                }
                break;
            case 3:
                if (consumer != NULL)
                {
                    if ((s = _copyString(consumer, str, l)) != NATS_OK)
                        return NATS_UPDATE_ERR_STACK(s);
                    done++;
                }
                break;
            case 4:
                if (numDelivered != NULL)
                {
                    *numDelivered = (uint64_t) val;
                    done++;
                }
                break;
            case 5:
                if (sseq != NULL)
                {
                    *sseq = (uint64_t) val;
                    done++;
                }
                break;
            case 6:
                if (dseq != NULL)
                {
                    *dseq = (uint64_t) val;
                    done++;
                }
                break;
            case 7:
                if (tm != NULL)
                {
                    *tm = val;
                    done++;
                }
                break;
            case 8:
                if (numPending != NULL)
                {
                    *numPending = (uint64_t) val;
                    done++;
                }
                break;
        }
        if (done == asked)
            return NATS_OK;
    }
    return NATS_OK;
}

natsStatus
jsSub_trackSequences(jsSub *jsi, const char *reply)
{
    natsStatus  s = NATS_OK;

    // Data is equivalent to HB, so consider active.
    jsi->active = true;

    if ((reply == NULL) || (strstr(reply, jsAckPrefix) != reply))
        return NATS_OK;

    // Keep track of inbound message "sequence" for flow control purposes.
    jsi->fciseq++;

    NATS_FREE(jsi->cmeta);
    DUP_STRING(s, jsi->cmeta, reply+jsAckPrefixLen);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsSub_processSequenceMismatch(natsSubscription *sub, natsMsg *msg, bool *sm)
{
    jsSub       *jsi   = sub->jsi;
    const char  *str   = NULL;
    int64_t     val    = 0;
    struct mismatch *m = &jsi->mismatch;
    natsStatus  s;

    *sm = false;

    // This is an HB, so update that we are active.
    jsi->active = true;

    if (jsi->cmeta == NULL)
        return NATS_OK;

    s = js_getMetaData(jsi->cmeta, NULL, NULL, NULL, NULL, &m->sseq, &m->dseq, NULL, NULL, 2);
    if (s != NATS_OK)
    {
        if (s == NATS_ERR)
            return nats_setError(NATS_ERR, "invalid JS ACK: '%s'", jsi->cmeta);
        return NATS_UPDATE_ERR_STACK(s);
    }

    s = natsMsgHeader_Get(msg, jsLastConsumerSeqHdr, &str);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (str != NULL)
    {
        // Now that we have the field, we parse it. This function returns
        // -1 if there is a parsing error.
        val = nats_ParseInt64(str, (int) strlen(str));
        if (val == -1)
            return nats_setError(NATS_ERR, "invalid last consumer sequence: '%s'", str);

        m->ldseq = (uint64_t) val;
    }
    if (m->ldseq == m->dseq)
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
    else
    {
        if (jsi->ordered)
        {
            s = jsSub_resetOrderedConsumer(sub, jsi->sseq+1);
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
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_GetConsumerInfo(jsConsumerInfo **ci, natsSubscription *sub,
                                 jsOptions *opts, jsErrCode *errCode)
{
    char        *consumer = NULL;
    const char  *stream   = NULL;
    jsCtx       *js       = NULL;
    natsStatus  s         = NATS_OK;

    if ((ci == NULL) || (sub == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsSub_Lock(sub);
    if ((sub->jsi == NULL) || (sub->jsi->consumer == NULL))
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_INVALID_SUBSCRIPTION);
    }
    js = sub->jsi->js;
    stream = (const char*) sub->jsi->stream;
    DUP_STRING(s, consumer, sub->jsi->consumer);
    if (s == NATS_OK)
        sub->refs++;
    natsSub_Unlock(sub);

    if (s == NATS_OK)
    {
        s = js_GetConsumerInfo(ci, js, stream, consumer, opts, errCode);
        NATS_FREE(consumer);
        natsSub_release(sub);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_GetSequenceMismatch(jsConsumerSequenceMismatch *csm, natsSubscription *sub)
{
    jsSub *jsi;
    struct mismatch *m = NULL;

    if ((csm == NULL) || (sub == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    nats_lockSubAndDispatcher(sub);
    if (sub->jsi == NULL)
    {
        nats_unlockSubAndDispatcher(sub);
        return nats_setError(NATS_INVALID_SUBSCRIPTION, "%s", jsErrNotAJetStreamSubscription);
    }
    jsi = sub->jsi;
    m = &jsi->mismatch;
    if (m->dseq == m->ldseq)
    {
        nats_unlockSubAndDispatcher(sub);
        return NATS_NOT_FOUND;
    }
    memset(csm, 0, sizeof(jsConsumerSequenceMismatch));
    csm->Stream = m->sseq;
    csm->ConsumerClient = m->dseq;
    csm->ConsumerServer = m->ldseq;
    nats_unlockSubAndDispatcher(sub);
    return NATS_OK;
}

char*
jsSub_checkForFlowControlResponse(natsSubscription *sub)
{
    jsSub *jsi     = sub->jsi;
    char  *fcReply = NULL;

    jsi->active = true;
    if (sub->delivered >= jsi->fcDelivered)
    {
        fcReply = jsi->fcReply;
        jsi->fcReply = NULL;
        jsi->fcDelivered = 0;
    }

    return fcReply;
}

natsStatus
jsSub_scheduleFlowControlResponse(jsSub *jsi, const char *reply)
{
    NATS_FREE(jsi->fcReply);
    jsi->fcReply = NATS_STRDUP(reply);
    if (jsi->fcReply == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    jsi->fcDelivered = jsi->fciseq;

    return NATS_OK;
}

// returns the fetchID from subj, or -1 if not the fetch status subject for the
// sub.
static inline int64_t
_fetchIDFromSubject(natsSubscription *sub, const char *subj)
{
    int     len = NATS_DEFAULT_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1; // {INBOX}. but without the *
    int64_t id  = 0;

    if (strncmp(sub->subject, subj, len) != 0)
        return -1;

    subj += len;
    if (*subj == '\0')
        return -1;

    for (; *subj != '\0'; subj++)
    {
        if ((*subj < '0') || (*subj > '9'))
            return -1;
        id = (id * 10) + (*subj - '0');
    }
    return id;
}

// returns the fetch status OK to continue, or an error to stop. Some errors
// like NATS_TIMEOUT are valid exit codes.
natsStatus
js_checkFetchedMsg(natsSubscription *sub, natsMsg *msg, uint64_t fetchID, bool checkSts, bool *usrMsg)
{
    natsStatus  s       = NATS_OK;
    const char  *val    = NULL;
    const char  *desc   = NULL;

    // Check for synthetic fetch event messages.
    if (sub->control != NULL)
    {
        if (msg == sub->control->fetch.missedHeartbeat)
        {
            *usrMsg = false;
            return NATS_MISSED_HEARTBEAT;
        }
        else if (msg == sub->control->fetch.expired)
        {
            *usrMsg = false;
            return NATS_TIMEOUT;
        }
    }

    if ((msg->dataLen > 0) || (msg->hdrLen <= 0))
    {
        // If we have data, or no header - user's
        *usrMsg = true;
        return NATS_OK;
    }

    s = natsMsgHeader_Get(msg, STATUS_HDR, &val);
    if (s == NATS_NOT_FOUND)
    {
        // If no status header, this is still considered a user message, so OK.
        *usrMsg = true;
        return NATS_OK;
    }
    else if (s != NATS_OK)
    {
        // If serious error, return it.
        *usrMsg = false;
        return NATS_UPDATE_ERR_STACK(s);
    }

    // At this point, this is known to be a status message, not a user message,
    // even if we don't recognize the status here.
    *usrMsg = false;

    // If we don't care about status, we are done.
    if (!checkSts)
        return NATS_OK;

    // 100 Idle hearbeat, return OK
    if (strncmp(val, HDR_STATUS_CTRL_100, HDR_STATUS_LEN) == 0)
        return NATS_OK;

    // Before checking for "errors", if the incoming status message is
    // for a previous request (message's subject is not reqSubj), then
    // simply return NATS_OK. The caller will destroy the message and
    // proceed as if nothing was received.
    int64_t id = _fetchIDFromSubject(sub, natsMsg_GetSubject(msg));
    if (id != (int64_t) fetchID)
        return NATS_OK;

    // 404 indicating that there are no messages.
    if (strncmp(val, HDR_STATUS_NOT_FOUND_404, HDR_STATUS_LEN) == 0)
        return NATS_NOT_FOUND;

    // 408 indicating request timeout
    if (strncmp(val, HDR_STATUS_TIMEOUT_408, HDR_STATUS_LEN) == 0)
        return NATS_TIMEOUT;

    // 409 indicating that MaxBytes has been reached, but it can come as other
    // errors (e.g. "Exceeded MaxWaiting"), so set the last error.
    if (strncmp(val, HDR_STATUS_MAX_BYTES_409, HDR_STATUS_LEN) == 0)
    {
        natsMsgHeader_Get(msg, DESCRIPTION_HDR, &desc);
        return nats_setError(NATS_LIMIT_REACHED, "%s", (desc == NULL ? "error checking pull subscribe message" : desc));
    }

    // The possible 503 is handled directly in natsSub_nextMsg(), so we would
    // never get it here in this function, but in PullSubscribeAsync. There, we
    // want to use it as the exit code (not NATS_ERR).
    if (strncmp(val, HDR_STATUS_NO_RESP_503, HDR_STATUS_LEN) == 0)
        return NATS_NO_RESPONDERS;

    natsMsgHeader_Get(msg, DESCRIPTION_HDR, &desc);
    return nats_setError(NATS_ERR, "%s", (desc == NULL ? "error checking pull subscribe message" : desc));
}

static natsStatus
_sendPullRequest(natsConnection *nc, const char *subj, const char *rply,
                 natsBuffer *buf, jsFetchRequest *req)
{
    natsStatus  s;
    int64_t     expires;

    // Make our request expiration a bit shorter than user provided expiration.
    expires = (req->Expires >= (int64_t) 20E6 ? req->Expires - (int64_t) 10E6 : req->Expires);

    natsBuf_Reset(buf);
    s = natsBuf_AppendByte(buf, '{');
    // Currently, Batch is required, so will always be > 0
    IFOK(s, nats_marshalLong(buf, false, "batch", (int64_t) req->Batch));
    if ((s == NATS_OK) && (req->MaxBytes > 0))
        s = nats_marshalLong(buf, true, "max_bytes", req->MaxBytes);
    if ((s == NATS_OK) && (expires > 0))
        s = nats_marshalLong(buf, true, "expires", expires);
    if ((s == NATS_OK) && (req->Heartbeat > 0))
        s = nats_marshalLong(buf, true, "idle_heartbeat", req->Heartbeat);
    if ((s == NATS_OK) && req->NoWait)
        s = natsBuf_Append(buf, ",\"no_wait\":true", -1);
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    // Sent the request to get more messages.
    IFOK(s, natsConnection_PublishRequest(nc, subj, rply,
        natsBuf_Data(buf), natsBuf_Len(buf)));

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
_fetch(natsMsgList *list, natsSubscription *sub, jsFetchRequest *req, bool simpleFetch)
{
    natsStatus      s       = NATS_OK;
    natsMsg         **msgs  = NULL;
    int             count   = 0;
    int             batch   = 0;
    natsConnection  *nc     = NULL;
    const char      *subj   = NULL;
    char            rply[NATS_DEFAULT_INBOX_PRE_LEN + NUID_BUFFER_LEN + 32];
    int             pmc = 0;
    char            buffer[64];
    natsBuffer      buf;
    int64_t         start    = 0;
    int64_t         deadline = 0;
    int64_t         timeout  = 0;
    int             size     = 0;
    bool            sendReq  = true;
    jsSub           *jsi     = NULL;
    bool            noWait   = false;
    uint64_t        fetchID  = 0;

    if (list == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(list, 0, sizeof(natsMsgList));

    if ((sub == NULL) || (req == NULL) || (req->Batch <= 0) || (req->MaxBytes < 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (!req->NoWait && req->Expires <= 0)
        return nats_setDefaultError(NATS_INVALID_TIMEOUT);

    natsSub_Lock(sub);
    jsi = sub->jsi;
    if ((jsi == NULL) || !jsi->pull)
    {
        natsSub_Unlock(sub);
        return nats_setError(NATS_INVALID_SUBSCRIPTION, "%s", jsErrNotAPullSubscription);
    }
    if (jsi->inFetch)
    {
        natsSub_Unlock(sub);
        return nats_setError(NATS_ERR, "%s", jsErrConcurrentFetchNotAllowed);
    }
    msgs = (natsMsg**) NATS_CALLOC(req->Batch, sizeof(natsMsg*));
    if (msgs == NULL)
    {
        natsSub_Unlock(sub);
        return nats_setDefaultError(NATS_NO_MEMORY);
    }
    natsBuf_InitWithBackend(&buf, buffer, 0, sizeof(buffer));
    nc   = sub->conn;
    subj = jsi->nxtMsgSubj;
    pmc  = (sub->ownDispatcher.queue.msgs > 0);
    jsi->inFetch = true;
    jsi->fetchID++;
    fetchID = jsi->fetchID;
    snprintf(rply, sizeof(rply), "%.*s%" PRIu64, (int)strlen(sub->subject) - 1, sub->subject, fetchID);
    if ((s == NATS_OK) && req->Heartbeat)
    {
        s = nats_createControlMessages(sub);
        if (s == NATS_OK)
        {
            int64_t hbi = req->Heartbeat / 1000000;
            sub->refs++;
            if (jsi->hbTimer == NULL)
            {
                s = natsTimer_Create(&jsi->hbTimer, _hbTimerFired, _releaseSubWhenStopped, hbi * 2, (void *)sub);
                if (s != NATS_OK)
                    sub->refs--;
            }
            else
                natsTimer_Reset(jsi->hbTimer, hbi);
        }
    }
    natsSub_Unlock(sub);

    if (req->Expires > 0)
    {
        start = nats_Now();
        timeout = req->Expires / (int64_t) 1E6;
        deadline = start + timeout;
    }

    // First, if there are already pending messages in the internal sub,
    // then get as much messages as we can (but not more than the batch).
    while (pmc && (s == NATS_OK) && (count < req->Batch) && ((req->MaxBytes == 0) || (size < req->MaxBytes)))
    {
        natsMsg *msg  = NULL;
        bool    usrMsg= false;

        // This call will pull messages from the internal sync subscription
        // but will not wait (and return NATS_TIMEOUT without updating
        // the error stack) if there are no messages.
        s = natsSub_nextMsg(&msg, sub, 0, true);
        if (s == NATS_TIMEOUT)
        {
            s = NATS_OK;
            break;
        }
        if (s == NATS_OK)
        {
            // Here we care only about user messages. We don't need to pass
            // the request subject since it is not even checked in this case.
            s = js_checkFetchedMsg(sub, msg, fetchID, false, &usrMsg);
            if ((s == NATS_OK) && usrMsg)
            {
                msgs[count++] = msg;
                size += msg->wsz;
            }
            else
                natsMsg_Destroy(msg);
        }
    }
    if (s == NATS_OK)
    {
        // If we come from natsSubscription_Fetch() (simpleFetch is true), then
        // we decide on the NoWait value.
        if (simpleFetch)
            noWait = (req->Batch - count > 1 ? true : false);
        else
            noWait = req->NoWait;
    }

    batch = req->Batch;
    // If we have OK and not all messages, we will send a fetch
    // request to the server.
    while ((s == NATS_OK) && (count != batch) && ((req->MaxBytes == 0) || (size < req->MaxBytes)))
    {
        natsMsg *msg    = NULL;
        bool    usrMsg  = false;

        if (req->Expires > 0)
        {
            timeout = deadline - nats_Now();
            if (timeout <= 0)
                s = NATS_TIMEOUT;
        }

        if ((s == NATS_OK) && sendReq)
        {
            sendReq = false;
            req->Batch = req->Batch - count;
            req->Expires = NATS_MILLIS_TO_NANOS(timeout);
            req->NoWait = noWait;
            s = _sendPullRequest(nc, subj, rply, &buf, req);
        }
        IFOK(s, natsSub_nextMsg(&msg, sub, timeout, true));
        if (s == NATS_OK)
        {
            s = js_checkFetchedMsg(sub, msg, fetchID, true, &usrMsg);
            if ((s == NATS_OK) && usrMsg)
            {
                msgs[count++] = msg;
                size += msg->wsz;
            }
            else
            {
                natsMsg_Destroy(msg);
                // If we come from "simpleFetch" and we have a 404 for
                // the noWait request and have not collected any message,
                // then resend the request and ask to wait this time.
                if (simpleFetch && noWait && (s == NATS_NOT_FOUND) && (count == 0))
                {
                    s        = NATS_OK;
                    noWait   = false;
                    sendReq  = true;
                }
            }
        }
    }

    natsBuf_Cleanup(&buf);

    // If count > 0 it means that we have gathered some user messages,
    // so we need to return them to the user with a NATS_OK status.
    if (count > 0)
    {
        // If there was an error, we need to clear the error stack,
        // since we return NATS_OK.
        if (s != NATS_OK)
        {
            nats_clearLastError();
            s = NATS_OK;
        }

        // Update the list with what we have collected.
        list->Msgs = msgs;
        list->Count = count;
    }

    if (s != NATS_OK)
        NATS_FREE(msgs);

    natsSub_Lock(sub);
    jsi->inFetch = false;
    if (req->Heartbeat && (jsi->hbTimer != NULL))
        natsTimer_Stop(jsi->hbTimer);
    natsSub_Unlock(sub);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsFetchRequest_Init(jsFetchRequest *request)
{
    if (request == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(request, 0, sizeof(jsFetchRequest));
    return NATS_OK;
}

natsStatus
natsSubscription_Fetch(natsMsgList *list, natsSubscription *sub, int batch, int64_t timeout,
                       jsErrCode *errCode)
{
    natsStatus      s;
    jsFetchRequest  req;

    if (errCode != NULL)
        *errCode = 0;

    jsFetchRequest_Init(&req);
    req.Batch = batch;
    req.Expires = NATS_MILLIS_TO_NANOS(timeout);
    s = _fetch(list, sub, &req, true);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsSubscription_FetchRequest(natsMsgList *list, natsSubscription *sub, jsFetchRequest *req)
{
    natsStatus      s;

    s = _fetch(list, sub, req, false);
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_fetchExpiredFired(natsTimer *timer, void *closure)
{
    natsSubscription *sub = (natsSubscription *)closure;

    // Let the dispatcher know that the fetch has expired. It will deliver all
    // queued up messages, then do the right termination.
    nats_lockSubAndDispatcher(sub);
    natsSub_enqueueMessage(sub, sub->control->fetch.expired);
    nats_unlockSubAndDispatcher(sub);
    natsTimer_Stop(timer);
}

static void
_hbTimerFired(natsTimer *timer, void* closure)
{
    natsSubscription    *sub = (natsSubscription*) closure;
    jsSub               *jsi = sub->jsi;
    bool                alert= false;
    natsConnection      *nc  = NULL;
    bool                oc   = false;
    natsStatus          s    = NATS_OK;

    nats_lockSubAndDispatcher(sub);
    alert = !jsi->active;
    oc = jsi->ordered;
    jsi->active = false;
    if (alert && jsi->pull)
    {
        // If there are messages pending then we can't really consider
        // that we missed hearbeats. Wait for those to be processed and
        // we will check missed HBs again.
        if (sub->ownDispatcher.queue.msgs == 0)
        {
            natsSub_enqueueMessage(sub, sub->control->fetch.missedHeartbeat);
            natsTimer_Stop(timer);
        }
        nats_unlockSubAndDispatcher(sub);
        return;
    }
    nc = sub->conn;
    nats_unlockSubAndDispatcher(sub);

    if (!alert)
        return;

    // For ordered consumers, we will need to reset
    if (oc)
    {
        nats_lockSubAndDispatcher(sub);
        if (!sub->closed)
        {
            // If we fail in that call, we will report to async err callback
            // (if one is specified).
            s = jsSub_resetOrderedConsumer(sub, sub->jsi->sseq+1);
        }
        nats_unlockSubAndDispatcher(sub);
    }

    natsConn_Lock(nc);
    // Even if we have called resetOrderedConsumer, we will post something
    // to the async error callback, either "missed heartbeats", or the error
    // that occurred trying to do the reset.
    if (s == NATS_OK)
        s = NATS_MISSED_HEARTBEAT;
    natsAsyncCb_PostErrHandler(nc, sub, s, NULL);
    natsConn_Unlock(nc);
}

// This is invoked when the subscription is destroyed, since in NATS C
// client, timers will automatically fire again, so this callback is
// invoked when the timer has been stopped (and we are ready to destroy it).
static void
_releaseSubWhenStopped(natsTimer *timer, void* closure)
{
    natsSubscription *sub = (natsSubscription*) closure;

    natsSub_release(sub);
}

static bool
_stringPropertyDiffer(const char *user, const char *server)
{
    if (nats_IsStringEmpty(user))
        return false;

    if (nats_IsStringEmpty(server))
        return true;

    return (strcmp(user, server) != 0 ? true : false);
}

#define CFG_CHECK_ERR_START "configuration requests %s to be "
#define CFG_CHECK_ERR_END   ", but consumer's value is "

static natsStatus
_checkConfig(jsConsumerConfig *s, jsConsumerConfig *u)
{
    if (_stringPropertyDiffer(u->Durable, s->Durable))
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "'%s'" CFG_CHECK_ERR_END "'%s'", "durable", u->Durable, s->Durable);

    if (_stringPropertyDiffer(u->Description, s->Description))
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "'%s'" CFG_CHECK_ERR_END "'%s'", "description", u->Description, s->Description);

    if ((int) u->DeliverPolicy >= 0 && u->DeliverPolicy != s->DeliverPolicy)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "deliver policy", u->DeliverPolicy, s->DeliverPolicy);

    if (u->OptStartSeq > 0 && u->OptStartSeq != s->OptStartSeq)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRIu64 CFG_CHECK_ERR_END "%" PRIu64, "optional start sequence", u->OptStartSeq, s->OptStartSeq);

    if (u->OptStartTime > 0 && u->OptStartTime != s->OptStartTime)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "optional start time", u->OptStartTime, s->OptStartTime);

    if ((int) u->AckPolicy >= 0 && u->AckPolicy != s->AckPolicy)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "ack policy", u->AckPolicy, s->AckPolicy);

    if (u->AckWait > 0 && u->AckWait != s->AckWait)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "ack wait", u->AckWait, s->AckWait);

    if (u->MaxDeliver > 0 && u->MaxDeliver != s->MaxDeliver)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "max deliver", u->MaxDeliver, s->MaxDeliver);

    if (u->BackOffLen > 0 && u->BackOffLen != s->BackOffLen)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "backoff list length", u->BackOffLen, s->BackOffLen);

    if ((int) u->ReplayPolicy >= 0 && u->ReplayPolicy != s->ReplayPolicy)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "replay policy", u->ReplayPolicy, s->ReplayPolicy);

    if (u->RateLimit > 0 && u->RateLimit != s->RateLimit)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRIu64 CFG_CHECK_ERR_END "%" PRIu64, "rate limit", u->RateLimit, s->RateLimit);

    if (_stringPropertyDiffer(u->SampleFrequency, s->SampleFrequency))
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "'%s'" CFG_CHECK_ERR_END "'%s'", "sample frequency", u->SampleFrequency, s->SampleFrequency);

    if (u->MaxWaiting > 0 && u->MaxWaiting != s->MaxWaiting)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "max waiting", u->MaxWaiting, s->MaxWaiting);

    if (u->MaxAckPending > 0 && u->MaxAckPending != s->MaxAckPending)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "max ack pending", u->MaxAckPending, s->MaxAckPending);

    // For flow control, we want to fail if the user explicit wanted it, but
    // it is not set in the existing consumer. If it is not asked by the user,
    // the library still handles it and so no reason to fail.
    if (u->FlowControl && !s->FlowControl)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "'%s'" CFG_CHECK_ERR_END "'%s'", "flow control", "true", "false");

    if (u->Heartbeat > 0 && u->Heartbeat != s->Heartbeat)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "heartbeat", u->Heartbeat, s->Heartbeat);

    if (u->HeadersOnly != s->HeadersOnly)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "headers only", u->HeadersOnly, s->HeadersOnly);

    if (u->MaxRequestBatch > 0 && u->MaxRequestBatch != s->MaxRequestBatch)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "max request batch", u->Heartbeat, s->Heartbeat);

    if (u->MaxRequestExpires > 0 && u->MaxRequestExpires != s->MaxRequestExpires)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "max request expires", u->MaxRequestExpires, s->MaxRequestExpires);

    if (u->InactiveThreshold > 0 && u->InactiveThreshold != s->InactiveThreshold)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "inactive threshold", u->InactiveThreshold, s->InactiveThreshold);

    if (u->Replicas > 0 && u->Replicas != s->Replicas)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%" PRId64 CFG_CHECK_ERR_END "%" PRId64, "replicas", u->Replicas, s->Replicas);

    if (u->MemoryStorage != s->MemoryStorage)
        return nats_setError(NATS_ERR, CFG_CHECK_ERR_START "%d" CFG_CHECK_ERR_END "%d", "memory storage", u->MemoryStorage, s->MemoryStorage);

    return NATS_OK;
}

static natsStatus
_processConsInfo(const char **dlvSubject, jsConsumerInfo *info, jsConsumerConfig *userCfg,
                 bool isPullMode, const char **subjects, int numSubjects, const char *queue)
{
    bool dlvSubjEmpty = false;
    jsConsumerConfig *ccfg = info->Config;
    const char *dg = NULL;
    natsStatus s = NATS_OK;
    const char *stackFilterSubject[] = {ccfg->FilterSubject};
    const char **filterSubjects = stackFilterSubject;
    int filterSubjectsLen = 1;
    int incoming, existing;

    *dlvSubject = NULL;
    // Always represent the consumer's filter subjects as a list, to match
    // uniformly against the incoming subject list. Consider lists of 1 empty
    // subject empty lists.
    if (ccfg->FilterSubjectsLen > 0)
    {
        filterSubjects = ccfg->FilterSubjects;
        filterSubjectsLen = ccfg->FilterSubjectsLen;
    }
    if ((filterSubjectsLen == 1) && nats_IsStringEmpty(filterSubjects[0]))
    {
        filterSubjects = NULL;
        filterSubjectsLen = 0;
    }
    if ((numSubjects == 1) && nats_IsStringEmpty(subjects[0]))
    {
        subjects = NULL;
        numSubjects = 0;
    }

    // Match the subjects against the consumer's filter subjects.
    if (numSubjects > 0 && filterSubjectsLen > 0)
    {
        // If the consumer has filter subject(s), then the subject(s) must match.
        bool matches = true;

        // TODO - This is N**2, but we don't expect a large number of subjects.
        for (incoming = 0; incoming < numSubjects; incoming++)
        {
            bool found = false;
            for (existing = 0; existing < filterSubjectsLen; existing++)
            {
                if (strcmp(subjects[incoming], filterSubjects[existing]) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                matches = false;
                break;
            }
        }

        if (!matches)
        {
            if (numSubjects == 1 && filterSubjectsLen == 1)
                return nats_setError(NATS_ERR, "subject '%s' does not match consumer filter subject '%s'.", subjects[0], filterSubjects[0]);
            else
                return nats_setError(NATS_ERR, "%d subjects do not match any consumer filter subjects.", numSubjects);
        }
    }
    // Check that if user wants to create a queue sub,
    // the consumer has no HB nor FC.
    queue = (nats_IsStringEmpty(queue) ? NULL : queue);

    if (queue != NULL)
    {
        if (ccfg->Heartbeat > 0)
            return nats_setError(NATS_ERR, "%s", jsErrNoHeartbeatForQueueSub);

        if (ccfg->FlowControl)
            return nats_setError(NATS_ERR, "%s", jsErrNoFlowControlForQueueSub);
    }

    dlvSubjEmpty = nats_IsStringEmpty(ccfg->DeliverSubject);

    // Prevent binding a subscription against incompatible consumer types.
    if (isPullMode && !dlvSubjEmpty)
    {
        return nats_setError(NATS_ERR, "%s", jsErrPullSubscribeToPushConsumer);
    }
    else if (!isPullMode && dlvSubjEmpty)
    {
        return nats_setError(NATS_ERR, "%s", jsErrPullSubscribeRequired);
    }

    // If pull mode, nothing else to check here.
    if (isPullMode)
    {
        s = _checkConfig(ccfg, userCfg);
        return NATS_UPDATE_ERR_STACK(s);
    }

    // At this point, we know the user wants push mode, and the JS consumer is
    // really push mode.
    dg = ccfg->DeliverGroup;

    if (nats_IsStringEmpty(dg))
    {
        // Prevent an user from attempting to create a queue subscription on
        // a JS consumer that was not created with a deliver group.
        if (queue != NULL)
        {
            return nats_setError(NATS_ERR, "%s",
                                 "cannot create a queue subscription for a consumer without a deliver group");
        }
        else if (info->PushBound)
        {
            // Need to reject a non queue subscription to a non queue consumer
            // if the consumer is already bound.
            return nats_setError(NATS_ERR, "%s", "consumer is already bound to a subscription");
        }
    }
    else
    {
        // If the JS consumer has a deliver group, we need to fail a non queue
        // subscription attempt:
        if (queue == NULL)
        {
            return nats_setError(NATS_ERR,
                                "cannot create a subscription for a consumer with a deliver group %s",
                                dg);
        }
        else if (strcmp(queue, dg) != 0)
        {
            // Here the user's queue group name does not match the one associated
            // with the JS consumer.
            return nats_setError(NATS_ERR,
                                 "cannot create a queue subscription '%s' for a consumer with a deliver group '%s'",
                                 queue, dg);
        }
    }
    s = _checkConfig(ccfg, userCfg);
    if (s == NATS_OK)
        *dlvSubject = ccfg->DeliverSubject;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_checkConsName(const char *cons, bool isDurable)
{
    int i;

    if (nats_IsStringEmpty(cons))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrConsumerNameRequired);

    for (i=0; i<(int)strlen(cons); i++)
    {
        char c = cons[i];
        if ((c == '.') || (c == ' ') || (c == '*') || (c == '>'))
        {
            return nats_setError(NATS_INVALID_ARG, "%s '%s' (cannot contain '%c')",
                (isDurable ? jsErrInvalidDurableName : jsErrInvalidConsumerName), cons, c);
        }
    }
    return NATS_OK;
}

static natsStatus
_subscribeMulti(natsSubscription **new_sub, jsCtx *js, const char **subjects, int numSubjects, const char *pullDurable,
           natsMsgHandler usrCB, void *usrCBClosure, bool isPullMode,
           jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus          s           = NATS_OK;
    const char          *stream     = NULL;
    const char          *consumer   = NULL;
    const char          *durable    = NULL;
    const char          *deliver    = NULL;
    jsErrCode           jerr        = 0;
    jsConsumerInfo      *info       = NULL;
    bool                lookupErr   = false;
    bool                consBound   = false;
    bool                isQueue     = false;
    natsConnection      *nc         = NULL;
    bool                freePfx     = false;
    bool                freeStream  = false;
    jsSub               *jsi        = NULL;
    int64_t             hbi         = 0;
    bool                create      = false;
    natsSubscription    *sub        = NULL;
    natsMsgHandler      cb          = NULL;
    void                *cbClosure  = NULL;
    natsInbox           *inbox      = NULL;
    int64_t             maxap       = 0;
    jsOptions           jo;
    jsSubOptions        o;
    jsConsumerConfig    cfgStack;
    jsConsumerConfig    *cfg = NULL;
    jsConsumerConfig    *ocCfg = NULL;

    if ((new_sub == NULL) || (js == NULL))
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
    if (opts->Config.InactiveThreshold < 0)
        return nats_setError(NATS_INVALID_ARG,
                             "invalid InactiveThreshold value (%d), needs to be greater or equal to 0",
                             (int) opts->Config.InactiveThreshold);

    // If user configures optional start sequence or time, the deliver policy
    // need to be updated accordingly. Server will return error if user tries to have both set.
    if (opts->Config.OptStartSeq > 0)
        opts->Config.DeliverPolicy = js_DeliverByStartSequence;
    if (opts->Config.OptStartTime > 0)
        opts->Config.DeliverPolicy = js_DeliverByStartTime;

    isQueue  = !nats_IsStringEmpty(opts->Queue);
    stream   = opts->Stream;
    durable  = (pullDurable != NULL ? pullDurable : opts->Config.Durable);
    consumer = opts->Consumer;
    consBound= (!nats_IsStringEmpty(stream) && !nats_IsStringEmpty(consumer));

    if (((numSubjects <= 0) || nats_IsStringEmpty(subjects[0])) && !consBound)
        return nats_setDefaultError(NATS_INVALID_ARG);

    // Do some quick checks here for ordered consumers.
    if (opts->Ordered)
    {
        // Check for pull mode.
        if (isPullMode)
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoPullMode);
        // Make sure we are not durable.
        if (!nats_IsStringEmpty(durable))
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoDurable);
        // Check ack policy.
        if ((int) opts->Config.AckPolicy != -1)
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoAckPolicy);
        // Check max deliver. If set, it has to be 1.
        if ((opts->Config.MaxDeliver > 0) && (opts->Config.MaxDeliver != 1))
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoMaxDeliver);
        // No deliver subject, we pick our own.
        if (!nats_IsStringEmpty(opts->Config.DeliverSubject))
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoDeliverSubject);
        // Queue groups not allowed.
        if (isQueue)
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoQueue);
        // Check for bound consumers.
        if (!nats_IsStringEmpty(consumer))
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrOrderedConsNoBind);
    }
    else if (isQueue)
    {
        // Reject a user configuration that would want to define hearbeats with
        // a queue subscription.
        if (opts->Config.Heartbeat > 0)
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrNoHeartbeatForQueueSub);
        // Same for flow control
        if (opts->Config.FlowControl)
            return nats_setError(NATS_INVALID_ARG, "%s", jsErrNoFlowControlForQueueSub);
        // If no durable name was provided, use the queue name as the durable.
        if (nats_IsStringEmpty(durable))
            durable = opts->Queue;
    }

    // If a durable name is specified, check that it is valid
    if (!nats_IsStringEmpty(durable))
    {
        if ((s = js_checkConsName(durable, true)) != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    // In case a consumer has not been set explicitly, then the durable name
    // will be used as the consumer name (after that, `consumer` will still be
    // possibly NULL).
    if (nats_IsStringEmpty(consumer))
        consumer = durable;

    // Find the stream mapped to the subject if not bound to a stream already,
    // that is, if user did not provide a `Stream` name through options).
    if (nats_IsStringEmpty(stream) && numSubjects > 0)
    {
        // Use the first subject to find the stream.
        s = _lookupStreamBySubject(&stream, nc, subjects[0], &jo, errCode);
        if (s != NATS_OK)
            goto END;

        freeStream = true;
    }

    // If a consumer name is specified, try to lookup the consumer and
    // if it exists, will attach to it.
    if (!nats_IsStringEmpty(consumer))
    {
        s = js_GetConsumerInfo(&info, js, stream, consumer, &jo, &jerr);
        lookupErr = (s == NATS_TIMEOUT) || (jerr == JSNotEnabledErr);
    }

PROCESS_INFO:
    if (info != NULL)
    {
        if (info->Config == NULL)
        {
            s = nats_setError(NATS_ERR, "%s", "no configuration in consumer info");
            goto END;
        }
        s = _processConsInfo(&deliver, info, &(opts->Config), isPullMode, subjects, numSubjects, opts->Queue);
        if (s != NATS_OK)
            goto END;

        // Capture the HB interval (convert in millsecond since Go duration is in nanos)
        hbi = info->Config->Heartbeat / 1000000;
        maxap = info->Config->MaxAckPending;
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
        s = NATS_OK;
        // Make a shallow copy of the provided consumer config
        // since we may have to change some fields before calling
        // AddConsumer.
        cfg = &cfgStack;
        memcpy(cfg, &(opts->Config), sizeof(jsConsumerConfig));

        if (!isPullMode)
        {
            // Attempt to create consumer if not found nor binding.
            natsConn_newInbox(nc, &inbox);
            deliver = (const char*) inbox;
            cfg->DeliverSubject = deliver;
        }

        // Do filtering always, server will clear as needed.
        if (numSubjects == 1)
        {
            cfg->FilterSubject = subjects[0];
        }
        else
        {
            cfg->FilterSubjects = subjects;
            cfg->FilterSubjectsLen = numSubjects;
        }

        if (opts->Ordered)
        {
            const char *tmp = NULL;

            cfg->FlowControl = true;
            cfg->AckPolicy   = js_AckNone;
            cfg->MaxDeliver  = 1;
            cfg->AckWait     = NATS_SECONDS_TO_NANOS(24*60*60); // Just set to something known, not utilized.
            if (opts->Config.Heartbeat <= 0)
                cfg->Heartbeat = jsOrderedHBInterval;
            cfg->MemoryStorage = true;
            cfg->Replicas      = 1;

            // Let's clone without the delivery subject because it will need
            // to be set to something new when recreating it anyway.
            tmp = cfg->DeliverSubject;
            cfg->DeliverSubject = NULL;
            s = js_cloneConsumerConfig(cfg, &ocCfg);
            cfg->DeliverSubject = tmp;
        }
        else
        {
            // Set config durable with "durable" variable, which will
            // possibly be NULL.
            cfg->Durable = durable;

            // Set DeliverGroup to queue name, possibly NULL
            cfg->DeliverGroup = opts->Queue;
        }

        // Capture the HB interval (convert in millsecond since Go duration is in nanos)
        hbi = cfg->Heartbeat / 1000000;

        create = true;
    }
    if (s == NATS_OK)
    {
        jsi = (jsSub*) NATS_CALLOC(1, sizeof(jsSub));
        if (jsi == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            if (isPullMode && !nats_IsStringEmpty(consumer))
            {
                if (nats_asprintf(&(jsi->nxtMsgSubj), jsApiRequestNextT, jo.Prefix, stream, consumer) < 0)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            IF_OK_DUP_STRING(s, jsi->stream, stream);
            IFOK(s, nats_formatStringArray(&jsi->psubj, subjects, numSubjects));
            if (s == NATS_OK)
            {
                jsi->js     = js;
                jsi->hbi    = hbi;
                jsi->pull   = isPullMode;
                jsi->ordered= opts->Ordered;
                jsi->ocCfg  = ocCfg;
                jsi->dseq   = 1;
                jsi->ackNone= (opts->Config.AckPolicy == js_AckNone || opts->Ordered);
                js_retain(js);

                if ((usrCB != NULL) && !opts->ManualAck && !jsi->ackNone)
                {
                    // Keep track of user provided CB and closure
                    jsi->usrCb          = usrCB;
                    jsi->usrCbClosure   = usrCBClosure;
                    // Use our own when creating the NATS subscription.
                    cb          = _autoAckCB;
                    cbClosure   = (void*) jsi;
                }
                else if (usrCB != NULL)
                {
                    cb        = usrCB;
                    cbClosure = usrCBClosure;
                }
            }
        }
    }
    if (s == NATS_OK)
    {
        char *pullWCInbox = NULL;

        if (isPullMode)
        {
            // Create a wildcard inbox.
            s = natsConn_newInbox(nc, &inbox);
            if (s == NATS_OK)
            {
                if (nats_asprintf(&pullWCInbox, "%s.*", (const char*) inbox) < 0)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            if (s == NATS_OK)
                deliver = (const char*) pullWCInbox;
        }
        // Create the NATS subscription on given deliver subject. Note that
        // cb/cbClosure will be NULL for sync or pull subscriptions.
        IFOK(s, natsConn_subscribeImpl(&sub, nc, true, deliver,
                                       opts->Queue, 0, cb, cbClosure, false, jsi));
        if ((s == NATS_OK) && (hbi > 0) && !isPullMode)
        {
            natsSub_Lock(sub);
            sub->refs++;
            s = natsTimer_Create(&jsi->hbTimer, _hbTimerFired, _releaseSubWhenStopped, hbi*2, (void*) sub);
            if (s != NATS_OK)
                sub->refs--;
            natsSub_Unlock(sub);
        }
        NATS_FREE(pullWCInbox);
    }
    if ((s == NATS_OK) && create)
    {
        // Multiple subscribers could compete in creating the first consumer
        // that will be shared using the same durable name. If this happens, then
        // do a lookup of the consumer info subscribe using the latest info.
        s = js_AddConsumer(&info, js, stream, cfg, &jo, &jerr);
        if (s != NATS_OK)
        {
            if ((jerr != JSConsumerExistingActiveErr) && (jerr != JSConsumerNameExistErr))
                goto END;

            jsConsumerInfo_Destroy(info);
            info = NULL;

            s = js_GetConsumerInfo(&info, js, stream, consumer, &jo, &jerr);
            if (s != NATS_OK)
                goto END;

            // We will re-create the sub/jsi, so destroy here and go back to point where
            // we process the consumer info response.
            natsSubscription_Destroy(sub);
            sub = NULL;
            jsi = NULL;
            create = false;

            goto PROCESS_INFO;
        }
        else
        {
            maxap = info->Config->MaxAckPending;
            nats_lockSubAndDispatcher(sub);
            jsi->dc = true;
            jsi->pending = info->NumPending + info->Delivered.Consumer;
            // There may be a race in the case of an ordered consumer where by this
            // time, the consumer has been recreated (jsResetOrderedConsumer). So
            // set only if jsi->consumer is NULL!
            if (jsi->consumer == NULL)
            {
                DUP_STRING(s, jsi->consumer, info->Name);
                if (s == NATS_OK)
                {
                    NATS_FREE(jsi->nxtMsgSubj);
                    jsi->nxtMsgSubj = NULL;
                    if (nats_asprintf(&(jsi->nxtMsgSubj), jsApiRequestNextT, jo.Prefix, stream, jsi->consumer) < 0)
                        s = nats_setDefaultError(NATS_NO_MEMORY);
                }
            }
            nats_unlockSubAndDispatcher(sub);
        }
    }

END:
    if (s == NATS_OK)
    {
        int64_t ml = 0;
        int64_t bl = 0;

        natsSub_Lock(sub);
        ml = (int64_t) sub->msgsLimit;
        bl = (int64_t) sub->bytesLimit;
        natsSub_Unlock(sub);
        if (maxap > ml)
        {
            ml = maxap;
            if (maxap*1024*1024 > bl)
                bl = maxap*1024*1024;

            natsSubscription_SetPendingLimits(sub, (int) ml, (int) bl);
        }
        *new_sub = sub;
    }
    else
    {
        if (sub == NULL)
            jsSub_free(jsi);
        else
            natsSubscription_Destroy(sub);

        if (errCode != NULL)
            *errCode = jerr;
    }

    // Common cleanup regardless of success or not.
    jsConsumerInfo_Destroy(info);
    if (freePfx)
        NATS_FREE((char*) jo.Prefix);
    if (freeStream)
        NATS_FREE((char*) stream);
    natsInbox_Destroy(inbox);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_subscribe(natsSubscription **new_sub, jsCtx *js, const char *subject, const char *pullDurable,
           natsMsgHandler usrCB, void *usrCBClosure, bool isPullMode,
           jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s = NATS_OK;
    const char *singleSubject[] = {subject};
    int numSubjects = 1;
    const char **subjects = singleSubject;

    if (nats_IsStringEmpty(subject))
    {
        numSubjects = 0;
        subjects = NULL;
    }

    s = _subscribeMulti(new_sub, js, subjects, numSubjects, pullDurable, usrCB, usrCBClosure, isPullMode, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_Subscribe(natsSubscription **sub, jsCtx *js, const char *subject,
             natsMsgHandler cb, void *cbClosure,
             jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    if (cb == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _subscribe(sub, js, subject, NULL, cb, cbClosure, false, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_SubscribeMulti(natsSubscription **sub, jsCtx *js, const char **subjects, int numSubjects,
                  natsMsgHandler cb, void *cbClosure,
                  jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    if (cb == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _subscribeMulti(sub, js, subjects, numSubjects, NULL, cb, cbClosure, false, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_SubscribeSync(natsSubscription **sub, jsCtx *js, const char *subject,
                 jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    s = _subscribe(sub, js, subject, NULL, NULL, NULL, false, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_SubscribeSyncMulti(natsSubscription **sub, jsCtx *js, const char **subjects, int numSubjects,
                    jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    s = _subscribeMulti(sub, js, subjects, numSubjects, NULL, NULL, NULL, false, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PullSubscribe(natsSubscription **sub, jsCtx *js, const char *subject, const char *durable,
                 jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    s = _subscribe(sub, js, subject, durable, NULL, NULL, true, jsOpts, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

// Neither sub's nor dispatcher's lock must be held.
natsStatus
js_maybeFetchMore(natsSubscription *sub, jsFetch *fetch)
{
    jsFetchRequest req = {.Expires = 0};
    if (fetch->opts.NextHandler == NULL)
        return NATS_OK;

    // Prepare the next fetch request
    if (!fetch->opts.NextHandler(&req.Batch, &req.MaxBytes, sub, fetch->opts.NextHandlerClosure))
        return NATS_OK;

    // These are not changeable by the callback, only Batch and MaxBytes can be updated.
    int64_t now = nats_Now();
    if (fetch->opts.Timeout != 0)
        req.Expires = (fetch->opts.Timeout - (now - fetch->startTimeMillis)) * 1000 * 1000; // ns, go time.Duration
    req.NoWait = fetch->opts.NoWait;
    req.Heartbeat = fetch->opts.Heartbeat * 1000 * 1000; // ns, go time.Duration

    char buffer[128];
    natsBuffer buf;
    natsBuf_InitWithBackend(&buf, buffer, 0, sizeof(buffer));

    nats_lockSubAndDispatcher(sub);

    jsSub *jsi = sub->jsi;
    jsi->inFetch = true;
    jsi->fetchID++;
    snprintf(fetch->replySubject, sizeof(fetch->replySubject), "%.*s%" PRIu64,
             (int)strlen(sub->subject) - 1, sub->subject, // exclude the last '*'
             jsi->fetchID);
    natsStatus s = _sendPullRequest(sub->conn, jsi->nxtMsgSubj, fetch->replySubject, &buf, &req);
    if (s == NATS_OK)
    {
        fetch->requestedMsgs += req.Batch;
    }

    nats_unlockSubAndDispatcher(sub);

    natsBuf_Destroy(&buf);
    return NATS_UPDATE_ERR_STACK(s);
}

// Sets Batch and MaxBytes for the next fetch request.
static bool
_autoNextFetchRequest(int *messages, int64_t *maxBytes, natsSubscription *sub, void *closure)
{
    jsFetch *fetch                  = (jsFetch *)closure;
    int     remainingUnrequested    = INT_MAX;
    int64_t remainingBytes          = 0;
    int     want                    = 0;
    bool    maybeMore               = true;

    nats_lockSubAndDispatcher(sub);

    int isAhead = fetch->requestedMsgs - fetch->deliveredMsgs;
    int wantAhead = fetch->opts.KeepAhead;
    if (isAhead > wantAhead)
        maybeMore = false;

    if (maybeMore && (fetch->opts.MaxMessages > 0))
    {
        remainingUnrequested = fetch->opts.MaxMessages - fetch->requestedMsgs;
        if (remainingUnrequested <= 0)
            maybeMore = false;
    }

    if (maybeMore && (fetch->opts.MaxBytes > 0))
    {
        remainingBytes = fetch->opts.MaxBytes - fetch->receivedBytes;
        if (remainingBytes <= 0)
            maybeMore = false;
    }

    if (maybeMore)
    {
        want = remainingUnrequested;
        if (want > fetch->opts.FetchSize)
            want = fetch->opts.FetchSize;

        maybeMore = (want > 0);
    }

    nats_unlockSubAndDispatcher(sub);

    if (!maybeMore)
        return false;

    // Since we do not allow keepAhead with MaxBytes, this is an accurate count
    // of how many more bytes we expect.
    *maxBytes = remainingBytes;
    *messages = want;
    return true;
}

natsStatus
js_PullSubscribeAsync(natsSubscription **newsub, jsCtx *js, const char *subject, const char *durable,
                      natsMsgHandler msgCB, void *msgCBClosure,
                      jsOptions *jsOpts, jsSubOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsSubscription    *sub    = NULL;
    jsSub               *jsi    = NULL;
    jsFetch             *fetch  = NULL;

    if ((newsub == NULL) || (msgCB == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if ((jsOpts != NULL) && (jsOpts->PullSubscribeAsync.KeepAhead > 0))
    {
        if (jsOpts->PullSubscribeAsync.MaxBytes > 0)
            return nats_setError(NATS_INVALID_ARG, "%s", "Can not use MaxBytes and KeepAhead together");
        if (jsOpts->PullSubscribeAsync.NoWait)
            return nats_setError(NATS_INVALID_ARG, "%s", "Can not use NoWait with KeepAhead together");
    }
    
    if (errCode != NULL)
        *errCode = 0;

    // Do a basic pull subscribe first, but with a callback so it is treated as
    // "async" and assigned to a dispatcher. Since we don't fetch anything, it
    // will not be active yet.
    s = _subscribe(&sub, js, subject, durable, msgCB, msgCBClosure, true, jsOpts, opts, errCode);
    if(s == NATS_OK)
    {
        fetch = NATS_CALLOC(1, sizeof(jsFetch));
        if (fetch == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    if (s != NATS_OK)
    {
        NATS_FREE(fetch);
        natsSubscription_Destroy(sub);
        return NATS_UPDATE_ERR_STACK(s);
    }

    // Initialize fetch parameters.
    fetch->status = NATS_OK;
    fetch->startTimeMillis = nats_Now();

    if (jsOpts != NULL)
        fetch->opts = jsOpts->PullSubscribeAsync;
    if (fetch->opts.FetchSize == 0)
        fetch->opts.FetchSize = NATS_DEFAULT_ASYNC_FETCH_SIZE;
    if (fetch->opts.NextHandler == NULL)
    {
        fetch->opts.NextHandler = _autoNextFetchRequest;
        fetch->opts.NextHandlerClosure = (void *)fetch;
    }

    nats_lockSubAndDispatcher(sub);
    jsi = sub->jsi;

    // Set up the fetch options
    jsi->fetch = fetch;
    jsi->inFetch = true;

    // Start the timers. They will live for the entire length of the
    // subscription (the missed heartbeat timer may be reset as needed).
    if (fetch->opts.Timeout > 0)
    {
        sub->refs++;
        s = natsTimer_Create(&fetch->expiresTimer, _fetchExpiredFired, _releaseSubWhenStopped,
                             fetch->opts.Timeout, (void *)sub);
        if (s != NATS_OK)
            sub->refs--;
    }

    if ((s == NATS_OK) && (fetch->opts.Heartbeat > 0))
    {
        int64_t dur = fetch->opts.Heartbeat * 2;
        sub->refs++;
        if (jsi->hbTimer == NULL)
        {
            s = natsTimer_Create(&jsi->hbTimer, _hbTimerFired, _releaseSubWhenStopped, dur, (void *)sub);
            if (s != NATS_OK)
                sub->refs--;
        }
        else
            natsTimer_Reset(jsi->hbTimer, dur);
    }

    if (s == NATS_OK)
    {
        // Send the first fetch request.
        s = js_maybeFetchMore(sub, fetch);
    }

    nats_unlockSubAndDispatcher(sub);

    if (s != NATS_OK)
    {
        natsSubscription_Destroy(sub);
        return NATS_UPDATE_ERR_STACK(s);
    }

    *newsub = sub;
    return NATS_OK;
}

typedef struct __ackOpts
{
    const char  *ackType;
    bool        inProgress;
    bool        sync;
    int64_t     nakDelay;

} _ackOpts;

static natsStatus
_ackMsg(natsMsg *msg, jsOptions *opts, _ackOpts *o, jsErrCode *errCode)
{
    natsSubscription    *sub = NULL;
    natsConnection      *nc  = NULL;
    jsCtx               *js  = NULL;
    jsSub               *jsi = NULL;
    natsStatus          s    = NATS_OK;
    const char          *body= o->ackType;
    bool                sync = o->sync;
    int64_t             wait = 0;
    char                tmp[64];

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

    // If option with Wait is specified, transform all Acks as sync operation.
    if ((opts != NULL) && (opts->Wait > 0))
    {
        wait = opts->Wait;
        sync = true;
    }
    if (o->nakDelay > 0)
    {
        int64_t v = NATS_MILLIS_TO_NANOS(o->nakDelay);
        snprintf(tmp, sizeof(tmp), "%s {\"delay\":%" PRId64 "}", o->ackType, v);
        body = (const char*) tmp;
    }
    if (sync)
    {
        natsMsg *rply   = NULL;

        if (wait == 0)
        {
            // When getting a context, if user did not specify a wait,
            // we default to jsDefaultRequestWait, so this won't be 0.
            js_lock(js);
            wait = js->opts.Wait;
            js_unlock(js);
        }
        IFOK_JSR(s, natsConnection_RequestString(&rply, nc, msg->reply, body, wait));
        natsMsg_Destroy(rply);
    }
    else
    {
        s = natsConnection_PublishString(nc, msg->reply, body);
    }
    // Indicate that we have ack'ed the message
    if ((s == NATS_OK) && !o->inProgress)
        natsMsg_setAcked(msg);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_Ack(natsMsg *msg, jsOptions *opts)
{
    natsStatus  s;
    _ackOpts    o = {jsAckAck, false, false, 0};

    s = _ackMsg(msg, opts, &o, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_AckSync(natsMsg *msg, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus  s;
    _ackOpts    o = {jsAckAck, false, true, 0};

    s = _ackMsg(msg, opts, &o, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_Nak(natsMsg *msg, jsOptions *opts)
{
    natsStatus  s;
    _ackOpts    o = {jsAckNak, false, false, 0};

    s = _ackMsg(msg, opts, &o, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_NakWithDelay(natsMsg *msg, int64_t delay, jsOptions *opts)
{
    natsStatus  s;
    _ackOpts    o = {jsAckNak, false, false, delay};

    s = _ackMsg(msg, opts, &o, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_InProgress(natsMsg *msg, jsOptions *opts)
{
    natsStatus  s;
    _ackOpts    o = {jsAckInProgress, true, false, 0};

    s = _ackMsg(msg, opts, &o, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsMsg_Term(natsMsg *msg, jsOptions *opts)
{
    natsStatus  s;
    _ackOpts    o = {jsAckTerm, false, false, 0};

    s = _ackMsg(msg, opts, &o, NULL);
    return NATS_UPDATE_ERR_STACK(s);
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

    s = js_getMetaData(msg->reply+jsAckPrefixLen,
                        &(meta->Domain),
                        &(meta->Stream),
                        &(meta->Consumer),
                        &(meta->NumDelivered),
                        &(meta->Sequence.Stream),
                        &(meta->Sequence.Consumer),
                        &(meta->Timestamp),
                        &(meta->NumPending),
                        8);
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
    NATS_FREE(meta->Domain);
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

    while ((*p != '\0') && isspace((unsigned char) *p))
        p++;

    if ((*p == '\r') || (*p == '\n') || (*p == '\0'))
        return false;

    if (strstr(p, HDR_STATUS_CTRL_100) != p)
        return false;

    p += HDR_STATUS_LEN;

    if (!isspace((unsigned char) *p))
        return false;

    while (isspace((unsigned char) *p))
        p++;

    if (strstr(p, "Idle") == p)
        *ctrlType = jsCtrlHeartbeat;
    else if (strstr(p, "Flow") == p)
        *ctrlType = jsCtrlFlowControl;

    return true;
}

// Update and replace sid.
// Lock should be held on entry but will be unlocked to prevent lock inversion.
int64_t
applyNewSID(natsSubscription *sub)
{
    int64_t         osid = 0;
    int64_t         nsid = 0;
    natsConnection  *nc  = sub->conn;

    natsSub_Unlock(sub);

    natsMutex_Lock(nc->subsMu);
    osid = sub->sid;
    natsHash_Remove(nc->subs, osid);
    // Place new one.
    nc->ssid++;
    nsid = nc->ssid;
    natsHash_Set(nc->subs, nsid, sub, NULL);
    natsMutex_Unlock(nc->subsMu);

    natsSub_Lock(sub);
    sub->sid = nsid;
    return osid;
}

static void
_recreateOrderedCons(void *closure)
{
    jsOrderedConsInfo   *oci = (jsOrderedConsInfo*) closure;
    natsConnection      *nc  = oci->nc;
    natsSubscription    *sub = oci->sub;
    natsThread          *t   = NULL;
    jsSub               *jsi = NULL;
    jsConsumerInfo      *ci  = NULL;
    jsConsumerConfig    *cc  = NULL;
    natsStatus          s;

    // Note: if anything fail here, the reset/recreate of the ordered consumer
    // will happen again based on the missed HB timer.

    // Unsubscribe and subscribe with new inbox and sid.
    // Remap a new low level sub into this sub since its client accessible.
    // This is done here in this thread to prevent lock inversion.

    natsConn_Lock(nc);
    SET_WRITE_DEADLINE(nc);
    s = natsConn_sendUnsubProto(nc, oci->osid, 0);
    if (!oci->done)
    {
        IFOK(s, natsConn_sendSubProto(nc, oci->ndlv, NULL, oci->nsid));
        if ((s == NATS_OK) && (oci->max > 0))
            s = natsConn_sendUnsubProto(nc, oci->nsid, oci->max);
    }
    IFOK(s, natsConn_flushOrKickFlusher(nc));
    natsConn_Unlock(nc);

    if (!oci->done && (s == NATS_OK))
    {
        natsSub_Lock(sub);
        t = oci->thread;
        jsi = sub->jsi;
        s = js_cloneConsumerConfig(jsi->ocCfg, &cc);
        natsSub_Unlock(sub);

        if (s == NATS_OK)
        {
            // Create consumer request for starting policy.
            cc->DeliverSubject = oci->ndlv;
            cc->DeliverPolicy  = js_DeliverByStartSequence;
            cc->OptStartSeq    = oci->sseq;

            s = js_AddConsumer(&ci, jsi->js, jsi->stream, cc, NULL, NULL);
            if (s == NATS_OK)
            {
                natsSub_Lock(sub);
                // Set the consumer name only if the consumer's info delivery subject
                // matches the subscription's current subject.
                if (strcmp(ci->Config->DeliverSubject, sub->subject) == 0)
                {
                    NATS_FREE(jsi->consumer);
                    jsi->consumer = NULL;
                    DUP_STRING(s, jsi->consumer, ci->Name);
                }
                natsSub_Unlock(sub);

                jsConsumerInfo_Destroy(ci);
            }

            // Clear cc->DeliverSubject now before destroying it (since
            // cc->DeliverSubject points to oci->ndlv (not a copy)
            cc->DeliverSubject = NULL;
            js_destroyConsumerConfig(cc);
        }
    }
    if (s != NATS_OK)
    {
        char tmp[256];
        const char *lastErr = nats_GetLastError(NULL);
        natsConn_Lock(nc);
        snprintf(tmp, sizeof(tmp),
                 "error recreating ordered consumer, will try again: status=%u error=%s",
                 s, (nats_IsStringEmpty(lastErr) ? natsStatus_GetText(s) : lastErr));
        natsAsyncCb_PostErrHandler(nc, sub, s, NATS_STRDUP(tmp));
        natsConn_Unlock(nc);
    }

    NATS_FREE(oci->ndlv);
    NATS_FREE(oci);
    natsThread_Detach(t);
    natsThread_Destroy(t);
    natsSub_release(sub);
}

// We are here if we have detected a gap with an ordered consumer.
// We will create a new consumer and rewire the low level subscription.
// Lock should be held.
natsStatus
jsSub_resetOrderedConsumer(natsSubscription *sub, uint64_t sseq)
{
    natsStatus          s           = NATS_OK;
    natsConnection      *nc         = sub->conn;
    int64_t             osid        = 0;
    natsInbox           *newDeliver = NULL;
    jsOrderedConsInfo   *oci        = NULL;
    int                 max         = 0;
    bool                done        = false;
    jsSub               *jsi        = sub->jsi;

    if ((jsi == NULL) || (nc == NULL) || sub->closed)
        return NATS_OK;

    // Note: if anything fail here, the reset/recreate of the ordered consumer
    // will happen again based on the missed HB timer.

    // If there was an AUTO_UNSUB, we need to adjust the new value and send
    // an UNSUB for the new sid with new value.
    if (sub->max > 0)
    {
        // If we are at or anove sub->max, then we are done with this sub
        // and will send an UNSUB in the _recreateOrderedCons thread function.
        if (sub->jsi->fciseq < sub->max)
            max = (int)(sub->max - jsi->fciseq);
        else
            done = true;
    }

    // Grab new inbox.
    s = natsConn_newInbox(nc, &newDeliver);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // Quick unsubscribe. Since we know this is a simple push subscriber we do in place.
    osid = applyNewSID(sub);

    NATS_FREE(sub->subject);
    sub->subject = (char*) newDeliver;

    // We are still in the low level readloop for the connection so we need
    // to spin a thread to try to create the new consumer.
    // Create object that will hold some state to pass to the thread.
    oci = NATS_CALLOC(1, sizeof(jsOrderedConsInfo));
    if (oci == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
        DUP_STRING(s, oci->ndlv, (char*) newDeliver);

    if (s == NATS_OK)
    {
        // Reset some items in jsi.
        jsi->dseq = 1;
        NATS_FREE(jsi->fcReply);
        jsi->fcReply = NULL;
        jsi->fcDelivered = 0;
        NATS_FREE(jsi->cmeta);
        jsi->cmeta = NULL;

        oci->osid = osid;
        oci->nsid = sub->sid;
        oci->sseq = sseq;
        oci->nc   = nc;
        oci->sub  = sub;
        oci->max  = max;
        oci->done = done;
        natsSub_retain(sub);

        s = natsThread_Create(&oci->thread, _recreateOrderedCons, (void*) oci);
        if (s != NATS_OK)
        {
            NATS_FREE(oci);
            natsSub_release(sub);
        }
    }
    if ((s != NATS_OK) && (oci != NULL))
    {
        NATS_FREE(oci->ndlv);
        NATS_FREE(oci);
    }
    return s;
}

// Check to make sure messages are arriving in order.
// Returns true if the sub had to be replaced. Will cause upper layers to return.
// The caller has verified that sub.jsi != nil and that this is not a control message.
// Lock should be held.
natsStatus
jsSub_checkOrderedMsg(natsSubscription *sub, natsMsg *msg, bool *reset)
{
    natsStatus  s    = NATS_OK;
    jsSub       *jsi = NULL;
    uint64_t    sseq = 0;
    uint64_t    dseq = 0;

    *reset = false;

    // Ignore msgs with no reply like HBs and flowcontrol, they are handled elsewhere.
    if (natsMsg_GetReply(msg) == NULL)
        return NATS_OK;

    // Normal message here.
    s = js_getMetaData(natsMsg_GetReply(msg), NULL, NULL, NULL, NULL, &sseq, &dseq, NULL, NULL, 2);
    if (s == NATS_OK)
    {
        jsi = sub->jsi;
        if (dseq != jsi->dseq)
        {
            *reset = true;
            s = jsSub_resetOrderedConsumer(sub, jsi->sseq+1);
        }
        else
        {
            // Update our tracking here.
            jsi->dseq = dseq+1;
            jsi->sseq = sseq;
        }
    }
    return NATS_UPDATE_ERR_STACK(s);
}
