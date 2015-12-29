// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef PARSER_H_
#define PARSER_H_

#include <stdint.h>

//#include "natsp.h"
#include "status.h"
#include "buf.h"

typedef enum
{
    OP_START = 0,
    OP_PLUS,
    OP_PLUS_O,
    OP_PLUS_OK,
    OP_MINUS,
    OP_MINUS_E,
    OP_MINUS_ER,
    OP_MINUS_ERR,
    OP_MINUS_ERR_SPC,
    MINUS_ERR_ARG,
    OP_M,
    OP_MS,
    OP_MSG,
    OP_MSG_SPC,
    MSG_ARG,
    MSG_PAYLOAD,
    MSG_END,
    OP_P,
    OP_PI,
    OP_PIN,
    OP_PING,
    OP_PO,
    OP_PON,
    OP_PONG,

} natsOp;

typedef struct __natsMsgArg
{
    natsBuffer  subjectRec;
    natsBuffer  *subject;
    natsBuffer  replyRec;
    natsBuffer  *reply;
    int64_t     sid;
    int         size;

} natsMsgArg;

#define MAX_CONTROL_LINE_SIZE   (1024)

typedef struct __natsParser
{
    natsOp      state;
    int         afterSpace;
    int         drop;
    natsMsgArg  ma;
    natsBuffer  argBufRec;
    natsBuffer  *argBuf;
    natsBuffer  msgBufRec;
    natsBuffer  *msgBuf;
    char        scratch[MAX_CONTROL_LINE_SIZE];

} natsParser;

// This is defined in natsp.h, natsp.h includes us. Alternatively, we can move
// all the parser defines in natsp.h
struct __natsConnection;

natsStatus
natsParser_Create(natsParser **newParser);

natsStatus
natsParser_Parse(struct __natsConnection *nc, char *buf, int bufLen);

void
natsParser_Destroy(natsParser *parser);

#endif /* PARSER_H_ */
