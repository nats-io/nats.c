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

#ifndef NATS_LIBEVENT_H_
#define NATS_LIBEVENT_H_

#ifdef __cplusplus
extern "C"
{
#endif

/** \cond
 *
 */
#include <event.h>
#include <event2/thread.h>
#include "../nats.h"

typedef struct
{
    natsConnection *nc;
    struct event_base *loop;
    struct event *read;
    struct event *write;
    struct event *keepActive;

} natsLibevent;

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

static void
natsLibevent_ProcessEvent(evutil_socket_t fd, short event, void *arg)
{
    natsLibevent *l = (natsLibevent *)arg;

    if (event & EV_READ)
        nats_ProcessReadEvent(l->nc);

    if (event & EV_WRITE)
        nats_ProcessWriteEvent(l->nc);
}

static void
keepAliveCb(evutil_socket_t fd, short flags, void *arg)
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
natsLibevent_Attach(void *userData, natsEventLoop *ev, natsConnection *nc, natsSock socket)
{
    natsLibevent *l = (natsLibevent *)userData;
    natsStatus s = NATS_OK;

    if (l == NULL)
        return NATS_INVALID_ARG;

    // cleanup any prior state.
    natsLibevent_Detach(userData);
    l->nc = nc;
    l->loop = (struct event_base *)ev->loop;

    l->keepActive = event_new(l->loop, -1, EV_PERSIST, keepAliveCb, NULL);
    if (l->keepActive == NULL)
        s = NATS_NO_MEMORY;
    if (s == NATS_OK)
    {
        struct timeval timeout;
        timeout.tv_sec = 100000;
        timeout.tv_usec = 0;
        if (event_add(l->keepActive, &timeout) != 0)
            s = NATS_ERR;
    }

    if (s == NATS_OK)
    {
        // Create the read event and add it right away. Persist it until
        // expicitly removed when detaching.
        l->read = event_new(l->loop, socket, EV_READ | EV_PERSIST,
                            natsLibevent_ProcessEvent, (void *)l);
        natsLibevent_Read((void *)l, true);

        // Create the write event. It will be added when needed by
        // natsConn_asyncWrite.
        l->write = event_new(l->loop, socket, EV_WRITE,
                               natsLibevent_ProcessEvent, (void *)l);
    }

    if (s != NATS_OK)
        natsLibevent_Detach(userData);

    return s;
}

/** \brief Start or stop polling on READ events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for READ events.
 *
 * @param userData the user object created in #natsLibevent_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibevent_Read(void *userData, bool add)
{
    natsLibevent *le = (natsLibevent *)userData;
    int res;

    if (add)
        res = event_add(le->read, NULL);
    else
        res = event_del_noblock(le->read);

    return (res == 0 ? NATS_OK : NATS_ERR);
}

/** \brief Start or stop polling on WRITE events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for WRITE events.
 *
 * @param userData the user object created in #natsLibevent_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibevent_Write(void *userData, bool add)
{
    natsLibevent *le = (natsLibevent *)userData;
    int res = 0;

    if (add)
    {
        if (event_pending(le->write, EV_WRITE, NULL) == 0)
            res = event_add(le->write, NULL);
    }
    else
    {
        res = event_del_noblock(le->write);
    }

    return (res == 0 ? NATS_OK : NATS_ERR);
}

// TODO: <>/<> comment
natsStatus
natsLibevent_Stop(void *userData)
{
    natsLibevent *l = (natsLibevent *)userData;

    if (l->read != NULL)
    {
        event_del_noblock(l->read);
        event_free(l->read);
        l->read = NULL;
    }
    if (l->write != NULL)
    {
        event_del_noblock(l->write);
        event_free(l->write);
        l->write = NULL;
    }

    return NATS_OK;
}

/** \brief The connection is closed, it can be safely detached.
 *
 * When a connection is closed (not disconnected, pending a reconnect), this
 * callback will be invoked. This is the opportunity to cleanup the state
 * maintained by the adapter for this connection.
 *
 * @param userData the user object created in #natsLibevent_Attach
 */
natsStatus
natsLibevent_Detach(void *userData)
{
    natsLibevent *l = (natsLibevent *)userData;

    natsLibevent_Stop(userData);

    if (l->keepActive != NULL)
    {
        event_active(l->keepActive, 0, 0);
        event_free(l->keepActive);
    }

    return NATS_OK;
}

/** \brief Initialize the adapter.
 *
 * Needs to be called once so that the adapter can initialize some state.
 */
natsStatus natsLibevent_Init(natsEventLoop *ev)
{
    natsEventLoop base = {
        .loop = event_base_new(),
        .ctxSize = sizeof(natsLibevent),
        .attach = natsLibevent_Attach,
        .read = natsLibevent_Read,
        .write = natsLibevent_Write,
        .detach = natsLibevent_Detach,
        .stop = natsLibevent_Stop,
    };
    if (base.loop == NULL)
        return NATS_NO_MEMORY;

    *ev = base;
    return NATS_OK;
}

/** @} */ // end of libeventFunctions

#ifdef __cplusplus
}
#endif

#endif /* NATS_LIBEVENT_H_ */
