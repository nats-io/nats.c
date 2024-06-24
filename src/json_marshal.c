// Copyright 2015-2023 The NATS Authors
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

#include "json.h"
#include "conn.h"
#include "opts.h"

natsStatus
nats_EncodeTimeUTC(char *buf, size_t bufLen, int64_t timeUTC)
{
    int64_t t = timeUTC / (int64_t)1E9;
    int64_t ns = timeUTC - ((int64_t)t * (int64_t)1E9);
    struct tm tp;
    int n;

    // We will encode at most: "YYYY:MM:DDTHH:MM:SS.123456789+12:34"
    // so we need at least 35+1 characters.
    if (bufLen < 36)
        return nats_setError(NATS_INVALID_ARG,
                             "buffer to encode UTC time is too small (%d), needs 36",
                             (int)bufLen);

    if (timeUTC == 0)
    {
        snprintf(buf, bufLen, "%s", "0001-01-01T00:00:00Z");
        return NATS_OK;
    }

    memset(&tp, 0, sizeof(struct tm));
#ifdef _WIN32
    _gmtime64_s(&tp, (const __time64_t *)&t);
#else
    gmtime_r((const time_t *)&t, &tp);
#endif
    n = (int)strftime(buf, bufLen, "%FT%T", &tp);
    if (n == 0)
        return nats_setDefaultError(NATS_ERR);

    if (ns > 0)
    {
        char nsBuf[15];
        int i, nd;

        nd = snprintf(nsBuf, sizeof(nsBuf), ".%" PRId64, ns);
        for (; (nd > 0) && (nsBuf[nd - 1] == '0');)
            nd--;

        for (i = 0; i < nd; i++)
            *(buf + n++) = nsBuf[i];
    }
    *(buf + n) = 'Z';
    *(buf + n + 1) = '\0';

    return NATS_OK;
}

