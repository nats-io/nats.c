// Copyright 2023 The NATS Authors
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

#include <ctype.h>

#include "microp.h"
#include "conn.h"
#include "opts.h"

static natsMutex *service_callback_mu;
static natsHash *all_services_to_callback; // uses `microService*` as the key and the value.

static inline void _lock_service(microService *m) { natsMutex_Lock(m->service_mu); }
static inline void _unlock_service(microService *m) { natsMutex_Unlock(m->service_mu); }
static inline void _lock_endpoint(microEndpoint *ep) { natsMutex_Lock(ep->endpoint_mu); }
static inline void _unlock_endpoint(microEndpoint *ep) { natsMutex_Unlock(ep->endpoint_mu); }

static microError *_clone_service_config(microServiceConfig **out, microServiceConfig *cfg);
static microError *_new_endpoint(microEndpoint **new_ep, microService *m, const char *prefix, microEndpointConfig *cfg, bool is_internal);
static microError *_new_service(microService **ptr, natsConnection *nc);
static microError *_start_endpoint_l(microService *m, microEndpoint *ep);
static microError *_stop_endpoint_l(microService *m, microEndpoint *ep);
static microError *_wrap_connection_event_callbacks(microService *m);

static bool _is_valid_name(const char *name);

static void _finalize_stopping_service_l(microService *m, const char *comment);
static void _retain_endpoint(microEndpoint *ep, bool lock, const char *caller, const char *comment);
static void _release_endpoint(microEndpoint *ep, bool lock, const char *caller, const char *comment);
static void _release_service(microService *m, bool lock, const char *caller, const char *comment);
static void _retain_service(microService *m, bool lock, const char *caller, const char *comment);

#define RETAIN_EP(__ep, __comment) _retain_endpoint(__ep, true, __comment, __func__)
#define RETAIN_EP_INLINE(__ep, __comment) _retain_endpoint(__ep, false, __comment, __func__)
#define RELEASE_EP(__ep, __comment) _release_endpoint(__ep, true, __comment, __func__)
#define RELEASE_EP_INLINE(__ep, __comment) _release_endpoint(__ep, false, __comment, __func__)
#define RETAIN_SERVICE(__m, __comment) _retain_service(__m, true, __comment, __func__)
#define RETAIN_SERVICE_INLINE(__m, __comment) _retain_service(__m, false, __comment, __func__)
#define RELEASE_SERVICE(__m, __comment) _release_service(__m, true, __comment, __func__)
#define RELEASE_SERVICE_INLINE(__m, __comment) _release_service(__m, false, __comment, __func__)

static inline int _endpoint_count(microService *m)
{
    int n = 0;
    for (microEndpoint *ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        n++;
    }
    return n;
}

// static inline void _dump_endpoints(microService *m)
// {
//     const char *sep = NULL;
//     for (microEndpoint *ep = m->first_ep; ep != NULL; ep = ep->next)
//     {
//         if (sep == NULL)
//             sep = ",";
//         else
//             printf("%s", sep);
//         printf("%s", ep->subject);
//     }
//     printf("\n");
// }

microError *
micro_AddService(microService **new_m, natsConnection *nc, microServiceConfig *cfg)
{
    natsStatus s = NATS_OK;
    microError *err = NULL;
    microService *m = NULL;

    if ((new_m == NULL) || (nc == NULL) || (cfg == NULL) || !_is_valid_name(cfg->Name) || nats_IsStringEmpty(cfg->Version))
        return micro_ErrorInvalidArg;

    // Make a microservice object, with a reference to a natsConnection.
    err = _new_service(&m, nc);
    if (err != NULL)
        return err;

    IFOK(s, natsMutex_Create(&m->service_mu));
    IFOK(s, natsNUID_Next(m->id, sizeof(m->id)));
    err = micro_ErrorFromStatus(s);

    MICRO_CALL(err, _clone_service_config(&m->cfg, cfg));

    // Wrap the connection callbacks before we subscribe to anything.
    MICRO_CALL(err, _wrap_connection_event_callbacks(m));

    MICRO_CALL(err, micro_init_monitoring(m));
    MICRO_CALL(err, microService_AddEndpoint(m, cfg->Endpoint));

    if (err != NULL)
    {
        microError_Ignore(microService_Destroy(m));
        return microError_Wrapf(err, "failed to add microservice %s", cfg->Name);
    }

    *new_m = m;
    return NULL;
}

microError *
microService_AddEndpoint(microService *m, microEndpointConfig *cfg)
{
    return micro_add_endpoint(NULL, m, NULL, cfg, false);
}

microError *
microGroup_AddEndpoint(microGroup *g, microEndpointConfig *cfg)
{
    if (g == NULL)
        return micro_ErrorInvalidArg;

    return micro_add_endpoint(NULL, g->m, g->prefix, cfg, false);
}

