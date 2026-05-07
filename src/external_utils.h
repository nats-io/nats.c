#ifndef _NATS_EXTERNAL_UTILS_H_
#define _NATS_EXTERNAL_UTILS_H_
#ifndef NATS_EXTERNAL_LIB
#error "This header is meant to allow for external libraries like orbit.c to use internal utilities.\
        It is not intended to be used outside of that context and is not part of the official API.\
        If you are seeing this error, you likely included this header by mistake."
#endif /* NATS_EXTERNAL_LIB */

#include "nats.h"

// buf.h
typedef struct __natsBuffer natsBuffer;

NATS_EXTERN natsStatus
natsBuf_InitWithBackend(natsBuffer *buf, char *data, int len, int capacity);

NATS_EXTERN natsStatus
natsBuf_Init(natsBuffer *buf, int capacity);

NATS_EXTERN natsStatus
natsBuf_CreateWithBackend(natsBuffer **newBuf, char *data, int len, int capacity);

NATS_EXTERN natsStatus
natsBuf_Create(natsBuffer **newBuf, int capacity);

NATS_EXTERN void
natsBuf_Reset(natsBuffer *buf);

NATS_EXTERN void
natsBuf_MoveTo(natsBuffer *buf, int newPosition);

NATS_EXTERN natsStatus
natsBuf_Expand(natsBuffer *buf, int newSize);

NATS_EXTERN natsStatus
natsBuf_Append(natsBuffer *buf, const char* data, int dataLen);

NATS_EXTERN natsStatus
natsBuf_AppendByte(natsBuffer *buf, char b);

NATS_EXTERN void
natsBuf_Consume(natsBuffer *buf, int n);

#define natsBuf_Cleanup(b)  natsBuf_Destroy((b))

NATS_EXTERN void
natsBuf_Destroy(natsBuffer *buf);

NATS_EXTERN const char*
natsBuf_GetData(natsBuffer *buf);

NATS_EXTERN int
natsBuf_GetLen(natsBuffer *buf);

NATS_EXTERN int
natsBuf_GetCapacity(natsBuffer *buf);

#endif /* __NATS_EXTERNAL_UTILS_H_ */
