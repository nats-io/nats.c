// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef GC_H_
#define GC_H_

// This callback implements the specific code to free the given object.
// This is invoked by the garbage collector.
typedef void (*nats_FreeObjectCb)(void *object);

// This structure should be included as the first field of any object
// that needs to be garbage collected.
typedef struct __natsGCItem
{
    struct __natsGCItem     *next;
    nats_FreeObjectCb       freeCb;

} natsGCItem;

// Gives the object to the garbage collector.
// Returns 'true' if the GC takes ownership, 'false' otherwise (in this case,
// the caller is responsible for freeing the object).
bool
natsGC_collect(natsGCItem *item);


#endif /* GC_H_ */