microError *
micro_add_endpoint(microEndpoint **new_ep, microService *m, const char *prefix, microEndpointConfig *cfg, bool is_internal)
{
    microError *err = NULL;
    microEndpoint *ptr = NULL;
    microEndpoint *prev_ptr = NULL;
    microEndpoint *ep = NULL;
    microEndpoint *prev_ep = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;
    if (cfg == NULL)
        return NULL;

    err = _new_endpoint(&ep, m, prefix, cfg, is_internal);
    if (err != NULL)
        return microError_Wrapf(err, "failed to create endpoint %s", cfg->Name);

    _lock_service(m);

    if (m->stopping || m->stopped)
    {
        _unlock_service(m);
        return micro_Errorf("can't add an endpoint %s to service %s: the service is stopped", cfg->Name, m->cfg->Name);
    }

    if (m->first_ep != NULL)
    {
        if (strcmp(m->first_ep->name, ep->name) == 0)
        {
            ep->next = m->first_ep->next;
            prev_ep = m->first_ep;
            m->first_ep = ep;
        }
        else
        {
            prev_ptr = m->first_ep;
            for (ptr = m->first_ep->next; ptr != NULL; prev_ptr = ptr, ptr = ptr->next)
            {
                if (strcmp(ptr->name, ep->name) == 0)
                {
                    ep->next = ptr->next;
                    prev_ptr->next = ep;
                    prev_ep = ptr;
                    break;
                }
            }
            if (prev_ep == NULL)
            {
                prev_ptr->next = ep;
            }
        }
    }
    else
    {
        m->first_ep = ep;
    }

    _unlock_service(m);

    if (prev_ep != NULL)
    {
        // Rid of the previous endpoint with the same name, if any. If this
        // fails we can return the error, leave the newly added endpoint in the
        // list, not started. A retry with the same name will clean it up.
        if (err = _stop_endpoint_l(m, prev_ep), err != NULL)
            return err;
        RELEASE_EP(prev_ep, "replaced ep");
    }

    if (err = _start_endpoint_l(m, ep), err != NULL)
    {
        // Best effort, leave the new endpoint in the list, as is. A retry with
        // the same name will clean it up.
        return microError_Wrapf(err, "failed to start endpoint %s", ep->name);
    }

    if (new_ep != NULL)
        *new_ep = ep;
    return NULL;
}

microError *
microService_Stop(microService *m)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    _lock_service(m);
    if (m->stopped)
    {
        _unlock_service(m);
        printf("<>/<> microService_Stop: already stopped\n");
        return NULL;
    }
    ep = m->first_ep;

    for (; ep != NULL; ep = ep->next)
    {
        printf("<>/<> microService_Stop: stopping endpoint %s\n", ep->config->Name);

        if (err = _stop_endpoint_l(m, ep), err != NULL)
            return microError_Wrapf(err, "failed to stop service %s", ep->config->Name);
    }
    _unlock_service(m);

    printf("<>/<> microService_Stop: finalize\n");
    _finalize_stopping_service_l(m, "STOP");
    return NULL;
}

static microError *
_new_service(microService **ptr, natsConnection *nc)
{
    *ptr = NATS_CALLOC(1, sizeof(microService));
    if (*ptr == NULL)
        return micro_ErrorOutOfMemory;

    natsConn_retain(nc);
    printf("<>/<> _new_service: refs: 1\n");
    (*ptr)->refs = 1;
    (*ptr)->nc = nc;
    (*ptr)->started = nats_Now() * 1000000;
    return NULL;
}

static inline microError *
new_service_config(microServiceConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microServiceConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

static inline microError *
_new_endpoint_config(microEndpointConfig **ptr)
{
    *ptr = NATS_CALLOC(1, sizeof(microEndpointConfig));
    return (*ptr == NULL) ? micro_ErrorOutOfMemory : NULL;
}

static void
_free_cloned_endpoint_config(microEndpointConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Subject);

    NATS_FREE(cfg);
}

static inline microError *
micro_strdup(char **ptr, const char *str)
{
    // Make a strdup(NULL) be a no-op, so we don't have to check for NULL
    // everywhere.
    if (str == NULL)
    {
        *ptr = NULL;
        return NULL;
    }
    *ptr = NATS_STRDUP(str);
    if (*ptr == NULL)
        return micro_ErrorOutOfMemory;
    return NULL;
}

