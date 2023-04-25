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

#include "micro.h"
#include "microp.h"
#include "util.h"

static microError *
marshal_ping(natsBuffer **new_buf, microService *m);
static void
handle_ping(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static microError *
marshal_info(natsBuffer **new_buf, microServiceInfo *info);
static void
handle_info(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static microError *
marshal_stats(natsBuffer **new_buf, microServiceStats *stats);
static void
handle_stats(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static microError *
add_internal_handler(microService *m, const char *verb, const char *kind, const char *id, const char *name, natsMsgHandler handler);
static microError *
add_verb_handlers(microService *m, const char *verb, natsMsgHandler handler);
static microError *
new_dotted_subject(char **new_subject, int count, ...);

microError *
micro_init_monitoring(microService *m)
{
    microError *err = NULL;

    m->monitoring_subs = NATS_CALLOC(MICRO_MONITORING_SUBS_CAP, sizeof(natsSubscription *));
    if (m->monitoring_subs == NULL)
        return micro_ErrorOutOfMemory;
    m->monitoring_subs_len = 0;

    MICRO_CALL(err, add_verb_handlers(m, MICRO_PING_VERB, handle_ping));
    MICRO_CALL(err, add_verb_handlers(m, MICRO_STATS_VERB, handle_stats));
    MICRO_CALL(err, add_verb_handlers(m, MICRO_INFO_VERB, handle_info));
    return err;
}

static void
handle_ping(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    microError *err = NULL;
    microService *m = (microService *)closure;
    microRequest *req = NULL;
    natsBuffer *buf = NULL;

    if ((m == NULL) || (m->cfg == NULL))
        return; // Should not happen

    MICRO_CALL(err, marshal_ping(&buf, m));
    MICRO_CALL(err, micro_new_request(&req, m, NULL, msg));

    // respond for both success and error cases.
    microRequest_Respond(req, &err, natsBuf_Data(buf), natsBuf_Len(buf));

    micro_destroy_request(req);
    natsMsg_Destroy(msg);
    natsBuf_Destroy(buf);
}

static void
handle_info(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    microError *err = NULL;
    microService *m = (microService *)closure;
    microRequest *req = NULL;
    microServiceInfo *info = NULL;
    natsBuffer *buf = NULL;

    if ((m == NULL) || (m->cfg == NULL))
        return; // Should not happen

    MICRO_CALL(err, microService_GetInfo(&info, m));
    MICRO_CALL(err, marshal_info(&buf, info));
    MICRO_CALL(err, micro_new_request(&req, m, NULL, msg));

    // respond for both success and error cases.
    microRequest_Respond(req, &err, natsBuf_Data(buf), natsBuf_Len(buf));

    natsBuf_Destroy(buf);
    natsMsg_Destroy(msg);
    microServiceInfo_Destroy(info);
}

static void
handle_stats_internal(microRequest *req)
{
    microError *err = NULL;
    microServiceStats *stats = NULL;
    natsBuffer *buf = NULL;

    if ((req == NULL) || (req->service == NULL) || (req->service->cfg == NULL))
        return; // Should not happen

    MICRO_CALL(err, microService_GetStats(&stats, req->service));
    MICRO_CALL(err, marshal_stats(&buf, stats));

    // respond for both success and error cases.
    microRequest_Respond(req, &err, natsBuf_Data(buf), natsBuf_Len(buf));

    natsBuf_Destroy(buf);
    natsMicroserviceStats_Destroy(stats);
}

static void
handle_stats(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    microError *err = NULL;
    microService *m = (microService *)closure;
    microRequest *req = NULL;
    microRequestHandler h = handle_stats_internal;

    if ((m == NULL) || (m->cfg == NULL))
        return; // Should not happen

    if (m->cfg->stats_handler != NULL)
        h = m->cfg->stats_handler;

    MICRO_CALL(err, micro_new_request(&req, m, NULL, msg));
    MICRO_DO(err, h(req));
    MICRO_DO(err, micro_destroy_request(req));
    natsMsg_Destroy(msg);
}

static microError *
new_dotted_subject(char **new_subject, int count, ...)
{
    va_list args;
    int i, len, n;
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
        return micro_Errorf(400, "service name is required when id is provided: %s", id);
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
                     const char *id, const char *name, natsMsgHandler handler)
{
    microError *err = NULL;
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    char *subj = NULL;

    if (m->monitoring_subs_len >= MICRO_MONITORING_SUBS_CAP)
        return micro_Errorf(500, "too many monitoring subscriptions (max: %d)", MICRO_MONITORING_SUBS_CAP);

    err = micro_new_control_subject(&subj, verb, kind, id);
    if (err != NULL)
        return err;

    s = natsConnection_Subscribe(&sub, m->nc, subj, handler, m);
    NATS_FREE(subj);
    if (s != NATS_OK)
    {
        microService_Stop(m);
        return micro_ErrorFromStatus(s);
    }

    m->monitoring_subs[m->monitoring_subs_len] = sub;
    m->monitoring_subs_len++;

    return NULL;
}

// __verbHandlers generates control handlers for a specific verb. Each request
// generates 3 subscriptions, one for the general verb affecting all services
// written with the framework, one that handles all services of a particular
// kind, and finally a specific service instance.
static microError *
add_verb_handlers(microService *m, const char *verb, natsMsgHandler handler)
{
    microError *err = NULL;
    char name[1024];

    snprintf(name, sizeof(name), "%s-all", verb);
    err = add_internal_handler(m, verb, "", "", name, handler);
    if (err == NULL)
    {
        snprintf(name, sizeof(name), "%s-kind", verb);
        err = add_internal_handler(m, verb, m->cfg->name, "", name, handler);
    }
    if (err == NULL)
    {
        err = add_internal_handler(m, verb, m->cfg->name, m->id, verb, handler);
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
        s = natsBuf_Append(buf, "{", -1);
        IFOK_attr("name", m->cfg->name, ",");
        IFOK_attr("version", m->cfg->version, ",");
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

    s = natsBuf_Create(&buf, 4096);
    IFOK(s, natsBuf_Append(buf, "{", -1));
    IFOK_attr("description", info->description, ",");
    IFOK_attr("id", info->id, ",");
    IFOK_attr("name", info->name, ",");
    IFOK_attr("type", info->type, ",");
    if ((s == NATS_OK) && (info->subjects_len > 0))
    {
        int i;
        IFOK(s, natsBuf_Append(buf, "\"subjects\":[", -1));
        for (i = 0; i < info->subjects_len; i++)
        {
            IFOK(s, natsBuf_Append(buf, "\"", -1));
            IFOK(s, natsBuf_Append(buf, info->subjects[i], -1));
            IFOK(s, natsBuf_Append(buf, "\"", -1));
            if (i < (info->subjects_len - 1))
                IFOK(s, natsBuf_Append(buf, ",", -1));
        }
        IFOK(s, natsBuf_Append(buf, "],", -1));
    }
    IFOK_attr("version", info->version, "");
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
    IFOK_attr("id", stats->id, ",");
    IFOK_attr("name", stats->name, ",");
    IFOK_attr("type", stats->type, ",");
    IFOK(s, nats_EncodeTimeUTC(timebuf, sizeof(timebuf), stats->started));
    IFOK_attr("started", timebuf, ",");

    if ((s == NATS_OK) && (stats->endpoints_len > 0))
    {
        IFOK(s, natsBuf_Append(buf, "\"endpoints\":[", -1));
        for (i = 0; i < stats->endpoints_len; i++)
        {
            ep = &stats->endpoints[i];
            IFOK(s, natsBuf_AppendByte(buf, '{'));
            IFOK_attr("name", ep->name, ",");
            IFOK_attr("subject", ep->subject, ",");
            IFOK(s, nats_marshalLong(buf, false, "num_requests", ep->num_requests));
            IFOK(s, nats_marshalLong(buf, true, "num_errors", ep->num_errors));
            IFOK(s, nats_marshalDuration(buf, true, "average_processing_time", ep->average_processing_time_ns));
            IFOK(s, natsBuf_AppendByte(buf, ','));
            IFOK_attr("last_error", ep->last_error_string, "");
            IFOK(s, natsBuf_Append(buf, "}", -1));

            if (i < (stats->endpoints_len - 1))
                IFOK(s, natsBuf_Append(buf, ",", -1));
        }
        IFOK(s, natsBuf_Append(buf, "],", -1));
    }

    IFOK_attr("version", stats->version, "");
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
