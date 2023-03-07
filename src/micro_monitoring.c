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

#include <stdarg.h>

#include "natsp.h"
#include "mem.h"
#include "microp.h"

static void
_handleMicroservicePing(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static natsStatus _marshalPing(natsBuffer **new_buf, natsMicroservice *m);

static natsStatus
_addInternalHandler(natsMicroservice *m, natsMicroserviceVerb verb, const char *kind, const char *id,
                    const char *name, natsMsgHandler handler);
static natsStatus
_addVerbHandlers(natsMicroservice *m, natsMicroserviceVerb verb, natsMsgHandler handler);

static natsStatus _newControlSubject(char **newSubject, natsMicroserviceVerb verb, const char *name, const char *id);
static natsStatus _newDottedSubject(char **new_subject, int count, ...);
static bool _isEmpty(const char *s);

natsStatus _initMicroserviceMonitoring(natsMicroservice *m) {
    natsStatus s = NATS_OK;

    IFOK(s, _addVerbHandlers(m, natsMicroserviceVerbPing, _handleMicroservicePing));

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_handleMicroservicePing(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroservice *m = (natsMicroservice *)closure;
    natsMicroserviceRequest req = {
        .msg = msg,
    };
    natsBuffer *buf = NULL;

    s = _marshalPing(&buf, m);
    if (s == NATS_OK)
    {
        s = natsMicroservice_Respond(m, &req, natsBuf_Data(buf), natsBuf_Len(buf));
    }
    if (buf != NULL)
    {
        natsBuf_Destroy(buf);
    }
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
_newControlSubject(char **newSubject, natsMicroserviceVerb verb, const char *name, const char *id)
{
    natsStatus s = NATS_OK;
    const char *verbStr;

    s = natsMicroserviceVerb_String(&verbStr, verb);
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
        return _newDottedSubject(newSubject, 2, natsMicroserviceAPIPrefix, verbStr);
    else if (_isEmpty(id))
        return _newDottedSubject(newSubject, 3, natsMicroserviceAPIPrefix, verbStr, name);
    else
        return _newDottedSubject(newSubject, 4, natsMicroserviceAPIPrefix, verbStr, name, id);
}

static natsStatus
_addInternalHandler(natsMicroservice *m, natsMicroserviceVerb verb, const char *kind,
                    const char *id, const char *name, natsMsgHandler handler)
{

    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    char *subj = NULL;

    s = _newControlSubject(&subj, verb, kind, id);
    if (s == NATS_OK)
    {
        s = natsConnection_Subscribe(&sub, m->nc, subj, handler, m);
    }
    if (s == NATS_OK)
    {
        return NATS_OK;
    }

    natsMicroservice_Stop(m);
    return NATS_UPDATE_ERR_STACK(s);
}

// __verbHandlers generates control handlers for a specific verb. Each request
// generates 3 subscriptions, one for the general verb affecting all services
// written with the framework, one that handles all services of a particular
// kind, and finally a specific service instance.
static natsStatus
_addVerbHandlers(natsMicroservice *m, natsMicroserviceVerb verb, natsMsgHandler handler)
{
    natsStatus s = NATS_OK;
    char name[256];
    const char *verbStr;

    s = natsMicroserviceVerb_String(&verbStr, verb);
    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-all", verbStr);
        s = _addInternalHandler(m, verb, "", "", name, handler);
    }
    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-kind", verbStr);
        s = _addInternalHandler(m, verb, m->identity.name, "", name, handler);
    }
    if (s == NATS_OK)
    {
        s = _addInternalHandler(m, verb, m->identity.name, m->identity.id, verbStr, handler);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalPing(natsBuffer **new_buf, natsMicroservice *m)
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
    IFOK_attr("type", natsMicroservicePingResponseType, "");
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
    {
        *new_buf = buf;
        return NATS_OK;
    }

    natsBuf_Destroy(buf);
    return NATS_UPDATE_ERR_STACK(s);
}
