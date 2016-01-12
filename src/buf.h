// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef BUF_H_
#define BUF_H_

#include <stdbool.h>

#include "status.h"

typedef struct __natsBuffer
{
    char*       data;
    char*       pos;
    int         len;
    int         capacity;
    bool        ownData;
    bool        doFree;

} natsBuffer;

#define natsBuf_Data(b)         ((b)->data)
#define natsBuf_Capacity(b)     ((b)->capacity)
#define natsBuf_Len(b)          ((b)->len)
#define natsBuf_Available(b)    ((b)->capacity - (b)->len)


// Initializes a natsBuffer using 'data' as the back-end byte array.
// The length and capacity are set based on the given parameters.
// Since the 'data' is not owned, and as long as the buffer does not need
// to be expanded, the byte buffer will not be freed when this natsBuffer
// is destroyed. Check natsBuf_Expand() for more details.
//
// One would use this call to initialize a natsBuffer without the added cost
// of allocating memory for the natsBuffer structure itself, for instance
// initializing an natsBuffer on the stack.
natsStatus
natsBuf_InitWithBackend(natsBuffer *buf, char *data, int len, int capacity);

// Initializes a natsBuffer and creates a byte buffer of 'capacity' bytes.
// The natsBuffer owns the buffer and will therefore free the memory used
// when destroyed.
//
// One would use this call to initialize a natsBuffer without the added cost
// of allocating memory for the natsBuffer structure itself, for instance
// initializing an natsBuffer on the stack.
natsStatus
natsBuf_Init(natsBuffer *buf, int capacity);

// Creates a new natsBuffer using 'data' as the back-end byte array.
// The length and capacity are set based on the given parameters.
// Since the 'data' is not owned, and as long as the buffer does not need
// to be expanded, the byte buffer will not be freed when this natsBuffer
// is destroyed. Check natsBuf_Expand() for more details.
natsStatus
natsBuf_CreateWithBackend(natsBuffer **newBuf, char *data, int len, int capacity);

// Creates a new natsBuffer and creates a byte buffer of 'capacity' bytes.
// The natsBuffer owns the buffer and will therefore free the memory used
// when destroyed.
natsStatus
natsBuf_Create(natsBuffer **newBuf, int capacity);

// Resets the length to zero, and the position to the beginning of the buffer.
void
natsBuf_Reset(natsBuffer *buf);

// Sets the size of the buffer to 'newPosition' and new data will be appended
// starting at this position.
void
natsBuf_RewindTo(natsBuffer *buf, int newPosition);

// Expands 'buf' underlying buffer to the given new size 'newSize'.
//
// If 'buf' did not own the underlying buffer, a new buffer is
// created and data copied over. The original data is now detached.
// The underlying buffer is now owned by 'buf' and will be freed when
// the natsBuffer is destroyed.
//
// When 'buf' owns the underlying buffer and it is expanded, a memory
// reallocation of the buffer occurs to satisfy the new size requirement.
//
// Note that one should not save the returned value of natsBuf_Data() and
// use it after any call to natsBuf_Expand/Append/AppendByte() since
// the memory address for the underlying byte buffer may have changed due
// to the buffer expansion.
natsStatus
natsBuf_Expand(natsBuffer *buf, int newSize);

// Appends 'dataLen' bytes from the 'data' byte array to the buffer,
// potentially expanding the buffer.
// See natsBuf_Expand for details about natsBuffer not owning the data.
natsStatus
natsBuf_Append(natsBuffer *buf, const char* data, int dataLen);

// Appends a byte to the buffer, potentially expanding the buffer.
// See natsBuf_Expand for details about natsBuffer not owning the data.
natsStatus
natsBuf_AppendByte(natsBuffer *buf, char b);

// Consume data from a buffer, overwriting the 'n' first bytes by the remaining
// of data in this buffer.
void
natsBuf_Consume(natsBuffer *buf, int n);

// Reads better when dealing with a buffer that was initialized as opposed to
// created, but calling natsBuf_Destroy() will do the right thing regardless
// of how the buffer was created.
#define natsBuf_Cleanup(b)  natsBuf_Destroy((b))

// Frees the data if owned (otherwise leaves it untouched) and the structure
// if the buffer was created with one of the natsBuf_CreateX() function,
// otherwise simply 'memset' the structure.
void
natsBuf_Destroy(natsBuffer *buf);

#endif /* BUF_H_ */
