// Copyright 2015-2024 The NATS Authors
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

#include "glibp.h"


void
nats_freeGC(natsLib *lib)
{
    natsGCList *gc = &(lib->gc);

    natsThread_Destroy(gc->thread);
    natsCondition_Destroy(gc->cond);
    natsMutex_Destroy(gc->lock);
}


void
nats_garbageCollectorThreadf(void *closure)
{
    natsLib *lib = (natsLib*) closure;
    natsGCList *gc = &(lib->gc);
    natsGCItem *item;
    natsGCItem *list;

    WAIT_LIB_INITIALIZED(lib);

    natsMutex_Lock(gc->lock);

    // Process all elements in the list, even on shutdown
    while (true)
    {
        // Go into wait until we are notified to shutdown
        // or there is something to garbage collect
        gc->inWait = true;

        while (!(gc->shutdown) && (gc->head == NULL))
        {
            natsCondition_Wait(gc->cond, gc->lock);
        }

        // Out of the wait. Setting this boolean avoids unnecessary
        // signaling when an item is added to the collector.
        gc->inWait = false;

        // Do not break out on shutdown here, we want to clear the list,
        // even on exit so that valgrind and the like are happy.

        // Under the lock, we will switch to a local list and reset the
        // GC's list (so that others can add to the list without contention
        // (at least from the GC itself).
        do
        {
            list = gc->head;
            gc->head = NULL;

            natsMutex_Unlock(gc->lock);

            // Now that we are outside of the lock, we can empty the list.
            while ((item = list) != NULL)
            {
                // Pops item from the beginning of the list.
                list = item->next;
                item->next = NULL;

                // Invoke the freeCb associated with this object
                (*(item->freeCb))((void*) item);
            }

            natsMutex_Lock(gc->lock);
        }
        while (gc->head != NULL);

        // If we were ask to shutdown and since the list is now empty, exit
        if (gc->shutdown)
            break;
    }

    natsMutex_Unlock(gc->lock);

    natsLib_Release();
}

bool
natsGC_collect(natsGCItem *item)
{
    natsGCList  *gc;
    bool        signal;

    // If the object was not setup for garbage collection, return false
    // so the caller frees the object.
    if (item->freeCb == NULL)
        return false;

    gc = &(nats_lib()->gc);

    natsMutex_Lock(gc->lock);

    // We will signal only if the GC is in the condition wait.
    signal = gc->inWait;

    // Add to the front of the list.
    item->next = gc->head;

    // Update head.
    gc->head = item;

    if (signal)
        natsCondition_Signal(gc->cond);

    natsMutex_Unlock(gc->lock);

    return true;
}

