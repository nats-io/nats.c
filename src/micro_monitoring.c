// Copyright 2023 The NATS Authors
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

#include "microp.h"
#include "util.h"

static microError *marshal_ping(natsBuffer **new_buf, microService *m);
static microError *handle_ping(microRequest *req);
static microError *marshal_info(natsBuffer **new_buf, microServiceInfo *info);
static microError *handle_info(microRequest *req);
static microError *marshal_stats(natsBuffer **new_buf, microServiceStats *stats);
static microError *handle_stats(microRequest *req);

static microError *
add_internal_handler(microService *m, const char *verb, const char *kind, const char *id, const char *name, microRequestHandler handler);
static microError *
add_verb_handlers(microService *m, const char *verb, microRequestHandler handler);
static microError *
new_dotted_subject(char **new_subject, int count, ...);

microError *
micro_init_monitoring(microService *m)
{
    microError *err = NULL;
    MICRO_CALL(err, add_verb_handlers(m, MICRO_PING_VERB, handle_ping));
    MICRO_CALL(err, add_verb_handlers(m, MICRO_STATS_VERB, handle_stats));
    MICRO_CALL(err, add_verb_handlers(m, MICRO_INFO_VERB, handle_info));
    return err;
}

static microError *
handle_ping(microRequest *req)
{
    microError *err = NULL;
    microService *m = microRequest_GetService(req);
    natsBuffer *buf = NULL;

    if ((m == NULL) || (m->cfg == NULL))
        return micro_ErrorInvalidArg; // Should not happen

    MICRO_CALL(err, marshal_ping(&buf, m));
    MICRO_CALL(err, microRequest_Respond(req, natsBuf_Data(buf), natsBuf_Len(buf)));

    natsBuf_Destroy(buf);
    return err;
}

static microError *
handle_info(microRequest *req)
{
    microError *err = NULL;
    microService *m = microRequest_GetService(req);
    microServiceInfo *info = NULL;
    natsBuffer *buf = NULL;

    if ((m == NULL) || (m->cfg == NULL))
        return micro_ErrorInvalidArg; // Should not happen

    MICRO_CALL(err, microService_GetInfo(&info, m));
    MICRO_CALL(err, marshal_info(&buf, info));
    MICRO_CALL(err, microRequest_Respond(req, natsBuf_Data(buf), natsBuf_Len(buf)));

    natsBuf_Destroy(buf);
    microServiceInfo_Destroy(info);
    return err;
}

static microError *
handle_stats_default(microRequest *req)
{
    microError *err = NULL;
    microService *m = microRequest_GetService(req);
    microServiceStats *stats = NULL;
    natsBuffer *buf = NULL;

    if ((m == NULL) || (m == NULL))
        return micro_ErrorInvalidArg; // Should not happen

    MICRO_CALL(err, microService_GetStats(&stats, req->Service));
    MICRO_CALL(err, marshal_stats(&buf, stats));
    MICRO_CALL(err, microRequest_Respond(req, natsBuf_Data(buf), natsBuf_Len(buf)));

    natsBuf_Destroy(buf);
    microServiceStats_Destroy(stats);
    return err;
}

static microError *
handle_stats(microRequest *req)
{
    microService *m = microRequest_GetService(req);

    if ((m == NULL) || (m->cfg == NULL))
        return micro_ErrorInvalidArg; // Should not happen

    if (m->cfg->StatsHandler != NULL)
        return m->cfg->StatsHandler(req);
    else
        return handle_stats_default(req);
}

static microError *
new_dotted_subject(char **new_subject, int count, ...)
{
    va_list args;
    int i;
    size_t len, n;
    char *result, *p;

    va_start(args, count);
    len = 0;
    for (i = 0; i < count; i++)
    {
        if (i > 0)
        {
            len++; /* for the dot */
        }
        len += strlen(va_arg(args, char *));
    }
    va_end(args);

    result = NATS_CALLOC(len + 1, 1);
    if (result == NULL)
    {
        return micro_ErrorInvalidArg;
    }

    len = 0;
    va_start(args, count);
    for (i = 0; i < count; i++)
    {
        if (i > 0)
        {
            result[len++] = '.';
        }
        p = va_arg(args, char *);
        n = strlen(p);
        memcpy(result + len, p, n);
        len += n;
    }
    va_end(args);

    *new_subject = result;
    return NULL;
}