static microError *
_clone_endpoint_config(microEndpointConfig **out, microEndpointConfig *cfg)
{
    microError *err = NULL;
    microEndpointConfig *new_cfg = NULL;

    if (out == NULL)
        return micro_ErrorInvalidArg;

    if (cfg == NULL)
    {
        *out = NULL;
        return NULL;
    }

    err = _new_endpoint_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microEndpointConfig));
    }

    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Subject, cfg->Subject));

    if (err != NULL)
    {
        _free_cloned_endpoint_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

static void
_free_endpoint(microEndpoint *ep)
{
    if (ep == NULL)
        return;

    printf("<>/<> _free_endpoint: %s\n", ep->subject);

    NATS_FREE(ep->name);
    NATS_FREE(ep->subject);
    natsSubscription_Destroy(ep->sub);
    natsMutex_Destroy(ep->endpoint_mu);
    _free_cloned_endpoint_config(ep->config);
    NATS_FREE(ep);
}

static bool
_find_endpoint(microEndpoint **prevp, microService *m, microEndpoint *to_find)
{
    microEndpoint *ep = NULL;
    microEndpoint *prev_ep = NULL;

    if ((m == NULL) || (to_find == NULL))
        return false;

    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if (ep == to_find)
        {
            *prevp = prev_ep;
            return true;
        }
        prev_ep = ep;
    }

    return false;
}

static void
_retain_endpoint(microEndpoint *ep, bool lock, const char *caller, const char *comment)
{
    if (ep == NULL)
        return;

    if (lock)
        _lock_endpoint(ep);

    // nats_Sleep(500);

    ep->refs++;
    printf("<>/<> _retain_endpoint++: %s by %s (%s), refs: %d\n", ep->subject, caller ? caller : "NULL", comment ? comment : "NULL", ep->refs);

    if (lock)
        _unlock_endpoint(ep);
}

static void
_release_endpoint(microEndpoint *ep, bool lock, const char *caller, const char *comment)
{
    int refs;

    if (ep == NULL)
        return;

    if (lock)
        _lock_endpoint(ep);

    // nats_Sleep(500);

    refs = --(ep->refs);
    printf("<>/<> _release_endpoint--: %s by %s (%s), refs: %d\n", ep->subject, caller ? caller : "NULL", comment ? comment : "NULL", ep->refs);

    if (lock)
        _unlock_endpoint(ep);

    if (refs == 0)
        _free_endpoint(ep);
}

static void
_free_cloned_service_config(microServiceConfig *cfg)
{
    if (cfg == NULL)
        return;

    // the strings are declared const for the public, but in a clone these need
    // to be freed.
    NATS_FREE((char *)cfg->Name);
    NATS_FREE((char *)cfg->Version);
    NATS_FREE((char *)cfg->Description);
    _free_cloned_endpoint_config(cfg->Endpoint);
    NATS_FREE(cfg);
}

static microError *
_clone_service_config(microServiceConfig **out, microServiceConfig *cfg)
{
    microError *err = NULL;
    microServiceConfig *new_cfg = NULL;

    if (out == NULL || cfg == NULL)
        return micro_ErrorInvalidArg;

    err = new_service_config(&new_cfg);
    if (err == NULL)
    {
        memcpy(new_cfg, cfg, sizeof(microServiceConfig));
    }
    // the strings are declared const for the public, but in a clone these need
    // to be duplicated.
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Name, cfg->Name));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Version, cfg->Version));
    MICRO_CALL(err, micro_strdup((char **)&new_cfg->Description, cfg->Description));
    MICRO_CALL(err, _clone_endpoint_config(&new_cfg->Endpoint, cfg->Endpoint));
    if (err != NULL)
    {
        _free_cloned_service_config(new_cfg);
        return err;
    }

    *out = new_cfg;
    return NULL;
}

static void
_retain_service(microService *m, bool lock, const char *caller, const char *comment)
{
    if (m == NULL)
        return;

    if (lock)
        _lock_service(m);

    // nats_Sleep(500);

    int refs = ++(m->refs);
    printf("<>/<> _retain_service++: %s by %s (%s), refs: %d\n", m->cfg->Name, caller ? caller : "NULL", comment ? comment : "NULL", refs);

    if (lock)
        _unlock_service(m);
}

static void
_free_service(microService *m)
{
    microGroup *next = NULL;

    if (m == NULL)
        return;

    printf("<>/<> _free_service: %s\n", m->cfg->Name);

    // destroy all groups.
    if (m->groups != NULL)
    {
        microGroup *g = m->groups;
        while (g != NULL)
        {
            next = g->next;
            NATS_FREE(g);
            g = next;
        }
    }

    _free_cloned_service_config(m->cfg);
    natsConn_release(m->nc);
    natsMutex_Destroy(m->service_mu);
    NATS_FREE(m);
}

static void
_release_service(microService *m, bool lock, const char *caller, const char *comment)
{
    int refs = 0;

    if (m == NULL)
        return;

    if (lock)
        _lock_service(m);

    // nats_Sleep(500);

    refs = --(m->refs);
    printf("<>/<> _release_service--: %s by %s (%s), refs: %d\n", m->cfg->Name, caller ? caller : "NULL", comment ? comment : "NULL", refs);

    if (lock)
        _unlock_service(m);

    if (refs == 0)
        _free_service(m);
}

