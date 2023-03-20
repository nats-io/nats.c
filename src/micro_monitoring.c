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
#include "mem.h"
#include "util.h"

static natsStatus
marshal_ping(natsBuffer **new_buf, natsMicroservice *m);
static void
handle_ping(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static natsStatus
marshal_info(natsBuffer **new_buf, natsMicroserviceInfo *info);
static void
handle_info(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static natsStatus
marshal_stats(natsBuffer **new_buf, natsMicroserviceStats *stats);
static void
handle_stats(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

static natsStatus
add_internal_handler(natsMicroservice *m, const char *verb, const char *kind, const char *id, const char *name, natsMsgHandler handler);
static natsStatus
add_verb_handlers(natsMicroservice *m, const char *verb, natsMsgHandler handler);
static natsStatus
new_control_subject(char **newSubject, const char *verb, const char *name, const char *id);
static natsStatus
new_dotted_subject(char **new_subject, int count, ...);

static natsStatus
marshal_duration(natsBuffer *out_buf, bool comma, const char *name, int64_t d);
static void
fmt_frac(char buf[], int w, uint64_t v, int prec, int *nw, uint64_t *nv);
static int
fmt_int(char buf[], int w, uint64_t v);

natsStatus
micro_monitoring_init(natsMicroservice *m)
{
    natsStatus s = NATS_OK;

    IFOK(s, add_verb_handlers(m, natsMicroservicePingVerb, handle_ping));
    IFOK(s, add_verb_handlers(m, natsMicroserviceStatsVerb, handle_stats));
    IFOK(s, add_verb_handlers(m, natsMicroserviceInfoVerb, handle_info));

    return NATS_UPDATE_ERR_STACK(s);
}

static void
handle_ping(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroservice *m = (natsMicroservice *)closure;
    natsMicroserviceRequest req = {
        .msg = msg,
    };
    natsBuffer *buf = NULL;

    s = marshal_ping(&buf, m);
    if (s == NATS_OK)
    {
        natsMicroserviceRequest_Respond(&req, natsBuf_Data(buf), natsBuf_Len(buf));
    }
    natsBuf_Destroy(buf);
}

void handle_info(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroservice *m = (natsMicroservice *)closure;
    natsMicroserviceRequest req = {
        .msg = msg,
    };
    natsMicroserviceInfo *info = NULL;
    natsBuffer *buf = NULL;

    s = natsMicroservice_Info(&info, m);
    IFOK(s, marshal_info(&buf, info));
    if (s == NATS_OK)
    {
        natsMicroserviceRequest_Respond(&req, natsBuf_Data(buf), natsBuf_Len(buf));
    }
    else
    {
        natsMicroserviceRequest_Error(&req, nats_NewStatusError(s));
    }
    natsBuf_Destroy(buf);
    natsMicroserviceInfo_Destroy(info);
}

static void
handle_stats(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    natsStatus s = NATS_OK;
    natsMicroservice *m = (natsMicroservice *)closure;
    natsMicroserviceRequest req = {
        .msg = msg,
    };
    natsMicroserviceStats *stats = NULL;
    natsBuffer *buf = NULL;

    s = natsMicroservice_Stats(&stats, m);
    IFOK(s, marshal_stats(&buf, stats));
    if (s == NATS_OK)
    {
        natsMicroserviceRequest_Respond(&req, natsBuf_Data(buf), natsBuf_Len(buf));
    }
    else
    {
        natsMicroserviceRequest_Error(&req, nats_NewStatusError(s));
    }
    natsBuf_Destroy(buf);
    natsMicroserviceStats_Destroy(stats);
}

static natsStatus
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

    result = NATS_MALLOC(len + 1);
    if (result == NULL)
    {
        return NATS_NO_MEMORY;
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
    return NATS_OK;
}

static natsStatus
new_control_subject(char **newSubject, const char *verb, const char *name, const char *id)
{
    if (nats_IsStringEmpty(name) && !nats_IsStringEmpty(id))
    {
        NATS_UPDATE_ERR_TXT("service name is required when id is provided: %s", id);
        return NATS_UPDATE_ERR_STACK(NATS_INVALID_ARG);
    }

    else if (nats_IsStringEmpty(name) && nats_IsStringEmpty(id))
        return new_dotted_subject(newSubject, 2, natsMicroserviceAPIPrefix, verb);
    else if (nats_IsStringEmpty(id))
        return new_dotted_subject(newSubject, 3, natsMicroserviceAPIPrefix, verb, name);
    else
        return new_dotted_subject(newSubject, 4, natsMicroserviceAPIPrefix, verb, name, id);
}

static natsStatus
add_internal_handler(natsMicroservice *m, const char *verb, const char *kind,
                     const char *id, const char *name, natsMsgHandler handler)
{

    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    char *subj = NULL;

    s = new_control_subject(&subj, verb, kind, id);
    if (s == NATS_OK)
    {
        s = natsConnection_Subscribe(&sub, m->nc, subj, handler, m);
    }

    if (s == NATS_OK)
    {
        return NATS_OK;
    }
    else
    {
        natsMicroservice_Stop(m);
        return NATS_UPDATE_ERR_STACK(s);
    }
}

// __verbHandlers generates control handlers for a specific verb. Each request
// generates 3 subscriptions, one for the general verb affecting all services
// written with the framework, one that handles all services of a particular
// kind, and finally a specific service instance.
static natsStatus
add_verb_handlers(natsMicroservice *m, const char *verb, natsMsgHandler handler)
{
    natsStatus s = NATS_OK;
    char name[1024];

    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-all", verb);
        s = add_internal_handler(m, verb, "", "", name, handler);
    }
    if (s == NATS_OK)
    {
        snprintf(name, sizeof(name), "%s-kind", verb);
        s = add_internal_handler(m, verb, m->cfg->name, "", name, handler);
    }
    if (s == NATS_OK)
    {
        s = add_internal_handler(m, verb, m->cfg->name, m->id, verb, handler);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

// name and sep must be a string literal
#define IFOK_attr(_name, _value, _sep)                                  \
    IFOK(s, natsBuf_Append(buf, "\"" _name "\":\"", -1));               \
    IFOK(s, natsBuf_Append(buf, (_value) != NULL ? (_value) : "", -1)); \
    IFOK(s, natsBuf_Append(buf, "\"" _sep, -1));

static natsStatus
marshal_ping(natsBuffer **new_buf, natsMicroservice *m)
{
    natsBuffer *buf = NULL;
    natsStatus s;

    s = natsBuf_Create(&buf, 1024);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = natsBuf_Append(buf, "{", -1);
    IFOK_attr("name", m->cfg->name, ",");
    IFOK_attr("version", m->cfg->version, ",");
    IFOK_attr("id", m->id, ",");
    IFOK_attr("type", natsMicroservicePingResponseType, "");
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
    {
        *new_buf = buf;
        return NATS_OK;
    }
    else
    {
        natsBuf_Destroy(buf);
        return NATS_UPDATE_ERR_STACK(s);
    }
}

static natsStatus
marshal_info(natsBuffer **new_buf, natsMicroserviceInfo *info)
{
    natsBuffer *buf = NULL;
    natsStatus s;

    s = natsBuf_Create(&buf, 4096);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = natsBuf_Append(buf, "{", -1);
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

    if (s == NATS_OK)
    {
        *new_buf = buf;
        return NATS_OK;
    }
    else
    {
        natsBuf_Destroy(buf);
        return NATS_UPDATE_ERR_STACK(s);
    }
}

static natsStatus
marshal_stats(natsBuffer **new_buf, natsMicroserviceStats *stats)
{
    natsBuffer *buf = NULL;
    natsStatus s;
    int i;
    char timebuf[128];
    natsMicroserviceEndpointStats *ep;

    s = natsBuf_Create(&buf, 8 * 1024);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = natsBuf_AppendByte(buf, '{');
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
            // IFOK(s, nats_marshalLong(buf, true, "average_processing_time", ep->average_processing_time_ns));
            IFOK(s, marshal_duration(buf, true, "average_processing_time", ep->average_processing_time_ns));
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
        return NATS_OK;
    }
    else
    {
        natsBuf_Destroy(buf);
        return NATS_UPDATE_ERR_STACK(s);
    }
}

static natsStatus marshal_duration(natsBuffer *out_buf, bool comma, const char *field_name, int64_t d)
{
    // Largest time is 2540400h10m10.000000000s
    char buf[32];
    int w = 32;
    uint64_t u = d;
    bool neg = d < 0;
    int prec;
    natsStatus s = NATS_OK;
    const char *start = (comma ? ",\"" : "\"");

    if (neg)
        u = -u;

    if (u < 1000000000)
    {
        // Special case: if duration is smaller than a second,
        // use smaller units, like 1.2ms
        w--;
        buf[w] = 's';
        w--;
        if (u == 0)
        {
            return natsBuf_Append(out_buf, "0s", 2);
        }
        else if (u < 1000)
        {
            // print nanoseconds
            prec = 0;
            buf[w] = 'n';
        }
        else if (u < 1000000)
        {
            // print microseconds
            prec = 3;
            // U+00B5 'µ' micro sign == 0xC2 0xB5 (in reverse?)
            buf[w] = '\xB5';
            w--; // Need room for two bytes.
            buf[w] = '\xC2';
        }
        else
        {
            // print milliseconds
            prec = 6;
            buf[w] = 'm';
        }
        fmt_frac(buf, w, u, prec, &w, &u);
        w = fmt_int(buf, w, u);
    }
    else
    {
        w--;
        buf[w] = 's';

        fmt_frac(buf, w, u, 9, &w, &u);

        // u is now integer seconds
        w = fmt_int(buf, w, u % 60);
        u /= 60;

        // u is now integer minutes
        if (u > 0)
        {
            w--;
            buf[w] = 'm';
            w = fmt_int(buf, w, u % 60);
            u /= 60;

            // u is now integer hours
            // Stop at hours because days can be different lengths.
            if (u > 0)
            {
                w--;
                buf[w] = 'h';
                w = fmt_int(buf, w, u);
            }
        }
    }

    if (neg)
    {
        w--;
        buf[w] = '-';
    }

    s = natsBuf_Append(out_buf, start, -1);
    IFOK(s, natsBuf_Append(out_buf, field_name, -1));
    IFOK(s, natsBuf_Append(out_buf, "\":\"", -1));
    IFOK(s, natsBuf_Append(out_buf, buf + w, sizeof(buf) - w));
    IFOK(s, natsBuf_Append(out_buf, "\":\"", -1));
    return NATS_UPDATE_ERR_STACK(s);
}

// fmtFrac formats the fraction of v/10**prec (e.g., ".12345") into the
// tail of buf, omitting trailing zeros. It omits the decimal
// point too when the fraction is 0. It returns the index where the
// output bytes begin and the value v/10**prec.
static void fmt_frac(char *buf, int w, uint64_t v, int prec, int *nw, uint64_t *nv)
{
    // Omit trailing zeros up to and including decimal point.
    bool print = false;
    int i;
    int digit;

    for (i = 0; i < prec; i++)
    {
        digit = v % 10;
        print = print || digit != 0;
        if (print)
        {
            w--;
            buf[w] = digit + '0';
        }
        v /= 10;
    }
    if (print)
    {
        w--;
        buf[w] = '.';
    }
    *nw = w;
    *nv = v;
}

// fmtInt formats v into the tail of buf.
// It returns the index where the output begins.
static int fmt_int(char *buf, int w, uint64_t v)
{
    if (v == 0)
    {
        w--;
        buf[w] = '0';
    }
    else
    {
        while (v > 0)
        {
            w--;
            buf[w] = v % 10 + '0';
            v /= 10;
        }
    }
    return w;
}
