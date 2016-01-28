// Copyright 2015 Apcera Inc. All rights reserved.

#include <string.h>
#include <stdio.h>

#include "natsp.h"
#include "conn.h"
#include "util.h"
#include "mem.h"

// cloneMsgArg is used when the split buffer scenario has the pubArg in the existing read buffer, but
// we need to hold onto it into the next read.
static natsStatus
_cloneMsgArg(natsConnection *nc)
{
    natsStatus  s;
    int         subjLen = natsBuf_Len(nc->ps->ma.subject);

    s = natsBuf_InitWithBackend(&(nc->ps->argBufRec),
                                nc->ps->scratch,
                                0,
                                sizeof(nc->ps->scratch));
    if (s == NATS_OK)
    {
        nc->ps->argBuf = &(nc->ps->argBufRec);

        s = natsBuf_Append(nc->ps->argBuf,
                           natsBuf_Data(nc->ps->ma.subject),
                           natsBuf_Len(nc->ps->ma.subject));
        if (s == NATS_OK)
        {
            natsBuf_Destroy(nc->ps->ma.subject);
            nc->ps->ma.subject = NULL;

            s = natsBuf_InitWithBackend(&(nc->ps->ma.subjectRec),
                                        nc->ps->scratch,
                                        subjLen,
                                        subjLen);
            if (s == NATS_OK)
                nc->ps->ma.subject = &(nc->ps->ma.subjectRec);
        }
    }
    if ((s == NATS_OK) && (nc->ps->ma.reply != NULL))
    {
        s = natsBuf_Append(nc->ps->argBuf,
                           natsBuf_Data(nc->ps->ma.reply),
                           natsBuf_Len(nc->ps->ma.reply));
        if (s == NATS_OK)
        {
            int replyLen = natsBuf_Len(nc->ps->ma.reply);

            natsBuf_Destroy(nc->ps->ma.reply);
            nc->ps->ma.reply = NULL;

            s = natsBuf_InitWithBackend(&(nc->ps->ma.replyRec),
                                        nc->ps->scratch + subjLen,
                                        replyLen,
                                        replyLen);
            if (s == NATS_OK)
                nc->ps->ma.reply = &(nc->ps->ma.replyRec);

        }
    }

    return s;
}

struct slice
{
    char    *start;
    int     len;
};

static natsStatus
_processMsgArgs(natsConnection *nc, char *buf, int bufLen)
{
    natsStatus      s       = NATS_OK;
    bool            started = false;
    char            *start  = buf;
    int             len     = 0;
    int             index   = 0;
    int             i;
    char            b;
    struct slice    slices[4];

    for (i = 0; i < bufLen; i++)
    {
        b = buf[i];

        if (((b == ' ') || (b == '\t') || (b == '\r') || (b == '\n'))
            && started)
        {
            slices[index].start = start;
            slices[index].len   = len;
            index++;

            started = false;
            len = 0;
        }
        else
        {
            if (!started)
            {
                start   = buf + i;
                started = true;
            }
            len++;
        }
    }
    if (started)
    {
        slices[index].start = start;
        slices[index].len   = len;
        index++;
    }

    if ((index == 3) || (index == 4))
    {
        int maSizeIndex = 2;

        s = natsBuf_InitWithBackend(&(nc->ps->ma.subjectRec),
                                    slices[0].start,
                                    slices[0].len,
                                    slices[0].len);
        if (s == NATS_OK)
        {
            nc->ps->ma.subject = &(nc->ps->ma.subjectRec);

            nc->ps->ma.sid   = nats_ParseInt64(slices[1].start, slices[1].len);

            if (index == 3)
            {
                nc->ps->ma.reply = NULL;
            }
            else
            {
                s = natsBuf_InitWithBackend(&(nc->ps->ma.replyRec),
                                            slices[2].start,
                                            slices[2].len,
                                            slices[2].len);
                if (s == NATS_OK)
                {
                    nc->ps->ma.reply = &(nc->ps->ma.replyRec);
                    maSizeIndex = 3;
                }
            }
        }
        if (s == NATS_OK)
            nc->ps->ma.size = (int) nats_ParseInt64(slices[maSizeIndex].start,
                                                    slices[maSizeIndex].len);
    }
    else
    {
        snprintf(nc->errStr, sizeof(nc->errStr), "processMsgArgs Parse Error: '%.*s'",
                 bufLen, buf);
        s = NATS_PROTOCOL_ERROR;
    }
    if (nc->ps->ma.sid < 0)
    {
        snprintf(nc->errStr, sizeof(nc->errStr), "processMsgArgs Bad or Missing Sid: '%.*s'",
                 bufLen, buf);
        s = NATS_PROTOCOL_ERROR;
    }
    if (nc->ps->ma.size < 0)
    {
        snprintf(nc->errStr, sizeof(nc->errStr), "processMsgArgs Bad or Missing Size: '%.*s'",
                 bufLen, buf);
        s = NATS_PROTOCOL_ERROR;
    }

    return s;
}

