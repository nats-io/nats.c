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

#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "conn.h"
#include "hash.h"

#define ASCII_0 (48)
#define ASCII_9 (57)

static char base32DecodeMap[256];

// static const char *base64EncodeURL= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
// static const char *base64EncodeStd= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// static char        base64Padding  = '=';

// static int base64Ints[] = {
//     62, -1, -1, -1, 63, 52, 53, 54, 55, 56,
//     57, 58, 59, 60, 61, -1, -1, -1, -1, -1,
//     -1, -1,  0,  1,  2,  3,  4,  5,  6,  7,
//      8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
//     18, 19, 20, 21, 22, 23, 24, 25, -1, -1,
//     -1, -1, -1, -1, 26, 27, 28, 29, 30, 31,
//     32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
//     42, 43, 44, 45, 46, 47, 48, 49, 50, 51};


// An implementation of crc16 according to CCITT standards for XMODEM.
static uint16_t crc16tab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
};

// parseInt64 expects decimal positive numbers. We
// return -1 to signal error
int64_t
nats_ParseInt64(const char *d, int dLen)
{
    int     i;
    char    dec;
    int64_t pn = 0;
    int64_t n  = 0;

    if (dLen == 0)
        return -1;

    for (i=0; i<dLen; i++)
    {
        dec = d[i];
        if ((dec < ASCII_0) || (dec > ASCII_9))
            return -1;

        n = (n * 10) + (int64_t)(dec - ASCII_0);

        // Check overflow..
        if (n < pn)
            return -1;

        pn = n;
    }

    return n;
}

