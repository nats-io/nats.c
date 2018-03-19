// Copyright 2015-2018 The NATS Authors
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