// parse is the fast protocol parser engine.
natsStatus
natsParser_Parse(natsConnection *nc, char* buf, int bufLen)
{
    natsStatus  s = NATS_OK;
    int         i;
    char        b;

    for (i = 0; (s == NATS_OK) && (i < bufLen); i++)
    {
        b = buf[i];

        switch (nc->ps->state)
        {
            case OP_START:
            {
                switch (b)
                {
                    case 'M':
                    case 'm':
                        nc->ps->state = OP_M;
                        break;
                    case 'P':
                    case 'p':
                        nc->ps->state = OP_P;
                        break;
                    case '+':
                        nc->ps->state = OP_PLUS;
                        break;
                    case '-':
                        nc->ps->state = OP_MINUS;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_M:
            {
                switch (b)
                {
                    case 'S':
                    case 's':
                        nc->ps->state = OP_MS;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MS:
            {
                switch (b)
                {
                    case 'G':
                    case 'g':
                        nc->ps->state = OP_MSG;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MSG:
            {
                switch (b)
                {
                    case ' ':
                    case '\t':
                        nc->ps->state = OP_MSG_SPC;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MSG_SPC:
            {
                switch (b)
                {
                    case ' ':
                    case '\t':
                        continue;
                    default:
                        nc->ps->state      = MSG_ARG;
                        nc->ps->afterSpace = i;
                        break;
                }
                break;
            }
            case MSG_ARG:
            {
                switch (b)
                {
                    case '\r':
                        nc->ps->drop = 1;
                        break;
                    case '\n':
                    {
                        char    *start = NULL;
                        int     len    = 0;

                        if (nc->ps->argBuf != NULL)
                        {
                            start = natsBuf_Data(nc->ps->argBuf);
                            len   = natsBuf_Len(nc->ps->argBuf);
                        }
                        else
                        {
                            start = buf + nc->ps->afterSpace;
                            len   = (i - nc->ps->drop) - nc->ps->afterSpace;
                        }

                        s = _processMsgArgs(nc, start, len);
                        if (s == NATS_OK)
                        {
                            nc->ps->drop        = 0;
                            nc->ps->afterSpace  = i+1;
                            nc->ps->state       = MSG_PAYLOAD;

                            // jump ahead with the index. If this overruns
                            // what is left we fall out and process split
                            // buffer.
                            i = nc->ps->afterSpace + nc->ps->ma.size - 1;
                        }
                        break;
                    }
                    default:
                    {
                        if (nc->ps->argBuf != NULL)
                            s = natsBuf_AppendByte(nc->ps->argBuf, b);
                        break;
                    }
                }
                break;
            }
            case MSG_PAYLOAD:
            {
                bool done = false;

                if (nc->ps->msgBuf != NULL)
                {
                    if (natsBuf_Len(nc->ps->msgBuf) >= nc->ps->ma.size)
                    {
                        s = natsConn_processMsg(nc,
                                                natsBuf_Data(nc->ps->msgBuf),
                                                natsBuf_Len(nc->ps->msgBuf));
                        done = true;
                    }
                    else
                    {
                        // copy as much as we can to the buffer and skip ahead.
                        int toCopy = nc->ps->ma.size - natsBuf_Len(nc->ps->msgBuf);
                        int avail  = bufLen - i;

                        if (avail < toCopy)
                            toCopy = avail;

                        if (toCopy > 0)
                        {
                            s = natsBuf_Append(nc->ps->msgBuf, buf+i, toCopy);
                            if (s == NATS_OK)
                                i += toCopy - 1;
                        }
                        else
                        {
                            s = natsBuf_AppendByte(nc->ps->msgBuf, b);
                        }
                    }
                }
                else if (i-nc->ps->afterSpace >= nc->ps->ma.size)
                {
                    char    *start  = NULL;
                    int     len     = 0;

                    start = buf + nc->ps->afterSpace;
                    len   = (i - nc->ps->afterSpace);

                    s = natsConn_processMsg(nc, start, len);

                    done = true;
                }

                if (done)
                {
                    natsBuf_Destroy(nc->ps->argBuf);
                    nc->ps->argBuf = NULL;
                    natsBuf_Destroy(nc->ps->msgBuf);
                    nc->ps->msgBuf = NULL;
                    nc->ps->state = MSG_END;
                }

                break;
            }
            case MSG_END:
            {
                switch (b)
                {
                    case '\n':
                        nc->ps->drop        = 0;
                        nc->ps->afterSpace  = i+1;
                        nc->ps->state       = OP_START;
                        break;
                    default:
                        continue;
                }
                break;
            }
            case OP_PLUS:
            {
                switch (b)
                {
                    case 'O':
                    case 'o':
                        nc->ps->state = OP_PLUS_O;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PLUS_O:
            {
                switch (b)
                {
                    case 'K':
                    case 'k':
                        nc->ps->state = OP_PLUS_OK;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PLUS_OK:
            {
                switch (b)
                {
                    case '\n':
                        natsConn_processOK(nc);
                        nc->ps->drop  = 0;
                        nc->ps->state = OP_START;
                        break;
                }
                break;
            }
            case OP_MINUS:
            {
                switch (b)
                {
                    case 'E':
                    case 'e':
                        nc->ps->state = OP_MINUS_E;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MINUS_E:
            {
                switch (b)
                {
                    case 'R':
                    case 'r':
                        nc->ps->state = OP_MINUS_ER;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MINUS_ER:
            {
                switch (b)
                {
                    case 'R':
                    case 'r':
                        nc->ps->state = OP_MINUS_ERR;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MINUS_ERR:
            {
                switch (b)
                {
                    case ' ':
                    case '\t':
                        nc->ps->state = OP_MINUS_ERR_SPC;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_MINUS_ERR_SPC:
            {
                switch (b)
                {
                    case ' ':
                    case '\t':
                        continue;
                    default:
                        nc->ps->state       = MINUS_ERR_ARG;
                        nc->ps->afterSpace  = i;
                        break;
                }
                break;
            }
            case MINUS_ERR_ARG:
            {
                switch (b)
                {
                    case '\r':
                        nc->ps->drop = 1;
                        break;
                    case '\n':
                    {
                        char    *start = NULL;
                        int     len    = 0;

                        if (nc->ps->argBuf != NULL)
                        {
                            start = natsBuf_Data(nc->ps->argBuf);
                            len   = natsBuf_Len(nc->ps->argBuf);
                        }
                        else
                        {
                            start = buf + nc->ps->afterSpace;
                            len   = (i - nc->ps->drop) - nc->ps->afterSpace;
                        }

                        natsConn_processErr(nc, start, len);

                        nc->ps->drop        = 0;
                        nc->ps->afterSpace  = i+1;
                        nc->ps->state       = OP_START;

                        if (nc->ps->argBuf != NULL)
                        {
                            natsBuf_Destroy(nc->ps->argBuf);
                            nc->ps->argBuf = NULL;
                        }

                        break;
                    }
                    default:
                    {
                        if (nc->ps->argBuf != NULL)
                            s = natsBuf_AppendByte(nc->ps->argBuf, b);

                        break;
                    }
                }
                break;
            }
            case OP_P:
            {
                switch (b)
                {
                    case 'I':
                    case 'i':
                        nc->ps->state = OP_PI;
                        break;
                    case 'O':
                    case 'o':
                        nc->ps->state = OP_PO;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PO:
            {
                switch (b)
                {
                    case 'N':
                    case 'n':
                        nc->ps->state = OP_PON;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PON:
            {
                switch (b)
                {
                    case 'G':
                    case 'g':
                        nc->ps->state = OP_PONG;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PONG:
            {
                switch (b)
                {
                    case '\n':
                        natsConn_processPong(nc);

                        nc->ps->drop        = 0;
                        nc->ps->afterSpace  = i+1;
                        nc->ps->state       = OP_START;
                        break;
                }
                break;
            }
            case OP_PI:
            {
                switch (b)
                {
                    case 'N':
                    case 'n':
                        nc->ps->state = OP_PIN;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PIN:
            {
                switch (b)
                {
                    case 'G':
                    case 'g':
                        nc->ps->state = OP_PING;
                        break;
                    default:
                        goto parseErr;
                }
                break;
            }
            case OP_PING:
            {
                switch (b)
                {
                    case '\n':
                        natsConn_processPing(nc);

                        nc->ps->drop        = 0;
                        nc->ps->afterSpace  = i+1;
                        nc->ps->state       = OP_START;
                        break;
                }
                break;
            }
            default:
                goto parseErr;
        }
    }

    // Check for split buffer scenarios
    if ((s == NATS_OK)
        && ((nc->ps->state == MSG_ARG) || (nc->ps->state == MINUS_ERR_ARG))
        && (nc->ps->argBuf == NULL))
    {
        s = natsBuf_InitWithBackend(&(nc->ps->argBufRec),
                                    nc->ps->scratch,
                                    0,
                                    sizeof(nc->ps->scratch));
        if (s == NATS_OK)
        {
            nc->ps->argBuf = &(nc->ps->argBufRec);
            s = natsBuf_Append(nc->ps->argBuf,
                               buf + nc->ps->afterSpace,
                               (i - nc->ps->drop) - nc->ps->afterSpace);
        }
    }
    // Check for split msg
    if ((s == NATS_OK)
        && (nc->ps->state == MSG_PAYLOAD) && (nc->ps->msgBuf == NULL))
    {
        // We need to clone the msgArg if it is still referencing the
        // read buffer and we are not able to process the msg.
        if (nc->ps->argBuf == NULL)
            s = _cloneMsgArg(nc);

        if (s == NATS_OK)
        {
            int remainingInScratch;
            int toCopy;

#ifdef _WIN32
// Suppresses the warning that nc->ps->argBuf may be NULL.
// If nc->ps->argBuf is NULL above, then _cloneMsgArg() will set it. If 's'
// is NATS_OK here, then nc->ps->argBuf can't be NULL.
#pragma warning(suppress: 6011)
#endif

            // If we will overflow the scratch buffer, just create a
            // new buffer to hold the split message.
            remainingInScratch = sizeof(nc->ps->scratch) - natsBuf_Len(nc->ps->argBuf);
            toCopy = bufLen - nc->ps->afterSpace;

            if (nc->ps->ma.size > remainingInScratch)
            {
                s = natsBuf_Create(&(nc->ps->msgBuf), nc->ps->ma.size);
            }
            else
            {
                s = natsBuf_InitWithBackend(&(nc->ps->msgBufRec),
                                            nc->ps->scratch + natsBuf_Len(nc->ps->argBuf),
                                            0, remainingInScratch);
                if (s == NATS_OK)
                    nc->ps->msgBuf = &(nc->ps->msgBufRec);
            }
            if (s == NATS_OK)
                s = natsBuf_Append(nc->ps->msgBuf,
                                   buf + nc->ps->afterSpace,
                                   toCopy);
        }
    }

    if (s != NATS_OK)
    {
        // Let's clear all our pointers...
        natsBuf_Destroy(nc->ps->argBuf);
        nc->ps->argBuf = NULL;
        natsBuf_Destroy(nc->ps->msgBuf);
        nc->ps->msgBuf = NULL;
        natsBuf_Destroy(nc->ps->ma.subject);
        nc->ps->ma.subject = NULL;
        natsBuf_Destroy(nc->ps->ma.reply);
        nc->ps->ma.reply = NULL;
    }

    return s;

parseErr:
    if (s == NATS_OK)
        s = NATS_PROTOCOL_ERROR;

    natsMutex_Lock(nc->mu);

    snprintf(nc->errStr, sizeof(nc->errStr),
             "Parse Error [%d]: '%.*s'",
             nc->ps->state,
             bufLen - i,
             buf + i);

    natsMutex_Unlock(nc->mu);

    return s;
}

natsStatus
natsParser_Create(natsParser **newParser)
{
    natsParser  *parser = (natsParser *) NATS_CALLOC(1, sizeof(natsParser));

    if (parser == NULL)
        return NATS_NO_MEMORY;

    *newParser = parser;

    return NATS_OK;
}

void
natsParser_Destroy(natsParser *parser)
{
    if (parser == NULL)
        return;

    natsBuf_Cleanup(&(parser->ma.subjectRec));
    natsBuf_Cleanup(&(parser->ma.replyRec));
    natsBuf_Cleanup(&(parser->argBufRec));
    natsBuf_Cleanup(&(parser->msgBufRec));

    NATS_FREE(parser);
}
