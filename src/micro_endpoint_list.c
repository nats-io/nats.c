// Copyright 2021-2023 The NATS Authors
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

#include "micro.h"
#include "microp.h"
#include "mem.h"


natsStatus
 _destroyMicroserviceEndpointList(__microserviceEndpointList *list)
{
    natsStatus s = NATS_OK;
    if (list == NULL)
        return NATS_OK;

    for (int i = 0; i < list->len; i++)
    {
        s = natsMicroserviceEndpoint_Destroy(list->endpoints[i]);
        if (s != NATS_OK)
        {
            return NATS_UPDATE_ERR_STACK(s);
        }
    }

    natsMutex_Destroy(list->mu);
    NATS_FREE(list->endpoints);
    NATS_FREE(list);
    return NATS_OK;
}

natsStatus
_newMicroserviceEndpointList(__microserviceEndpointList **new_list)
{
    natsStatus s = NATS_OK;
    __microserviceEndpointList *list = NULL;

    list = NATS_CALLOC(1, sizeof(__microserviceEndpointList));
    if (list == NULL)
    {
        s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    IFOK(s, natsMutex_Create(&list->mu));

    if (s == NATS_OK)
    {
        *new_list = list;
    }
    else
    {
        _destroyMicroserviceEndpointList(list);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsMicroserviceEndpoint *
_microserviceEndpointList_Find(__microserviceEndpointList *list, const char *name, int *index)
{
    natsMicroserviceEndpoint *ep = NULL;
    int i;

    natsMutex_Lock(list->mu);
    for (i = 0; i < list->len; i++)
    {
        if (strcmp(list->endpoints[i]->name, name) == 0)
        {
            if (index != NULL)
            {
                *index = i;
            }
            ep = list->endpoints[i];
            break;
        }
    }
    natsMutex_Unlock(list->mu);
    return ep;
}

natsMicroserviceEndpoint *
_microserviceEndpointList_Get(__microserviceEndpointList *list, const char *name, int *index)
{
    natsMicroserviceEndpoint *ep = NULL;
    int i;

    natsMutex_Lock(list->mu);
    for (i = 0; i < list->len; i++)
    {
        if (strcmp(list->endpoints[i]->name, name) == 0)
        {
            if (index != NULL)
            {
                *index = i;
            }
            ep = list->endpoints[i];
            break;
        }
    }
    natsMutex_Unlock(list->mu);
    return ep;
}


natsStatus
_microserviceEndpointList_Put(__microserviceEndpointList *list, int index, natsMicroserviceEndpoint *ep)
{
    natsStatus s = NATS_OK;

    natsMutex_Lock(list->mu);

    if (index >= list->len)
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }
    else if (index == -1)
    {
        list->endpoints = (natsMicroserviceEndpoint **)
            NATS_REALLOC(list->endpoints, sizeof(natsMicroserviceEndpoint *) * (list->len + 1));
        if (list->endpoints == NULL)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (s == NATS_OK)
        {
            list->endpoints[list->len] = ep;
            list->len++;
        }
    }
    else
    {
        list->endpoints[index] = ep;
    }

    natsMutex_Unlock(list->mu);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
_microserviceEndpointList_Remove(__microserviceEndpointList *list, int index)
{
    natsMutex_Lock(list->mu);

    if (index >= list->len || index < 0)
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }
    if (index < list->len - 1)
    {
        memmove(&list->endpoints[index], &list->endpoints[index + 1], (list->len - index - 1) * sizeof(natsMicroserviceEndpoint *));
    }
    list->len--;

    natsMutex_Unlock(list->mu);
    return NATS_OK;
}