bool micro_match_endpoint_subject(const char *ep_subject, const char *actual_subject)
{
    const char *e = ep_subject;
    const char *a = actual_subject;
    const char *etok, *enext;
    int etok_len;
    bool last_etok = false;
    const char *atok, *anext;
    int atok_len;
    bool last_atok = false;

    if (e == NULL || a == NULL)
        return false;

    while (true)
    {
        enext = strchr(e, '.');
        if (enext == NULL)
        {
            enext = e + strlen(e);
            last_etok = true;
        }
        etok = e;
        etok_len = (int)(enext - e);
        e = enext + 1;

        anext = strchr(a, '.');
        if (anext == NULL)
        {
            anext = a + strlen(a);
            last_atok = true;
        }
        atok = a;
        atok_len = (int)(anext - a);
        a = anext + 1;

        if (last_etok)
        {
            if (etok_len == 1 && etok[0] == '>')
                return true;

            if (!last_atok)
                return false;
        }
        if (!(etok_len == 1 && etok[0] == '*') &&
            !(etok_len == atok_len && strncmp(etok, atok, etok_len) == 0))
        {
            return false;
        }
        if (last_atok)
        {
            return last_etok;
        }
    }
}

static microError *
_start_service_callbacks(microService *m)
{
    natsStatus s = NATS_OK;

    if (m == NULL)
        return micro_ErrorInvalidArg;

    if (service_callback_mu == NULL)
    {
        IFOK(s, natsMutex_Create(&service_callback_mu));
        if (s != NATS_OK)
            return micro_ErrorFromStatus(s);
    }

    if (all_services_to_callback == NULL)
    {
        IFOK(s, natsHash_Create(&all_services_to_callback, 8));
        if (s != NATS_OK)
            return micro_ErrorFromStatus(s);
    }

    // Extra reference to the service as long as its callbacks are registered.
    RETAIN_SERVICE(m, "Started service callbacks");

    natsMutex_Lock(service_callback_mu);
    IFOK(s, natsHash_Set(all_services_to_callback, (int64_t)m, (void *)m, NULL));
    natsMutex_Unlock(service_callback_mu);
    if (s != NATS_OK)
    {
        RELEASE_SERVICE(m, "in error");
    }

    return micro_ErrorFromStatus(s);
}

static void
_stop_service_callbacks(microService *m)
{
    if ((m == NULL) || (all_services_to_callback == NULL) || (service_callback_mu == NULL))
        return;

    natsMutex_Lock(service_callback_mu);
    natsHash_Remove(all_services_to_callback, (int64_t)m);
    natsMutex_Unlock(service_callback_mu);

    RELEASE_SERVICE(m, "");
}

static microError *
_services_for_connection(microService ***to_call, int *num_microservices, natsConnection *nc)
{
    microService *m = NULL;
    microService **p = NULL;
    natsHashIter iter;
    int n = 0;
    int i;

    natsMutex_Lock(service_callback_mu);

    natsHashIter_Init(&iter, all_services_to_callback);
    while (natsHashIter_Next(&iter, NULL, (void **)&m))
        if (m->nc == nc)
            n++;
    natsHashIter_Done(&iter);
    if (n > 0)
    {
        p = NATS_CALLOC(n, sizeof(microService *));
        if (p == NULL)
        {
            natsMutex_Unlock(service_callback_mu);
            return micro_ErrorOutOfMemory;
        }

        natsHashIter_Init(&iter, all_services_to_callback);
        i = 0;
        while (natsHashIter_Next(&iter, NULL, (void **)&m))
        {
            if (m->nc == nc)
            {
                RETAIN_SERVICE(m, "For the specific connection closed/error callback"); // for the callback
                p[i++] = m;
            }
        }
        natsHashIter_Done(&iter);
    }

    natsMutex_Unlock(service_callback_mu);

    *to_call = p;
    *num_microservices = n;
    return NULL;
}

static void
_on_connection_closed(natsConnection *nc, void *ignored)
{
    microService *m = NULL;
    microService **to_call = NULL;
    microError *err = NULL;
    int n = 0;
    int i;

    printf("<>/<> on_connection_closed: CALLED\n");

    err = _services_for_connection(&to_call, &n, nc);
    if (err != NULL)
    {
        microError_Ignore(err);
        return;
    }

    printf("<>/<> on_connection_closed: %d services to call\n", n);

    for (i = 0; i < n; i++)
    {
        m = to_call[i];
        microError_Ignore(
            microService_Stop(m));

        RELEASE_SERVICE(m, "After connection closed callback");
    }

    NATS_FREE(to_call);
}