microError *
micro_new_control_subject(char **newSubject, const char *verb, const char *name, const char *id)
{
    if (nats_IsStringEmpty(name) && !nats_IsStringEmpty(id))
    {
        return micro_Errorf("service name is required when id is provided: '%s'", id);
    }

    else if (nats_IsStringEmpty(name) && nats_IsStringEmpty(id))
        return new_dotted_subject(newSubject, 2, MICRO_API_PREFIX, verb);
    else if (nats_IsStringEmpty(id))
        return new_dotted_subject(newSubject, 3, MICRO_API_PREFIX, verb, name);
    else
        return new_dotted_subject(newSubject, 4, MICRO_API_PREFIX, verb, name, id);
}

static microError *
add_internal_handler(microService *m, const char *verb, const char *kind,
                     const char *id, const char *name, microRequestHandler handler)
{
    microError *err = NULL;
    char *subj = NULL;

    err = micro_new_control_subject(&subj, verb, kind, id);
    if (err != NULL)
        return err;

    microEndpointConfig cfg = {
        .Subject = subj,
        .Name = name,
        .Handler = handler,
    };
    err = micro_add_endpoint(NULL, m, NULL, &cfg, true);
    NATS_FREE(subj);
    return err;
}

// __verbHandlers generates control handlers for a specific verb. Each request
// generates 3 subscriptions, one for the general verb affecting all services
// written with the framework, one that handles all services of a particular
// kind, and finally a specific service instance.
static microError *
add_verb_handlers(microService *m, const char *verb, microRequestHandler handler)
{
    microError *err = NULL;
    char name[1024];

    snprintf(name, sizeof(name), "%s-all", verb);
    err = add_internal_handler(m, verb, "", "", name, handler);
    if (err == NULL)
    {
        snprintf(name, sizeof(name), "%s-kind", verb);
        err = add_internal_handler(m, verb, m->cfg->Name, "", name, handler);
    }
    if (err == NULL)
    {
        err = add_internal_handler(m, verb, m->cfg->Name, m->id, verb, handler);
    }
    return err;
}

// name and sep must be a string literal
#define IFOK_attr(_name, _value, _sep)                                  \
    IFOK(s, natsBuf_Append(buf, "\"" _name "\":\"", -1));               \
    IFOK(s, natsBuf_Append(buf, (_value) != NULL ? (_value) : "", -1)); \
    IFOK(s, natsBuf_Append(buf, "\"" _sep, -1));

static microError *
marshal_ping(natsBuffer **new_buf, microService *m)
{
    natsBuffer *buf = NULL;
    natsStatus s;

    s = natsBuf_Create(&buf, 1024);
    if (s == NATS_OK)
    {
        s = natsBuf_AppendByte(buf, '{');
        IFOK_attr("name", m->cfg->Name, ",");
        IFOK_attr("version", m->cfg->Version, ",");
        IFOK_attr("id", m->id, ",");
        IFOK_attr("type", MICRO_PING_RESPONSE_TYPE, "");
        IFOK(s, natsBuf_AppendByte(buf, '}'));
    }

    if (s != NATS_OK)
    {
        natsBuf_Destroy(buf);
        return micro_ErrorFromStatus(s);
    }

    *new_buf = buf;
    return NULL;
}