static natsStatus
_marshalLongVal(natsBuf *buf, bool comma, const char *fieldName, bool l, int64_t lval, uint64_t uval)
{
    natsStatus s = NATS_OK;
    char temp[32];
    const char *start = (comma ? ",\"" : "\"");

    if (l)
        snprintf(temp, sizeof(temp), "%" PRId64, lval);
    else
        snprintf(temp, sizeof(temp), "%" PRIi64, uval);

    s = natsBuf_addCString(buf, start);
    IFOK(s, natsBuf_addCString(buf, fieldName));
    IFOK(s, natsBuf_addCString(buf, "\":"));
    IFOK(s, natsBuf_addCString(buf, temp));

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_marshalLong(natsBuf *buf, bool comma, const char *fieldName, int64_t lval)
{
    natsStatus s = _marshalLongVal(buf, comma, fieldName, true, lval, 0);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
nats_marshalULong(natsBuf *buf, bool comma, const char *fieldName, uint64_t uval)
{
    natsStatus s = _marshalLongVal(buf, comma, fieldName, false, 0, uval);
    return NATS_UPDATE_ERR_STACK(s);
}

// fmtFrac formats the fraction of v/10**prec (e.g., ".12345") into the
// tail of buf, omitting trailing zeros. It omits the decimal
// point too when the fraction is 0. It returns the index where the
// output bytes begin and the value v/10**prec.
static void
fmt_frac(uint8_t *buf, int w, uint64_t v, int prec, int *nw, uint64_t *nv)
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
static int
fmt_int(uint8_t *buf, int w, uint64_t v)
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

natsStatus
nats_marshalDuration(natsBuf *out_buf, bool comma, const char *field_name, int64_t d)
{
    // Largest time is 2540400h10m10.000000000s
    uint8_t buf[32];
    int w = 32;
    bool neg = d < 0;
    uint64_t u = (uint64_t)(neg ? -d : d);
    int prec;
    natsStatus s = NATS_OK;
    const char *start = (comma ? ",\"" : "\"");

    if (u < 1000000000)
    {
        // Special case: if duration is smaller than a second,
        // use smaller units, like 1.2ms
        w--;
        buf[w] = 's';
        w--;
        if (u == 0)
        {
            s = natsBuf_addCString(out_buf, start);
            IFOK(s, natsBuf_addCString(out_buf, field_name));
            IFOK(s, natsBuf_addCString(out_buf, "\":\"0s\""));
            return NATS_UPDATE_ERR_STACK(s);
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

    s = natsBuf_addCString(out_buf, start);
    IFOK(s, natsBuf_addCString(out_buf, field_name));
    IFOK(s, natsBuf_addCString(out_buf, "\":\""));
    IFOK(s, natsBuf_addBB(out_buf, buf + w, sizeof(buf) - w));
    IFOK(s, natsBuf_addCString(out_buf, "\""));
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus nats_marshalConnect(natsString **out, natsConnection *nc, const char *user,
                               const char *pwd, const char *token, const char *name,
                               bool hdrs, bool noResponders)
{
    size_t need = 0;
    size_t cap = 0;
    char *buf = NULL;

    for (int i = 0; i < 2; i++)
    {
        if (need != 0)
        {
            buf = nats_palloc(nc->connectPool, need);
            if (buf == NULL)
                return nats_setDefaultError(NATS_NO_MEMORY);
            cap = need;
        }

        need = snprintf(buf, cap,
                        "CONNECT {\"verbose\":%s,\"pedantic\":%s,%s%s%s%s%s%s%s%s%s\"tls_required\":%s,"
                        "\"name\":\"%s\",\"lang\":\"%s\",\"version\":\"%s\",\"protocol\":%d,\"echo\":%s,"
                        "\"headers\":%s,\"no_responders\":%s}%s",
                        nats_GetBoolStr(nc->opts->proto.verbose),
                        nats_GetBoolStr(nc->opts->proto.pedantic),
                        (user != NULL ? "\"user\":\"" : ""),
                        (user != NULL ? user : ""),
                        (user != NULL ? "\"," : ""),
                        (pwd != NULL ? "\"pass\":\"" : ""),
                        (pwd != NULL ? pwd : ""),
                        (pwd != NULL ? "\"," : ""),
                        (token != NULL ? "\"auth_token\":\"" : ""),
                        (token != NULL ? token : ""),
                        (token != NULL ? "\"," : ""),
                        nats_GetBoolStr(nc->opts->secure.secure),
                        (name != NULL ? name : ""),
                        CLangString, NATS_VERSION_STRING,
                        CLIENT_PROTO_INFO,
                        nats_GetBoolStr(!nc->opts->proto.noEcho),
                        nats_GetBoolStr(hdrs),
                        nats_GetBoolStr(noResponders),
                        _CRLF_);
        need++; // For '\0'
    }

    *out = nats_palloc(nc->connectPool, sizeof(natsString));
    (*out)->data = (uint8_t *)buf;
    (*out)->len = need - 1;
    return NATS_OK;
}

// natsStatus
// nats_marshalMetadata(natsBuf *buf, bool comma, const char *fieldName, natsMetadata md)
// {
//     natsStatus s = NATS_OK;
//     int i;
//     const char *start = (comma ? ",\"" : "\"");

//     if (md.Count <= 0)
//         return NATS_OK;

//     IFOK(s, natsBuf_addCString(buf, start));
//     IFOK(s, natsBuf_addCString(buf, fieldName));
//     IFOK(s, natsBuf_Append(buf, (const uint8_t *)"\":{", 3));
//     for (i = 0; (STILL_OK(s)) && (i < md.Count); i++)
//     {
//         IFOK(s, natsBuf_addB(buf, '"'));
//         IFOK(s, natsBuf_addCString(buf, md.List[i * 2]));
//         IFOK(s, natsBuf_Append(buf, (const uint8_t *)"\":\"", 3));
//         IFOK(s, natsBuf_addCString(buf, md.List[i * 2 + 1]));
//         IFOK(s, natsBuf_addB(buf, '"'));

//         if (i != md.Count - 1)
//             IFOK(s, natsBuf_addB(buf, ','));
//     }
//     IFOK(s, natsBuf_addB(buf, '}'));
//     return NATS_OK;
// }