static void
_on_service_error_l(microService *m, const char *subject, natsStatus s)
{
    microEndpoint *ep = NULL;
    microError *err = NULL;

    if (m == NULL)
        return;

    _lock_service(m);
    for (ep = m->first_ep;
         (ep != NULL) && !micro_match_endpoint_subject(ep->subject, subject);
         ep = ep->next)
        ;
    RETAIN_EP(ep, "For error callback"); // for the callback
    _unlock_service(m);

    if ((ep != NULL) && (m->cfg->ErrHandler != NULL))
    {
        (*m->cfg->ErrHandler)(m, ep, s);
    }
    RELEASE_EP(ep, "After error callback"); // after the callback

    if (ep != NULL)
    {
        err = microError_Wrapf(micro_ErrorFromStatus(s), "NATS error on endpoint %s", ep->subject);
        micro_update_last_error(ep, err);
        microError_Destroy(err);
    }

    // TODO: Should we stop the service? The Go client does.
    // microError_Ignore(microService_Stop(m));
}

static void
_on_error(natsConnection *nc, natsSubscription *sub, natsStatus s, void *not_used)
{
    microService *m = NULL;
    microService **to_call = NULL;
    microError *err = NULL;
    const char *subject = NULL;
    int n = 0;
    int i;

    printf("<>/<> on_error: CALLED: %d\n", s);

    if (sub == NULL)
    {
        printf("<>/<> on_error: no sub, nothing to do\n");
        return;
    }
    subject = natsSubscription_GetSubject(sub);

    // `to_call` will have a list of retained service pointers.
    err = _services_for_connection(&to_call, &n, nc);
    if (err != NULL)
    {
        microError_Ignore(err);
        return;
    }

    printf("<>/<> on_error: %d services to call\n", n);

    for (i = 0; i < n; i++)
    {
        m = to_call[i];
        _on_service_error_l(m, subject, s);
        RELEASE_SERVICE(m, "After on_error callback"); // release the extra ref in `to_call`.
    }

    NATS_FREE(to_call);
}

static microError *
_wrap_connection_event_callbacks(microService *m)
{
    microError *err = NULL;

    if ((m == NULL) || (m->nc == NULL) || (m->nc->opts == NULL))
        return micro_ErrorInvalidArg;

    // The new service must be in the list for this to work.
    MICRO_CALL(err, _start_service_callbacks(m));
    MICRO_CALL(err, micro_ErrorFromStatus(
                        natsOptions_setMicroCallbacks(m->nc->opts, _on_connection_closed, _on_error)));

    return microError_Wrapf(err, "failed to wrap connection event callbacks");
}

bool microService_IsStopped(microService *m)
{
    bool stopped;

    if ((m == NULL) || (m->service_mu == NULL))
        return true;

    _lock_service(m);
    stopped = m->stopped;
    _unlock_service(m);

    return stopped;
}

microError *
microService_Destroy(microService *m)
{
    microError *err = NULL;

    printf("<>/<> microService_Destroy: %s\n", m->cfg->Name);

    err = microService_Stop(m);
    if (err != NULL)
        return err;

    RELEASE_SERVICE(m, "Destroy");
    return NULL;
}

