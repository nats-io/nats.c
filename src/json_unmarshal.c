// Copyright 2015-2023 The NATS Authors
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

#include "natsp.h"

#include "json.h"
#include "conn.h"

natsStatus
nats_unmarshalServerInfo(nats_JSON *json, natsPool *pool, natsServerInfo *info)
{
    natsStatus s = NATS_OK;
    IFOK(s, nats_JSONDupStrIfDiff(json, pool, "server_id", &info->id));
    IFOK(s, nats_JSONDupStrIfDiff(json, pool, "version", &(info->version)));
    IFOK(s, nats_JSONDupStrIfDiff(json, pool, "host", &(info->host)));
    IFOK(s, nats_JSONGetInt(json, "port", &(info->port)));
    IFOK(s, nats_JSONGetBool(json, "auth_required", &(info->authRequired)));
    IFOK(s, nats_JSONGetBool(json, "tls_required", &(info->tlsRequired)));
    IFOK(s, nats_JSONGetBool(json, "tls_available", &(info->tlsAvailable)));
    IFOK(s, nats_JSONGetLong(json, "max_payload", &(info->maxPayload)));
    IFOK(s, nats_JSONDupStringArrayIfDiff(json, pool, "connect_urls",
                                          &(info->connectURLs),
                                          &(info->connectURLsCount)));
    IFOK(s, nats_JSONGetInt(json, "proto", &(info->proto)));
    IFOK(s, nats_JSONGetULong(json, "client_id", &(info->CID)));
    IFOK(s, nats_JSONDupStrIfDiff(json, pool, "nonce", &(info->nonce)));
    IFOK(s, nats_JSONDupStrIfDiff(json, pool, "client_ip", &(info->clientIP)));
    IFOK(s, nats_JSONGetBool(json, "ldm", &(info->lameDuckMode)));
    IFOK(s, nats_JSONGetBool(json, "headers", &(info->headers)));

    return NATS_UPDATE_ERR_STACK(s);
}
// static natsStatus
// _addMD(void *closure, const char *fieldName, nats_JSONField *f)
// {
//     natsMetadata *md = (natsMetadata *)closure;

//     char *name = NATS_STRDUP(fieldName);
//     char *value = NATS_STRDUP(f->value.vstr);
//     if ((name == NULL) || (value == NULL))
//     {
//         NATS_FREE(name);
//         NATS_FREE(value);
//         return nats_setDefaultError(NATS_NO_MEMORY);
//     }

//     md->List[md->Count * 2] = name;
//     md->List[md->Count * 2 + 1] = value;
//     md->Count++;
//     return NATS_OK;
// }

// natsStatus
// nats_unmarshalMetadata(nats_JSON *json, natsPool *pool, const char *fieldName, natsMetadata *md)
// {
//     natsStatus s = NATS_OK;
//     nats_JSON *mdJSON = NULL;
//     int n;

//     md->List = NULL;
//     md->Count = 0;
//     if (json == NULL)
//         return NATS_OK;

//     s = nats_JSONGetObject(json, fieldName, &mdJSON);
//     if ((s != NATS_OK) || (mdJSON == NULL))
//         return NATS_OK;

//     n = natsStrHash_Count(mdJSON->fields);
//     md->List = NATS_CALLOC(n * 2, sizeof(char *));
//     if (md->List == NULL)
//         s = nats_setDefaultError(NATS_NO_MEMORY);
//     IFOK(s, nats_JSONRange(mdJSON, TYPE_STR, 0, _addMD, md));

//     return s;
// }

// natsStatus
// nats_cloneMetadata(natsPool *pool, natsMetadata *clone, natsMetadata md)
// {
//     natsStatus s = NATS_OK;
//     int i = 0;
//     int n;
//     char **list = NULL;

//     clone->Count = 0;
//     clone->List = NULL;
//     if (md.Count == 0)
//         return NATS_OK;

//     n = md.Count * 2;
//     list = natsPool_alloc(pool, n * sizeof(char *));
//     if (list == NULL)
//         s = nats_setDefaultError(NATS_NO_MEMORY);

//     for (i = 0; (STILL_OK(s)) && (i < n); i++)
//     {
//         list[i] = nats_StrdupPool(pool, md.List[i]);
//         if (list[i] == NULL)
//             s = nats_setDefaultError(NATS_NO_MEMORY);
//     }

//     if (STILL_OK(s))
//     {
//         clone->List = (const char **)list;
//         clone->Count = md.Count;
//     }

//     return s;
// }
