// Copyright 2016-2018 The NATS Authors
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

#ifndef LIBEVENT_H_
#define LIBEVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

/** \cond
 *
 */
#include <event.h>
#include <event2/thread.h>
#include "../nats.h"

typedef struct
{
    natsConnection      *nc;
    struct event_base   *loop;
    struct event        *read;
    struct event        *write;
    struct event        *keepActive;

} natsLibeventEvents;

// Forward declarations
natsStatus natsLibevent_Read(void *userData, bool add);
natsStatus natsLibevent_Detach(void *userData);

/** \endcond
 *
 */


/** \defgroup libeventFunctions Libevent Adapter
 *
 *  Adapter to plug a `NATS` connection to a `libevent` event loop.
 *  @{
 */

/** \brief Initialize the adapter.
 *
 * Needs to be called once so that the adapter can initialize some state.
 */
void
natsLibevent_Init(void)
{
#if _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
}

static void
natsLibevent_ProcessEvent(evutil_socket_t fd, short event, void *arg)
{
    natsLibeventEvents *nle = (natsLibeventEvents*) arg;

    if (event & EV_READ)
        natsConnection_ProcessReadEvent(nle->nc);

    if (event & EV_WRITE)
        natsConnection_ProcessWriteEvent(nle->nc);
}

static void
keepAliveCb(evutil_socket_t fd, short flags, void * arg)
{
    // do nothing...
}

/** \brief Attach a connection to the given event loop.
 *
 * This callback is invoked after `NATS` library has connected, or reconnected.
 * For a reconnect event, `*userData` will not be `NULL`. This function will
 * start polling on READ events for the given `socket`.
 *
 * @param userData the location where the adapter stores the user object passed
 * to the other callbacks.
 * @param loop the event loop as a generic pointer. Cast to appropriate type.
 * @param nc the connection to attach to the event loop
 * @param socket the socket to start polling on.
 */
natsStatus
natsLibevent_Attach(void **userData, void *loop, natsConnection *nc, natsSock socket)
{
    struct event_base   *libeventLoop = (struct event_base*) loop;
    natsLibeventEvents  *nle          = (natsLibeventEvents*) (*userData);
    natsStatus          s             = NATS_OK;

    // This is the first attach (when reconnecting, nle will be non-NULL).
    if (nle == NULL)
    {
        nle = (natsLibeventEvents*) calloc(1, sizeof(natsLibeventEvents));
        if (nle == NULL)
            return NATS_NO_MEMORY;

        nle->nc   = nc;
        nle->loop = libeventLoop;

        nle->keepActive = event_new(nle->loop, -1, EV_PERSIST, keepAliveCb, NULL);
        if (nle->keepActive == NULL)
            s = NATS_NO_MEMORY;

        if (s == NATS_OK)
        {
            struct timeval timeout;

            timeout.tv_sec = 100000;
            timeout.tv_usec = 0;

            if (event_add(nle->keepActive, &timeout) != 0)
                s = NATS_ERR;
        }
    }
    else
    {
        if (nle->read != NULL)
        {
            event_free(nle->read);
            nle->read = NULL;
        }
        if (nle->write != NULL)
        {
            event_free(nle->write);
            nle->write = NULL;
        }
    }

    if (s == NATS_OK)
    {
        nle->read = event_new(nle->loop, socket, EV_READ|EV_PERSIST,
                              natsLibevent_ProcessEvent, (void*) nle);
        natsLibevent_Read((void*) nle, true);

        nle->write = event_new(nle->loop, socket, EV_WRITE|EV_PERSIST,
                               natsLibevent_ProcessEvent, (void*) nle);
    }

    if (s == NATS_OK)
        *userData = (void*) nle;
    else
        natsLibevent_Detach((void*) nle);

    return s;
}

/** \brief Start or stop polling on READ events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for READ events.
 *
 * @param userData the user object created in #natsLibuv_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibevent_Read(void *userData, bool add)
{
    natsLibeventEvents  *nle = (natsLibeventEvents*) userData;
    int                 res;

    if (add)
        res = event_add(nle->read, NULL);
    else
        res = event_del(nle->read);

    return (res == 0 ? NATS_OK : NATS_ERR);
}

/** \brief Start or stop polling on WRITE events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for WRITE events.
 *
 * @param userData the user object created in #natsLibuv_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibevent_Write(void *userData, bool add)
{
    natsLibeventEvents  *nle = (natsLibeventEvents*) userData;
    int                 res;

    if (add)
        res = event_add(nle->write, NULL);
    else
        res = event_del(nle->write);

    return (res == 0 ? NATS_OK : NATS_ERR);
}

/** \brief The connection is closed, it can be safely detached.
 *
 * When a connection is closed (not disconnected, pending a reconnect), this
 * callback will be invoked. This is the opportunity to cleanup the state
 * maintained by the adapter for this connection.
 *
 * @param userData the user object created in #natsLibuv_Attach
 */
natsStatus
natsLibevent_Detach(void *userData)
{
    natsLibeventEvents *nle = (natsLibeventEvents*) userData;

    if (nle->read != NULL)
        event_free(nle->read);
    if (nle->write != NULL)
        event_free(nle->write);
    if (nle->keepActive != NULL)
    {
        event_active(nle->keepActive, 0, 0);
        event_free(nle->keepActive);
    }

    free(nle);

    return NATS_OK;
}

/** @} */ // end of libeventFunctions

#ifdef __cplusplus
}
#endif

#endif /* LIBEVENT_H_ */