microError *
microService_Run(microService *m)
{
    if ((m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    while (!microService_IsStopped(m))
    {
        nats_Sleep(50);
    }

    return NULL;
}

void *
microService_GetState(microService *m)
{
    if (m == NULL)
        return NULL;

    return m->cfg->State;
}

microError *
microService_AddGroup(microGroup **new_group, microService *m, const char *prefix)
{
    if ((m == NULL) || (new_group == NULL) || (prefix == NULL))
        return micro_ErrorInvalidArg;

    *new_group = NATS_CALLOC(1, sizeof(microGroup) +
                                    strlen(prefix) + 1); // "prefix\0"
    if (new_group == NULL)
    {
        return micro_ErrorOutOfMemory;
    }

    memcpy((*new_group)->prefix, prefix, strlen(prefix) + 1);
    (*new_group)->m = m;
    (*new_group)->next = m->groups;
    m->groups = *new_group;

    return NULL;
}

microError *
microGroup_AddGroup(microGroup **new_group, microGroup *parent, const char *prefix)
{
    char *p;
    size_t len;

    if ((parent == NULL) || (new_group == NULL) || (prefix == NULL))
        return micro_ErrorInvalidArg;

    *new_group = NATS_CALLOC(1, sizeof(microGroup) +
                                    strlen(parent->prefix) + 1 + // "parent_prefix."
                                    strlen(prefix) + 1);         // "prefix\0"
    if (new_group == NULL)
    {
        return micro_ErrorOutOfMemory;
    }

    p = (*new_group)->prefix;
    len = strlen(parent->prefix);
    memcpy(p, parent->prefix, len);
    p[len] = '.';
    p += len + 1;
    memcpy(p, prefix, strlen(prefix) + 1);
    (*new_group)->m = parent->m;
    (*new_group)->next = parent->m->groups;
    parent->m->groups = *new_group;

    return NULL;
}

natsConnection *
microService_GetConnection(microService *m)
{
    if (m == NULL)
        return NULL;
    return m->nc;
}

static void
_update_last_error(microEndpoint *ep, microError *err)
{
    ep->stats.NumErrors++;
    microError_String(err, ep->stats.LastErrorString, sizeof(ep->stats.LastErrorString));
}

void micro_update_last_error(microEndpoint *ep, microError *err)
{
    if (err == NULL || ep == NULL)
        return;

    _lock_endpoint(ep);
    _update_last_error(ep, err);
    _unlock_endpoint(ep);
}

static void
_handle_request(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    microError *err = NULL;
    microError *service_err = NULL;
    microEndpoint *ep = (microEndpoint *)closure;
    microService *m;
    microEndpointStats *stats = NULL;
    microRequestHandler handler;
    microRequest *req = NULL;
    int64_t start, elapsed_ns = 0, full_s;

    if ((ep == NULL) || (ep->endpoint_mu == NULL) || (ep->config == NULL) || (ep->config->Handler == NULL))
    {
        // This would be a bug, we should not have received a message on this
        // subscription.
        return;
    }

    stats = &ep->stats;
    m = ep->service_ptr_for_on_complete;
    handler = ep->config->Handler;

    err = micro_new_request(&req, m, ep, msg);
    if (err == NULL)
    {
        // handle the request.
        start = nats_NowInNanoSeconds();

        service_err = handler(req);
        if (service_err != NULL)
        {
            // if the handler returned an error, we attempt to respond with it.
            // Note that if the handler chose to do its own RespondError which
            // fails, and then the handler returns its error - we'll try to
            // RespondError again, double-counting the error.
            err = microRequest_RespondError(req, service_err);
        }

        elapsed_ns = nats_NowInNanoSeconds() - start;
    }

    // Update stats.
    _lock_endpoint(ep);
    stats->NumRequests++;
    stats->ProcessingTimeNanoseconds += elapsed_ns;
    full_s = stats->ProcessingTimeNanoseconds / 1000000000;
    stats->ProcessingTimeSeconds += full_s;
    stats->ProcessingTimeNanoseconds -= full_s * 1000000000;
    _update_last_error(ep, err);
    _unlock_endpoint(ep);

    microError_Destroy(err);
    micro_free_request(req);
    natsMsg_Destroy(msg);
}

static void
_finalize_stopping_service_l(microService *m, const char *comment)
{
    microDoneHandler doneHandler = NULL;

    if (m == NULL)
        return;

    _lock_service(m);

    // If the service is already stopped there is nothing to do. Also, if this
    // function is called while the endpoints are still linked to the service,
    // it means they are draining and we should wait for them to complete.
    if (m->stopped || m->first_ep != NULL)
    {
        printf("<>/<> finalize_stopping_service_l: %s: already stopped or draining %d\n", comment, _endpoint_count(m));
        _unlock_service(m);
        return;
    }
    m->stopped = true;
    doneHandler = m->cfg->DoneHandler;
    _unlock_service(m);

    printf("<>/<> _finalize_stopping_service_l: %s: stop now!!!\n", comment);
    // Disable any subsequent async callbacks.
    _stop_service_callbacks(m);

    if (doneHandler != NULL)
    {
        RETAIN_SERVICE(m, "For DONE handler");
        doneHandler(m);
        RELEASE_SERVICE(m, "After DONE handler");
    }
}

static void
_release_on_endpoint_complete(void *closure)
{
    microEndpoint *ep = (microEndpoint *)closure;
    microEndpoint *prev_ep = NULL;
    microService *m = NULL;
    natsSubscription *sub = NULL;

    if (ep == NULL)
        return;

    nats_Sleep(1000);

    m = ep->service_ptr_for_on_complete;
    if ((m == NULL) || (m->service_mu == NULL))
        return;

    _lock_endpoint(ep);
    printf("<>/<> EP DONE: remove EP %s and finalize\n", ep->subject);
    ep->is_draining = false;
    sub = ep->sub;
    ep->sub = NULL;
    _unlock_endpoint(ep);

    // Unlink the endpoint from the service.
    _lock_service(m);
    if (_find_endpoint(&prev_ep, m, ep))
    {
        if (prev_ep != NULL)
            prev_ep->next = ep->next;
        else
            m->first_ep = ep->next;
    }
    RELEASE_SERVICE(m, "EP DONE");
    _unlock_service(m);

    // Force the subscription to be destroyed now.
    natsSubscription_Destroy(sub);
    RELEASE_EP(ep, "EP DONE");

    // If this is the last shutting down endpoint, finalize the service
    // shutdown.
    _finalize_stopping_service_l(m, ep->subject);
}

static microError *
_start_endpoint_l(microService *m, microEndpoint *ep)
{
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;

    if ((ep->subject == NULL) || (ep->config == NULL) || (ep->config->Handler == NULL))
        // nothing to do
        return NULL;

    // reset the stats.
    memset(&ep->stats, 0, sizeof(ep->stats));

    if (ep->is_monitoring_endpoint)
        s = natsConnection_Subscribe(&sub, m->nc, ep->subject, _handle_request, ep);
    else
        s = natsConnection_QueueSubscribe(&sub, m->nc, ep->subject, MICRO_QUEUE_GROUP, _handle_request, ep);

    if (s == NATS_OK)
    {
        // extra retain before subscribing since we'll need to hold it until
        // on_complete on the subscription.
        _lock_endpoint(ep);
        RETAIN_EP_INLINE(ep, "Started endpoint");
        ep->sub = sub;
        ep->is_draining = false;
        _unlock_endpoint(ep);

        natsSubscription_SetOnCompleteCB(sub, _release_on_endpoint_complete, ep);
    }
    else
    {
        natsSubscription_Destroy(sub); // likely always a no-op.
    }

    return micro_ErrorFromStatus(s);
}

static microError *
_stop_endpoint_l(microService *m, microEndpoint *ep)
{
    natsStatus s = NATS_OK;
    natsSubscription *sub = NULL;
    bool conn_closed = natsConnection_IsClosed(m->nc);

    if (ep == NULL)
        return NULL;

    _lock_endpoint(ep);
    sub = ep->sub;

    if (ep->is_draining || conn_closed || !natsSubscription_IsValid(sub))
    {
        // If stopping, _release_on_endpoint_complete will take care of
        // finalizing, nothing else to do. In other cases
        // _release_on_endpoint_complete has already been called.
        _unlock_endpoint(ep);
        return NULL;
    }

    ep->is_draining = true;
    // RELEASE_EP_INLINE(ep, "stopped (counter to initial ref=1), draining endpoint");
    _unlock_endpoint(ep);

    // When the drain is complete, will release the final ref on ep.
    s = natsSubscription_Drain(sub);
    if (s != NATS_OK)
    {
        return microError_Wrapf(micro_ErrorFromStatus(s),
                                "failed to stop endpoint %s: failed to drain subscription", ep->name);
    }

    return NULL;
}

static bool
_is_valid_subject(const char *subject)
{
    int i;
    int len;

    if (subject == NULL)
        return false;

    len = (int)strlen(subject);
    if (len == 0)
        return false;

    for (i = 0; i < len - 1; i++)
    {
        if ((subject[i] == ' ') || (subject[i] == '>'))
            return false;
    }

    if ((subject[i] == ' '))
        return false;

    return true;
}

static microError *
_dup_with_prefix(char **dst, const char *prefix, const char *src)
{
    size_t len = strlen(src) + 1;
    char *p;

    if (!nats_IsStringEmpty(prefix))
        len += strlen(prefix) + 1;

    *dst = NATS_CALLOC(1, len);
    if (*dst == NULL)
        return micro_ErrorOutOfMemory;

    p = *dst;
    if (!nats_IsStringEmpty(prefix))
    {
        len = strlen(prefix);
        memcpy(p, prefix, len);
        p[len] = '.';
        p += len + 1;
    }
    memcpy(p, src, strlen(src) + 1);
    return NULL;
}

static microError *
_new_endpoint(microEndpoint **new_ep, microService *m, const char *prefix, microEndpointConfig *cfg, bool is_internal)
{
    microError *err = NULL;
    microEndpoint *ep = NULL;
    const char *subj;

    if (cfg == NULL)
        return microError_Wrapf(micro_ErrorInvalidArg, "NULL endpoint config");
    if (!_is_valid_name(cfg->Name))
        return microError_Wrapf(micro_ErrorInvalidArg, "invalid endpoint name %s", cfg->Name);
    if (cfg->Handler == NULL)
        return microError_Wrapf(micro_ErrorInvalidArg, "NULL endpoint request handler for %s", cfg->Name);

    if ((cfg->Subject != NULL) && !_is_valid_subject(cfg->Subject))
        return micro_ErrorInvalidArg;

    subj = nats_IsStringEmpty(cfg->Subject) ? cfg->Name : cfg->Subject;

    ep = NATS_CALLOC(1, sizeof(microEndpoint));
    if (ep == NULL)
        return micro_ErrorOutOfMemory;
    ep->is_monitoring_endpoint = is_internal;

    // retain `m` before storing it for callbacks.
    RETAIN_SERVICE(m, "in endpoint for callbacks");
    ep->service_ptr_for_on_complete = m;

    MICRO_CALL(err, micro_ErrorFromStatus(natsMutex_Create(&ep->endpoint_mu)));
    MICRO_CALL(err, _clone_endpoint_config(&ep->config, cfg));
    MICRO_CALL(err, _dup_with_prefix(&ep->name, prefix, cfg->Name));
    MICRO_CALL(err, _dup_with_prefix(&ep->subject, prefix, subj));
    if (err != NULL)
    {
        _free_endpoint(ep);
        return err;
    }

    *new_ep = ep;
    return NULL;
}

microError *
microService_GetInfo(microServiceInfo **new_info, microService *m)
{
    microServiceInfo *info = NULL;
    microEndpoint *ep = NULL;
    int len;

    if ((new_info == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    info = NATS_CALLOC(1, sizeof(microServiceInfo));
    if (info == NULL)
        return micro_ErrorOutOfMemory;

    micro_strdup((char **)&info->Name, m->cfg->Name);
    micro_strdup((char **)&info->Version, m->cfg->Version);
    micro_strdup((char **)&info->Description, m->cfg->Description);
    micro_strdup((char **)&info->Id, m->id);
    info->Type = MICRO_INFO_RESPONSE_TYPE;

    _lock_service(m);

    len = 0;
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
            len++;
    }

    // Overallocate subjects, will filter out internal ones.
    info->Subjects = NATS_CALLOC(len, sizeof(char *));
    if (info->Subjects == NULL)
    {
        _unlock_service(m);
        NATS_FREE(info);
        return micro_ErrorOutOfMemory;
    }

    len = 0;
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((!ep->is_monitoring_endpoint) && (ep->subject != NULL))
        {
            micro_strdup((char **)&info->Subjects[len], ep->subject);
            len++;
        }
    }
    info->SubjectsLen = len;
    _unlock_service(m);

    *new_info = info;
    return NULL;
}

void microServiceInfo_Destroy(microServiceInfo *info)
{
    int i;

    if (info == NULL)
        return;

    // casts to quiet the compiler.
    for (i = 0; i < info->SubjectsLen; i++)
        NATS_FREE((char *)info->Subjects[i]);
    NATS_FREE((char *)info->Subjects);
    NATS_FREE((char *)info->Name);
    NATS_FREE((char *)info->Version);
    NATS_FREE((char *)info->Description);
    NATS_FREE((char *)info->Id);
    NATS_FREE(info);
}

microError *
microService_GetStats(microServiceStats **new_stats, microService *m)
{
    microServiceStats *stats = NULL;
    microEndpoint *ep = NULL;
    int len;
    long double avg = 0.0;

    if ((new_stats == NULL) || (m == NULL) || (m->service_mu == NULL))
        return micro_ErrorInvalidArg;

    stats = NATS_CALLOC(1, sizeof(microServiceStats));
    if (stats == NULL)
        return micro_ErrorOutOfMemory;

    micro_strdup((char **)&stats->Name, m->cfg->Name);
    micro_strdup((char **)&stats->Version, m->cfg->Version);
    micro_strdup((char **)&stats->Id, m->id);
    stats->Started = m->started;
    stats->Type = MICRO_STATS_RESPONSE_TYPE;

    _lock_service(m);

    len = 0;
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((ep != NULL) && (!ep->is_monitoring_endpoint))
            len++;
    }

    // Allocate the actual structs, not pointers.
    stats->Endpoints = NATS_CALLOC(len, sizeof(microEndpointStats));
    if (stats->Endpoints == NULL)
    {
        _unlock_service(m);
        NATS_FREE(stats);
        return micro_ErrorOutOfMemory;
    }

    len = 0;
    for (ep = m->first_ep; ep != NULL; ep = ep->next)
    {
        if ((ep != NULL) && (!ep->is_monitoring_endpoint) && (ep->endpoint_mu != NULL))
        {
            _lock_endpoint(ep);
            // copy the entire struct, including the last error buffer.
            stats->Endpoints[len] = ep->stats;

            micro_strdup((char **)&stats->Endpoints[len].Name, ep->name);
            micro_strdup((char **)&stats->Endpoints[len].Subject, ep->subject);
            avg = (long double)ep->stats.ProcessingTimeSeconds * 1000000000.0 + (long double)ep->stats.ProcessingTimeNanoseconds;
            avg = avg / (long double)ep->stats.NumRequests;
            stats->Endpoints[len].AverageProcessingTimeNanoseconds = (int64_t)avg;
            len++;
            _unlock_endpoint(ep);
        }
    }

    _unlock_service(m);
    stats->EndpointsLen = len;

    *new_stats = stats;
    return NULL;
}

void microServiceStats_Destroy(microServiceStats *stats)
{
    int i;

    if (stats == NULL)
        return;

    for (i = 0; i < stats->EndpointsLen; i++)
    {
        NATS_FREE((char *)stats->Endpoints[i].Name);
        NATS_FREE((char *)stats->Endpoints[i].Subject);
    }
    NATS_FREE(stats->Endpoints);
    NATS_FREE((char *)stats->Name);
    NATS_FREE((char *)stats->Version);
    NATS_FREE((char *)stats->Id);
    NATS_FREE(stats);
}

static bool
_is_valid_name(const char *name)
{
    int i;
    int len;

    if (name == NULL)
        return false;

    len = (int)strlen(name);
    if (len == 0)
        return false;

    for (i = 0; i < len; i++)
    {
        if (!isalnum(name[i]) && (name[i] != '_') && (name[i] != '-'))
            return false;
    }
    return true;
}