static microError *
marshal_info(natsBuffer **new_buf, microServiceInfo *info)
{
    natsBuffer *buf = NULL;
    natsStatus s;
    int i;

    s = natsBuf_Create(&buf, 4096);
    IFOK(s, natsBuf_AppendByte(buf, '{'));

    IFOK_attr("description", info->Description, ",");

    // "endpoints":{...}
    if ((s == NATS_OK) && (info->EndpointsLen > 0))
    {
        IFOK(s, natsBuf_Append(buf, "\"endpoints\":[", -1));
        for (i = 0; ((s == NATS_OK) && (i < info->EndpointsLen)); i++)
        {
            IFOK(s, natsBuf_AppendByte(buf, '{'));
            IFOK_attr("name", info->Endpoints[i].Name, "");
            IFOK(s, nats_marshalMetadata(buf, true, "metadata", info->Endpoints[i].Metadata));
            IFOK(s, natsBuf_AppendByte(buf, ','));
            if (!nats_IsStringEmpty(info->Endpoints[i].QueueGroup))
                IFOK_attr("queue_group", info->Endpoints[i].QueueGroup, ",");
            IFOK_attr("subject", info->Endpoints[i].Subject, "");
            IFOK(s, natsBuf_AppendByte(buf, '}')); // end endpoint
            if (i != info->EndpointsLen - 1)
                IFOK(s, natsBuf_AppendByte(buf, ','));
        }
        IFOK(s, natsBuf_Append(buf, "],", 2));
    }

    IFOK_attr("id", info->Id, "");
    IFOK(s, nats_marshalMetadata(buf, true, "metadata", info->Metadata));
    IFOK(s, natsBuf_AppendByte(buf, ','));
    IFOK_attr("name", info->Name, ",");
    IFOK_attr("type", info->Type, ",");
    IFOK_attr("version", info->Version, "");
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s != NATS_OK)
    {
        natsBuf_Destroy(buf);
        return microError_Wrapf(micro_ErrorFromStatus(s), "failed to marshal service info");
    }
    *new_buf = buf;
    return NULL;
}

static microError *
marshal_stats(natsBuffer **new_buf, microServiceStats *stats)
{
    natsBuffer *buf = NULL;
    natsStatus s;
    int i;
    char timebuf[128];
    microEndpointStats *ep;

    s = natsBuf_Create(&buf, 8 * 1024);
    IFOK(s, natsBuf_AppendByte(buf, '{'));
    IFOK_attr("id", stats->Id, ",");
    IFOK_attr("name", stats->Name, ",");
    IFOK_attr("type", stats->Type, ",");
    IFOK(s, nats_EncodeTimeUTC(timebuf, sizeof(timebuf), stats->Started));
    IFOK_attr("started", timebuf, ",");

    if ((s == NATS_OK) && (stats->EndpointsLen > 0))
    {
        IFOK(s, natsBuf_Append(buf, "\"endpoints\":[", -1));
        for (i = 0; i < stats->EndpointsLen; i++)
        {
            ep = &stats->Endpoints[i];
            IFOK(s, natsBuf_AppendByte(buf, '{'));
            IFOK_attr("name", ep->Name, ",");
            IFOK_attr("subject", ep->Subject, ",");
            if (!nats_IsStringEmpty(ep->QueueGroup))
                IFOK_attr("queue_group", ep->QueueGroup, ",");
            IFOK(s, nats_marshalLong(buf, false, "num_requests", ep->NumRequests));
            IFOK(s, nats_marshalLong(buf, true, "num_errors", ep->NumErrors));
            IFOK(s, nats_marshalDuration(buf, true, "average_processing_time", ep->AverageProcessingTimeNanoseconds));
            IFOK(s, natsBuf_AppendByte(buf, ','));
            IFOK_attr("last_error", ep->LastErrorString, "");
            IFOK(s, natsBuf_AppendByte(buf, '}'));

            if (i < (stats->EndpointsLen - 1))
                IFOK(s, natsBuf_AppendByte(buf, ','));
        }
        IFOK(s, natsBuf_Append(buf, "],", 2));
    }

    IFOK_attr("version", stats->Version, "");
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
    {
        *new_buf = buf;
        return NULL;
    }
    else
    {
        natsBuf_Destroy(buf);
        return microError_Wrapf(micro_ErrorFromStatus(s), "failed to marshal service info");
    }
}
