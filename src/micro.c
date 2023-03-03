// Copyright 2021-2023 The NATS Authors
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
#include <stdarg.h>

#include "natsp.h"
#include "micro.h"
#include "mem.h"
#include "js.h"
// #include "conn.h"
// #include "sub.h"

static void _freeMicroservice(jsMicroservice *m);
static void _retainMicroservice(jsMicroservice *m);
static void _releaseMicroservice(jsMicroservice *m);
static natsStatus _createMicroservice(jsMicroservice **new_microservice, jsCtx *js, jsMicroserviceConfig *cfg);
static bool _isEmpty(const char *s);
static natsStatus _newDottedSubject(char **new_subject, int count, ...);
static natsStatus _newControlSubject(char **newSubject, jsMicroserviceVerb verb, const char *name, const char *id);
static natsStatus _addInternalHandler(jsMicroservice *m, jsMicroserviceVerb verb, const char *kind, const char *id, const char *name, natsMsgHandler handler, jsErrCode *errCode);
static natsStatus _addVerbHandlers(jsMicroservice *m, jsMicroserviceVerb verb, natsMsgHandler handler);
static natsStatus _respond(jsMicroservice *m, jsMicroserviceRequest *r, const char *data, int len, jsErrCode *errCode);
static natsStatus _marshalPing(natsBuffer **new_buf, jsMicroservice *m);
static void _handleMicroservicePing(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

//////////////////////////////////////////////////////////////////////////////
// microservice management APIs
//////////////////////////////////////////////////////////////////////////////

natsStatus
js_AddMicroservice(jsMicroservice **new_m, jsCtx *js, jsMicroserviceConfig *cfg, jsErrCode *errCode)
{
    natsStatus s;

    if ((new_m == NULL) || (js == NULL) || (cfg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _createMicroservice(new_m, js, cfg);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (errCode != NULL)
    {
        *errCode = 0;
    }
    return NATS_OK;
}

natsStatus
js_StopMicroservice(jsMicroservice *m, jsErrCode *errCode)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsMutex_Lock(m->mu);

    if (m->stopped)
        goto OK;

    m->stopped = true;

    // if s.DoneHandler != nil {
    // 	s.asyncDispatcher.push(func() { s.DoneHandler(s) })
    // 	s.asyncDispatcher.close()
    // }

OK:
    natsMutex_Unlock(m->mu);
    if (errCode != NULL)
    {
        *errCode = 0;
    }
    return NATS_OK;
}

bool js_IsMicroserviceStopped(jsMicroservice *m)
{
    bool stopped;

    if ((m == NULL) || (m->mu == NULL))
        return true;

    natsMutex_Lock(m->mu);
    stopped = m->stopped;
    natsMutex_Unlock(m->mu);
    return stopped;
}

// TODO: eliminate sleep
natsStatus
js_RunMicroservice(jsMicroservice *m, jsErrCode *errCode)
{
    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    for (; !js_IsMicroserviceStopped(m);)
    {
        nats_Sleep(50);
    }
    return NATS_OK;
}

static void
_freeMicroservice(jsMicroservice *m)
{
    jsCtx *js = NULL;

    if (m == NULL)
        return;

    js = m->js;
    natsMutex_Destroy(m->mu);
    NATS_FREE(m->identity.id);
    NATS_FREE(m);
    js_release(js);
}

static void
_retainMicroservice(jsMicroservice *m)
{
    natsMutex_Lock(m->mu);
    m->refs++;
    natsMutex_Unlock(m->mu);
}

static void
_releaseMicroservice(jsMicroservice *m)
{
    bool doFree;

    if (m == NULL)
        return;

    natsMutex_Lock(m->mu);
    doFree = (--(m->refs) == 0);
    natsMutex_Unlock(m->mu);

    if (doFree)
        _freeMicroservice(m);
}

void jsMicroservice_Destroy(jsMicroservice *m)
{
    _releaseMicroservice(m);
}

static bool
_isEmpty(const char *s)
{
    return (s == NULL || *s == '\0');
}

static natsStatus
_newDottedSubject(char **new_subject, int count, ...)
{
    va_list args1, args2;
    int i, len, n;
    char *result, *p;

    va_start(args1, count);
    va_copy(args2, args1);

    len = 0;
    for (i = 0; i < count; i++)
    {
        if (i > 0)
        {
            len++; /* for the dot */
        }
        len += strlen(va_arg(args1, char *));
    }
    va_end(args1);

    result = NATS_MALLOC(len + 1);
    if (result == NULL)
    {
        va_end(args2);
        return NATS_NO_MEMORY;
    }

    len = 0;
    for (i = 0; i < count; i++)
    {
        if (i > 0)
        {
            result[len++] = '.';
        }
        p = va_arg(args2, char *);
        n = strlen(p);
        memcpy(result + len, p, n);
        len += n;
    }
    va_end(args2);

    *new_subject = result;
    return NATS_OK;
}

static natsStatus
_newControlSubject(char **newSubject, jsMicroserviceVerb verb, const char *name, const char *id)
{
    natsStatus s = NATS_OK;
    const char *verbStr;

    s = jsMicroserviceVerb_String(&verbStr, verb);
    if (s != NATS_OK)
    {
        return NATS_UPDATE_ERR_STACK(s);
    }

    if (_isEmpty(name) && !_isEmpty(id))
    {
        NATS_UPDATE_ERR_TXT("service name is required when id is provided: %s", id);
        return NATS_UPDATE_ERR_STACK(NATS_INVALID_ARG);
    }

    else if (_isEmpty(name) && _isEmpty(id))
        return _newDottedSubject(newSubject, 2, jsMicroserviceAPIPrefix, verbStr);
    else if (_isEmpty(id))
        return _newDottedSubject(newSubject, 3, jsMicroserviceAPIPrefix, verbStr, name);
    else
        return _newDottedSubject(newSubject, 4, jsMicroserviceAPIPrefix, verbStr, name, id);
}

static natsStatus
_addInternalHandler(jsMicroservice *m, jsMicroserviceVerb verb, const char *kind,
                    const char *id, const char *name, natsMsgHandler handler, jsErrCode *errCode)
{

    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    char *subj = NULL;

    s = _newControlSubject(&subj, verb, kind, id);
    if (s == NATS_OK)
    {
        s = natsConnection_Subscribe(&sub, m->js->nc, subj, handler, m);
    }
    if (s == NATS_OK)
    {
        return NATS_OK;
    }

    js_StopMicroservice(m, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

// __verbHandlers generates control handlers for a specific verb. Each request
// generates 3 subscriptions, one for the general verb affecting all services
// written with the framework, one that handles all services of a particular
// kind, and finally a specific service instance.
static natsStatus
_addVerbHandlers(jsMicroservice *m, jsMicroserviceVerb verb, natsMsgHandler handler)
{
    natsStatus s = NATS_OK;
    char name[256];
    const char *verbStr;

    s = jsMicroserviceVerb_String(&verbStr, verb);
    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-all", verbStr);
        s = _addInternalHandler(m, verb, "", "", name, handler, NULL);
    }
    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-kind", verbStr);
        s = _addInternalHandler(m, verb, m->identity.name, "", name, handler, NULL);
    }
    if (s == NATS_OK)
    {
        s = _addInternalHandler(m, verb, m->identity.name, m->identity.id, verbStr, handler, NULL);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_respond(jsMicroservice *m, jsMicroserviceRequest *r, const char *data, int len, jsErrCode *errCode)
{
    natsStatus s = NATS_OK;
    natsMsg *respMsg = NULL;

    if ((m == NULL) || (m->mu == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = natsConnection_Publish(m->js->nc, natsMsg_GetReply(r->msg), data, len);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalPing(natsBuffer **new_buf, jsMicroservice *m)
{
    natsBuffer *buf = NULL;
    natsStatus s;

    s = natsBuf_Create(&buf, 1024);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

// name and sep must be a string literal
#define IFOK_attr(_name, _value, _sep)                    \
    IFOK(s, natsBuf_Append(buf, "\"" _name "\":\"", -1)); \
    IFOK(s, natsBuf_Append(buf, _value, -1));             \
    IFOK(s, natsBuf_Append(buf, "\"" _sep, -1));

    s = natsBuf_Append(buf, "{", -1);
    IFOK_attr("name", m->identity.name, ",");
    IFOK_attr("version", m->identity.version, ",");
    IFOK_attr("id", m->identity.id, ",");
    IFOK_attr("type", jsMicroservicePingResponseType, "");
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
    {
        *new_buf = buf;
        return NATS_OK;
    }

    natsBuf_Destroy(buf);
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_handleMicroservicePing(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    jsMicroservice *m = (jsMicroservice *)closure;
    jsMicroserviceRequest req = {
        .msg = msg,
    };
    natsBuffer *buf = NULL;

    s = _marshalPing(&buf, m);
    if (s == NATS_OK)
    {
        s = _respond(m, &req, natsBuf_Data(buf), natsBuf_Len(buf), NULL);
    }
    if (buf != NULL)
    {
        natsBuf_Destroy(buf);
    }
}

static natsStatus
_createMicroservice(jsMicroservice **new_microservice, jsCtx *js, jsMicroserviceConfig *cfg)
{
    natsStatus s = NATS_OK;
    jsMicroservice *m = NULL;
    natsMutex *mu = NULL;

    m = (jsMicroservice *)NATS_CALLOC(1, sizeof(jsMicroservice));
    if (m == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);
    m->js = js;

    s = natsMutex_Create(&mu);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);
    m->mu = mu;

    char tmpNUID[NUID_BUFFER_LEN + 1];
    s = natsNUID_Next(tmpNUID, NUID_BUFFER_LEN + 1);
    IF_OK_DUP_STRING(s, m->identity.id, tmpNUID);
    if (s == NATS_OK)
    {
        m->identity.name = cfg->name;
        m->identity.version = cfg->version;
    }
    if (s == NATS_OK)
    {
        s = _addVerbHandlers(m, jsMicroserviceVerbPing, _handleMicroservicePing);
    }
    if (s == NATS_OK)
    {
        *new_microservice = m;
    }
    return NATS_UPDATE_ERR_STACK(s);
}