natsStatus
nats_Trim(char **pres, natsPool *pool, const char *s)
{
    int     len    = 0;
    char    *res   = NULL;
    char    *ptr   = (char*) s;
    char    *start = (char*) s;

    while ((*ptr != '\0') && isspace((unsigned char) *ptr))
        ptr++;

    start = ptr;
    ptr = (char*) (s + strlen(s) - 1);
    while ((ptr != start) && isspace((unsigned char) *ptr))
        ptr--;

    // Compute len of trimmed string
    len = (int) (ptr-start) + 1;

    // Allocate for copy (add 1 for terminating 0)
    res = nats_palloc(pool, len+1);
    if (res == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(res, start, (size_t) len);
    res[len] = '\0';
    *pres = res;

    return NATS_OK;
}

void
nats_Sleep(int64_t millisec)
{
#ifdef _WIN32
    Sleep((DWORD) millisec);
#else
    usleep(millisec * 1000);
#endif
}

const char*
nats_GetBoolStr(bool value)
{
    if (value)
        return "true";

    return "false";
}

void
nats_NormalizeErr(char *error)
{
    int start = 0;
    int end   = 0;
    int len   = (int) strlen(error);
    int i;

    if (natsString_equalC(&nats_ERR, error))
        start = nats_ERR.len;

    for (i=start; i<len; i++)
    {
        if ((error[i] != ' ') && (error[i] != '\''))
            break;
    }

    start = i;
    if (start == len)
    {
        error[0] = '\0';
        return;
    }

    for (end=len-1; end>0; end--)
    {
        char c = error[end];
        if ((c == '\r') || (c == '\n') || (c == '\'') || (c == ' '))
            continue;
        break;
    }

    if (end <= start)
    {
        error[0] = '\0';
        return;
    }

    len = end - start + 1;
    memmove(error, error + start, len);
    error[len] = '\0';
}


natsStatus
nats_parseTime(char *orgStr, int64_t *timeUTC)
{
    natsStatus  s           = NATS_OK;
    char        *dotPos     = NULL;
    char        utcOff[7]   = {'\0'};
    int64_t     nanosecs    = 0;
    char        *p          = NULL;
    char        timeStr[42] = {'\0'};
    char        tmpStr[36]  = {'\0'};
    char        *str        = NULL;
    char        offSign     = '+';
    int         offHours    = 0;
    int         offMin      = 0;
    int         i, l;
    struct tm   tp;

    // Check for "0"
    if (strcmp(orgStr, "0001-01-01T00:00:00Z") == 0)
    {
        *timeUTC = 0;
        return NATS_OK;
    }

    l = (int) strlen(orgStr);
    // The smallest date/time should be: "YYYY:MM:DDTHH:MM:SSZ", which is 20
    // while the longest should be: "YYYY:MM:DDTHH:MM:SS.123456789-12:34" which is 35
    if ((l < 20) || (l > (int) (sizeof(tmpStr) - 1)))
    {
        if (l < 20)
            s = nats_setError(NATS_INVALID_ARG, "time '%s' too small", orgStr);
        else
            s = nats_setError(NATS_INVALID_ARG, "time '%s' too long", orgStr);
        return NATS_UPDATE_ERR_STACK(s);
    }

    // Copy the user provided string in our temporary buffer since we may alter
    // the string as we parse.
    snprintf(tmpStr, sizeof(tmpStr), "%s", orgStr);
    str = (char*) tmpStr;
    memset(&tp, 0, sizeof(struct tm));

    // If ends with 'Z', the time is already UTC
    if ((str[l-1] == 'Z') || (str[l-1] == 'z'))
    {
        // Set the timezone to "+00:00"
        snprintf(utcOff, sizeof(utcOff), "%s", "+00:00");
        str[l-1] = '\0';
    }
    else
    {
        // Make sure the UTC offset comes as "+12:34" (or "-12:34").
        p = str+l-6;
        if ((strlen(p) != 6) || ((*p != '+') && (*p != '-')) || (*(p+3) != ':'))
        {
            s = nats_setError(NATS_INVALID_ARG, "time '%s' has invalid UTC offset", orgStr);
            return NATS_UPDATE_ERR_STACK(s);
        }
        snprintf(utcOff, sizeof(utcOff), "%s", p);
        // Set end of 'str' to beginning of the offset.
        *p = '\0';
    }

    // Check if there is below seconds precision
    dotPos = strstr(str, ".");
    if (dotPos != NULL)
    {
        int64_t val = 0;

        p = (char*) (dotPos+1);
        // Need to recompute the length, since it has changed.
        l = (int) strlen(p);

        val = nats_ParseInt64((const char*) p, l);
        if (val == -1)
        {
            s = nats_setError(NATS_INVALID_ARG, "time '%s' is invalid", orgStr);
            return NATS_UPDATE_ERR_STACK(s);
        }

        for (i=0; i<9-l; i++)
            val *= 10;

        if (val > 999999999)
        {
            s = nats_setError(NATS_INVALID_ARG, "time '%s' second fraction too big", orgStr);
            return NATS_UPDATE_ERR_STACK(s);
        }

        nanosecs = val;
        // Set end of string at the place of the '.'
        *dotPos = '\0';
    }

    snprintf(timeStr, sizeof(timeStr), "%s%s", str, utcOff);
    if (sscanf(timeStr, "%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d",
               &tp.tm_year, &tp.tm_mon, &tp.tm_mday, &tp.tm_hour, &tp.tm_min, &tp.tm_sec,
               &offSign, &offHours, &offMin) == 9)
    {
        int64_t res = 0;
        int64_t off = 0;

        tp.tm_year -= 1900;
        tp.tm_mon--;
        tp.tm_isdst = 0;
#ifdef _WIN32
        res = (int64_t) _mkgmtime64(&tp);
#else
        res = (int64_t) timegm(&tp);
#endif
        if (res == -1)
        {
            s = nats_setError(NATS_ERR, "error parsing time '%s'", orgStr);
            return NATS_UPDATE_ERR_STACK(s);
        }
        // Compute the offset
        off = (int64_t) ((offHours * 60 * 60) + (offMin * 60));
        // If UTC offset is positive, then we need to remove to get down to UTC time,
        // where as if negative, we need to add the offset to get up to UTC time.
        if (offSign == '+')
            off *= (int64_t) -1;

        res *= (int64_t) 1E9;
        res += (off * (int64_t) 1E9);
        res += nanosecs;
        *timeUTC = res;
    }
    else
    {
        s = nats_setError(NATS_ERR, "error parsing time '%s'", orgStr);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

void
nats_Base32_Init(void)
{
    const char  *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    int         alphaLen  = (int) strlen(alphabet);
    int         i;

    for (i=0; i<(int)sizeof(base32DecodeMap); i++)
        base32DecodeMap[i] = (char) 0xFF;

    for (i=0; i<alphaLen; i++)
        base32DecodeMap[(int)alphabet[i]] = (char) i;
}

natsStatus
nats_Base32_DecodeString(const char *src, char *dst, int dstMax, int *dstLen)
{
    char        *ptr      = (char*) src;
    int         n         = 0;
    bool        done      = false;
    int         srcLen    = (int) strlen(src);
    int         remaining = srcLen;

    *dstLen = 0;

    while (remaining > 0)
    {
        char dbuf[8];
        int  dLen = 8;
        int  j;
        int  needs;

        for (j=0; j<8; )
        {
            int in;

            if (remaining == 0)
            {
                dLen = j;
                done  = true;
                break;
            }

            in = (int) *ptr;
            ptr++;
            remaining--;

            dbuf[j] = base32DecodeMap[in];
            // If invalid character, report the position but as the number of character
            // since beginning, not array index.
            if (dbuf[j] == (char) 0xFF)
                return nats_setError(NATS_ERR, "base32: invalid data at location %d", srcLen - remaining);
            j++;
        }

        needs = 0;
        switch (dLen)
        {
            case 8: needs = 5; break;
            case 7: needs = 4; break;
            case 5: needs = 3; break;
            case 4: needs = 2; break;
            case 2: needs = 1; break;
        }
        if (n+needs > dstMax)
            return nats_setError(NATS_INSUFFICIENT_BUFFER, "based32: needs %d bytes, max is %d", n+needs, dstMax);

        if (dLen == 8)
            dst[4] = dbuf[6]<<5 | dbuf[7];
        if (dLen >= 7)
            dst[3] = dbuf[4]<<7 | dbuf[5]<<2 | dbuf[6]>>3;
        if (dLen >= 5)
            dst[2] = dbuf[3]<<4 | dbuf[4]>>1;
        if (dLen >= 4)
            dst[1] = dbuf[1]<<6 | dbuf[2]<<1 | dbuf[3]>>4;
        if (dLen >= 2)
            dst[0] = dbuf[0]<<3 | dbuf[1]>>2;

        n += needs;

        if (!done)
            dst += 5;
    }

    *dstLen = n;

    return NATS_OK;
}

// static natsStatus
// _base64Encode(const char *map, bool padding, const unsigned char *src, int srcLen, char **pDest)
// {
//     char        *dst   = NULL;
//     int         dstLen = 0;
//     int         n;
//     int         di = 0;
//     int         si = 0;
//     int         remain = 0;
//     uint32_t    val = 0;

//     *pDest = NULL;

//     if ((src == NULL) || (srcLen == 0))
//         return NATS_OK;

//     n = srcLen;
//     if (padding)
//         dstLen = (n + 2) / 3 * 4;
//     else
//         dstLen = (n * 8 + 5) / 6;
//     dst = NATS_CALLOC(1, dstLen + 1);
//     if (dst == NULL)
//         return nats_setDefaultError(NATS_NO_MEMORY);

//     n = ((srcLen / 3) * 3);
//     for (si = 0; si < n; )
//     {
//         // Convert 3x 8bit source bytes into 4 bytes
//         val = (uint32_t)(src[si+0])<<16 | (uint32_t)(src[si+1])<<8 | (uint32_t)(src[si+2]);

//         dst[di+0] = map[val >> 18 & 0x3F];
//         dst[di+1] = map[val >> 12 & 0x3F];
//         dst[di+2] = map[val >>  6 & 0x3F];
//         dst[di+3] = map[val       & 0x3F];

//         si += 3;
//         di += 4;
//     }

//     remain = srcLen - si;
//     if (remain == 0)
//     {
//         *pDest = dst;
//         return NATS_OK;
//     }

//     // Add the remaining small block
//     val = (uint32_t)src[si+0] << 16;
//     if (remain == 2)
//         val |= (uint32_t)src[si+1] << 8;

//     dst[di+0] = map[val >> 18 & 0x3F];
//     dst[di+1] = map[val >> 12 & 0x3F];

//     if (remain == 2)
//     {
//         dst[di+2] = map[val >> 6 & 0x3F];
//         if (padding)
//             dst[di+3] = base64Padding;
//     }
//     else if (padding && (remain == 1))
//     {
//         dst[di+2] = base64Padding;
//         dst[di+3] = base64Padding;
//     }

//     *pDest = dst;

//     return NATS_OK;
// }

// natsStatus
// nats_Base64RawURL_EncodeString(const unsigned char *src, int srcLen, char **pDest)
// {
//     natsStatus s = _base64Encode(base64EncodeURL, false, src, srcLen, pDest);
//     return NATS_UPDATE_ERR_STACK(s);
// }

// natsStatus
// nats_Base64_Encode(const unsigned char *src, int srcLen, char **pDest)
// {
//     natsStatus s = _base64Encode(base64EncodeStd, true, src, srcLen, pDest);
//     return NATS_UPDATE_ERR_STACK(s);
// }

// static bool
// _base64IsValidChar(char c, bool paddingOk)
// {
//     if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
//         || (c >= '0' && c <= '9') || c == '+' || c == '/')
//     {
//         return true;
//     }
//     if (c == base64Padding && paddingOk)
//         return true;
//     return false;
// }

// natsStatus
// nats_Base64_DecodeLen(const char *src, int *srcLen, int *dstLen)
// {
//     int l;
//     int dl;
//     int i;

//     if (nats_isCStringEmpty(src))
//         return nats_setError(NATS_INVALID_ARG, "%s", "base64 content cannot be empty");

//     l = (int) strlen(src);
//     if (l % 4 != 0)
//         return nats_setError(NATS_INVALID_ARG, "invalid base64 length: %d", l);

//     dl = l / 4 * 3;
//     for (i=0; i<l; i++)
//     {
//         if (!_base64IsValidChar(src[i], i>=l-2))
//             return nats_setError(NATS_INVALID_ARG, "invalid base64 character: '%c'", src[i]);

//         if (src[i] == base64Padding)
//             dl--;
//     }

//     *srcLen = l;
//     *dstLen = dl;
//     return NATS_OK;
// }

// void
// nats_Base64_DecodeInPlace(const char *src, int l, unsigned char *dst)
// {
//     int i, j, v;

//     for (i=0, j=0; i<l; i+=4, j+=3)
//     {
//         v = base64Ints[src[i]-43];
//         v = (v << 6) | base64Ints[src[i+1]-43];
//         v = (src[i+2] == base64Padding ? v << 6 : (v << 6) | base64Ints[src[i+2]-43]);
//         v = (src[i+3] == base64Padding ? v << 6 : (v << 6) | base64Ints[src[i+3]-43]);

//         dst[j] = (v >> 16) & 0xFF;
//         if (src[i+2] != base64Padding)
//             dst[j+1] = (v >> 8) & 0xFF;
//         if (src[i+3] != base64Padding)
//             dst[j+2] = v & 0xFF;
//     }
// }

// natsStatus
// nats_Base64_Decode(const char *src, unsigned char **dst, int *dstLen)
// {
//     natsStatus  s;
//     int         sl = 0;

//     *dst = NULL;
//     *dstLen = 0;

//     s = nats_Base64_DecodeLen(src, &sl, dstLen);
//     if (STILL_OK(s))
//     {
//         *dst = (unsigned char*) NATS_MALLOC(*dstLen);
//         if (*dst == NULL)
//         {
//             *dstLen = 0;
//             return nats_setDefaultError(NATS_NO_MEMORY);
//         }
//         nats_Base64_DecodeInPlace(src, sl, *dst);
//     }
//     return NATS_UPDATE_ERR_STACK(s);
// }

// Returns the 2-byte crc for the data provided.
uint16_t
nats_CRC16_Compute(unsigned char *data, int len)
{
    uint16_t    crc = 0;
    int         i;

    for (i=0; i<len; i++)
        crc = ((crc << 8) & 0xFFFF) ^ crc16tab[((crc>>8)^(uint16_t)(data[i]))&0x00FF];

    return crc;
}

// Checks the calculated crc16 checksum for data against the expected.
bool
nats_CRC16_Validate(unsigned char *data, int len, uint16_t expected)
{
    uint16_t crc = nats_CRC16_Compute(data, len);
    return crc == expected;
}

// natsStatus
// nats_ReadFile(natsBuf **buffer, int initBufSize, const char *fn)
// {
//     natsStatus  s;
//     FILE        *f      = NULL;
//     natsBuf  *buf    = NULL;
//     uint8_t     *ptr    = NULL;
//     int         total   = 0;

//     if ((initBufSize <= 0) || nats_isCStringEmpty(fn))
//         return nats_setDefaultError(NATS_INVALID_ARG);

//     f = fopen(fn, "r");
//     if (f == NULL)
//         return nats_setError(NATS_ERR, "error opening file '%s': %s", fn, strerror(errno));

//     s = natsBuf_Create(&buf, initBufSize);
//     if (STILL_OK(s))
//         ptr = natsBuf_data(buf);
//     while (STILL_OK(s))
//     {
//         int r = (int) fread(ptr, 1, natsBuf_Available(buf), f);
//         if (r == 0)
//             break;

//         total += r;
//         natsBuf_MoveTo(buf, total);
//         if (natsBuf_Available(buf) == 0)
//             s = natsBuf_Expand(buf, natsBuf_Capacity(buf)*2);
//         if (STILL_OK(s))
//             ptr = natsBuf_data(buf) + total;
//     }

//     // Close file. If there was an error, do not report possible closing error
//     // as the actual error
//     if (s != NATS_OK)
//         fclose(f);
//     else if (fclose(f) != 0)
//         s = nats_setError(NATS_ERR, "error closing file '%s': '%s", fn, strerror(errno));

//     IFOK(s, natsBuf_addB(buf, '\0'));

//     if (STILL_OK(s))
//     {
//         *buffer = buf;
//     }
//     else if (buf != NULL)
//     {
//         memset(natsBuf_data(buf), 0, natsBuf_Capacity(buf));
//         natsBuf_Destroy(buf);
//     }

//     return NATS_UPDATE_ERR_STACK(s);
// }

void
nats_FreeAddrInfo(struct addrinfo *res)
{
    // Calling freeaddrinfo(NULL) is undefined behavior.
    if (res == NULL)
        return;

    freeaddrinfo(res);
}

bool
nats_HostIsIP(const char *host)
{
    struct addrinfo hint;
    struct addrinfo *res = NULL;
    bool            isIP = true;

    memset(&hint, '\0', sizeof hint);

    hint.ai_family = PF_UNSPEC;
    hint.ai_flags = AI_NUMERICHOST;

    if (getaddrinfo(host, NULL, &hint, &res) != 0)
        isIP = false;

    nats_FreeAddrInfo(res);

    return isIP;
}

static bool
_isLineAnHeader(const char *ptr)
{
    char    *last   = NULL;
    int     len     = 0;
    int     count   = 0;
    bool    done    = false;

    // We are looking for a header. Based on the Go client's regex,
    // the strict requirement is that it ends with at least 3 consecutive
    // `-` characters. It must also have 3 consecutive `-` before that.
    // So the minimum size would be 6.
    len = (int) strlen(ptr);
    if (len < 6)
        return false;

    // First make sure that we have at least 3 `-` at the end.
    last = (char*) (ptr + len - 1);

    while ((*last == '-') && (last != ptr))
    {
        count++;
        last--;
        if (count == 3)
            break;
    }
    if (count != 3)
        return false;

    // Now from that point and going backward, we consider
    // to have proper header if we find again 3 consecutive
    // dashes.
    count = 0;
    while (!done)
    {
        if (*last == '-')
        {
            // We have at least `---`, we are done.
            if (++count == 3)
                return true;
        }
        else
        {
            // Reset.. we need 3 consecutive dashes
            count = 0;
        }
        if (last == ptr)
            done = true;
        else
            last--;
    }
    // If we are here, it means we did not find `---`
    return false;
}

bool nats_isSubjectValid(const uint8_t *subject, size_t len, bool wcAllowed)
{
    int     i       = 0;
    int     lastDot = -1;
    char    c;

    if (nats_isCStringEmpty((const char *)subject))
        return false;

    for (i=0; i<(int)len ; i++)
    {
        c = subject[i];
        if (isspace((unsigned char) c))
            return false;

        if (c == '.')
        {
            if ((i == (int)len-1) || (i == lastDot+1))
                return false;

            // If the last token was 1 character long...
            if (i == lastDot+2)
            {
                char prevToken = subject[i-1];

                // If that token was `>`, then it is not a valid subject,
                // regardless if wildcards are allowed or not.
                if (prevToken == '>')
                    return false;
                else if (!wcAllowed && (prevToken == '*'))
                    return false;
            }

            lastDot = i;
        }

        // Check the last character to see if it is a wildcard. Others
        // are handled when processing the next '.' character (since
        // if not a token of their own, they are not considered wildcards).
        if (i == (int)len-1)
        {
            // If they are a token of their own, that is, the preceeding
            // character is the `.` or they are the first and only character,
            // then the result will depend if wilcards are allowed or not.
            if (((c == '>') || (c == '*')) && (i == lastDot+1))
                return wcAllowed;
        }
    }
    return true;
}


// allocates a sufficiently large buffer and formats the strings into it, as a
// ["unencoded-string-0","unencoded-string-1",...]. For an empty array of
// strings returns "[]".
// natsStatus nats_formatStringArray(char **out, const char **strings, int count)
// {
//     natsStatus s = NATS_OK;
//     natsBuf buf;
//     int len = 0;
//     int  i;

//     len++; // For the '['
//     for (i = 0; i < count; i++)
//     {
//         len += 2; // For the quotes
//         if (i > 0)
//             len++; // For the ','
//         if (strings[i] == NULL)
//             len += strlen("(null)");
//         else
//             len += strlen(strings[i]);
//     }
//     len++; // For the ']'
//     len++; // For the '\0'

//     s = natsBuf_InitCalloc(&buf, len);

//     natsBuf_addB(&buf, '[');
//     for (i = 0; (STILL_OK(s)) && (i < count); i++)
//     {
//         if (i > 0)
//         {
//             IFOK(s, natsBuf_addB(&buf, ','));
//         }
//         IFOK(s, natsBuf_addB(&buf, '"'));
//         if (strings[i] == NULL)
//         {
//             IFOK(s, natsBuf_addCString(&buf, "(null)"));
//         }
//         else
//         {
//             IFOK(s, natsBuf_addCString(&buf, strings[i]));
//         }
//         IFOK(s, natsBuf_addB(&buf, '"'));
//     }

//     IFOK(s, natsBuf_addB(&buf, ']'));
//     IFOK(s, natsBuf_addB(&buf, '\0'));
    
//     if (s != NATS_OK)
//     {
//         natsBuf_Destroy(&buf);
//         return s;
//     }

//     *out = (char*)natsBuf_data(&buf);
//     return NATS_OK;
// }

