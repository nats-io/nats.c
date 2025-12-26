// Copyright 2025 The NATS Authors
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
#include "object.h"
#include "js.h"

static const char *objBackingStore     = "JetStream";
static const char *objNamePrefix       = "OBJ_";
static const char *objAllChunksFilter  = "$O.*.C.>";   // filter to get all stream names/info for object stores
static const char *objNameTmpl         = "OBJ_%s";     // OBJ_<bucket>                  // stream name
static const char *objAllChunksPreTmpl = "$O.%s.C.>";  // $O.<bucket>.C.>               // chunk stream subject
static const char *objAllMetaPreTmpl   = "$O.%s.M.>";  // $O.<bucket>.M.>               // meta stream subject
static const char *objChunksPreTmpl    = "$O.%s.C.%s"; // $O.<bucket>.C.<object-nuid>   // chunk message subject
static const char *objMetaPreTmpl      = "$O.%s.M.%s"; // $O.<bucket>.M.<name-encoded>  // meta message subject
static const char *objDigestTmpl       = "SHA-256=%s";


//////////////////////////////////////////////////////////////////////////////
// objStore management APIs
//////////////////////////////////////////////////////////////////////////////

natsStatus
objStoreConfig_Init(objStoreConfig *cfg)
{
    if (cfg == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(cfg, 0, sizeof(objStoreConfig));
    return NATS_OK;
}

static void
_freeObs(objStore *obs)
{
    jsCtx *js       = NULL;
    jsCtx *pushJS   = NULL;

    if (obs == NULL)
        return;

    js = obs->js;
    pushJS = obs->pushJS;
    NATS_FREE(obs->name);
    NATS_FREE(obs->streamName);
    natsMutex_Destroy(obs->mu);
    NATS_FREE(obs);
    js_release(pushJS);
    js_release(js);
}

static void
_retainObs(objStore *obs)
{
    natsMutex_Lock(obs->mu);
    obs->refs++;
    natsMutex_Unlock(obs->mu);
}

static void
_releaseObs(objStore *obs)
{
    bool doFree;

    if (obs == NULL)
        return;

    natsMutex_Lock(obs->mu);
    doFree = (--(obs->refs) == 0);
    natsMutex_Unlock(obs->mu);

    if (doFree)
        _freeObs(obs);
}

static natsStatus
_createObjStore(objStore **new_obs, jsCtx *js, const char *bucket)
{
    natsStatus  s       = NATS_OK;
    objStore    *obs    = NULL;

    if (!nats_validBucketName(bucket))
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrInvalidStoreName);

    obs = (objStore*) NATS_CALLOC(1, sizeof(objStore));
    if (obs == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    obs->refs = 1;
    s = natsMutex_Create(&(obs->mu));
    IF_OK_DUP_STRING(s, obs->name, bucket);
    if ((s == NATS_OK) && (nats_asprintf(&(obs->streamName), objNameTmpl, bucket) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        obs->js = js;
        js_retain(js);
        *new_obs = obs;
    }
    else
        _freeObs(obs);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_addOrUpdate(objStore **new_obs, jsCtx *js, objStoreConfig *cfg, bool add)
{
    natsStatus      s       = NATS_OK;
    objStore        *obs    = NULL;
    char            *chunks = NULL;
    char            *meta   = NULL;
    const char      *subjects[2];
    jsStreamConfig  scfg;

    if ((new_obs == NULL) || (js == NULL) || (cfg == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _createObjStore(&obs, js, cfg->Bucket);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (nats_asprintf(&chunks, objAllChunksPreTmpl, cfg->Bucket) < 0)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if ((s == NATS_OK) && (nats_asprintf(&meta, objAllMetaPreTmpl, cfg->Bucket) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        subjects[0] = chunks;
        subjects[1] = meta;

        int replicas = cfg->Replicas;
        if (replicas == 0)
            replicas = 1;

        int64_t maxBytes = cfg->MaxBytes;
        if (maxBytes == 0)
            maxBytes = -1;

        jsStorageCompression compression = js_StorageCompressionNone;
        if (cfg->Compression)
            compression = js_StorageCompressionS2;

        jsStreamConfig_Init(&scfg);
        scfg.Name = obs->streamName;
        scfg.Description = cfg->Description;
        scfg.Subjects = subjects;
        scfg.SubjectsLen = 2;
        scfg.MaxAge = NATS_MILLIS_TO_NANOS(cfg->TTL);
        scfg.MaxBytes = maxBytes;
        scfg.Storage = cfg->Storage;
        scfg.Replicas = (int64_t) replicas;
        scfg.Placement = cfg->Placement;
        scfg.Discard = js_DiscardNew;
        scfg.AllowRollup = true;
        scfg.AllowDirect = true;
        scfg.Compression = compression;
        scfg.Metadata = cfg->Metadata;
    }
    if (s == NATS_OK)
    {
        jsErrCode errCode = 0;

        if (add)
        {
            s = js_AddStream(NULL, js, &scfg, NULL, &errCode);
            if ((s != NATS_OK) && (errCode == JSStreamNameExistErr))
                s = nats_setError(NATS_ERR, "%s: %s", obsErrBucketExists, cfg->Bucket);
        }
        else
        {
            s = js_UpdateStream(NULL, js, &scfg, NULL, &errCode);
            if ((s != NATS_OK) && (errCode == JSStreamNotFoundErr))
                s = nats_setError(NATS_NOT_FOUND, "%s: %s", obsErrBucketNotFound, cfg->Bucket);
        }
    }
    if (s == NATS_OK)
    {
        js_lock(js);
        s = natsConnection_JetStream(&(obs->pushJS), js->nc, &(js->opts));
        js_unlock(js);
    }

    NATS_FREE(chunks);
    NATS_FREE(meta);

    if (s == NATS_OK)
        *new_obs = obs;
    else
        _freeObs(obs);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_CreateObjectStore(objStore **new_obs, jsCtx *js, objStoreConfig *cfg)
{
    natsStatus s = _addOrUpdate(new_obs, js, cfg, true);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_UpdateObjectStore(objStore **new_obs, jsCtx *js, objStoreConfig *cfg)
{
    natsStatus s = _addOrUpdate(new_obs, js, cfg, false);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_ObjectStore(objStore **new_obs, jsCtx *js, const char *bucket)
{
    natsStatus      s       = NATS_OK;
    objStore        *obs    = NULL;
    jsStreamInfo    *si     = NULL;
    jsErrCode       errCode = 0;

    if ((new_obs == NULL) || (js == NULL) || nats_IsStringEmpty(bucket))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (!nats_validBucketName(bucket))
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrInvalidStoreName);

    s = _createObjStore(&obs, js, bucket);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // Lookup the stream.
    s = js_GetStreamInfo(&si, js, obs->streamName, NULL, &errCode);
    if (s == NATS_OK)
    {
        js_lock(js);
        s = natsConnection_JetStream(&(obs->pushJS), js->nc, &(js->opts));
        js_unlock(js);
    }
    if (s == NATS_OK)
        *new_obs = obs;
    else
        _freeObs(obs);

    jsStreamInfo_Destroy(si);

    // If the stream was not found, return this error without updating the stack.
    if ((s == NATS_NOT_FOUND) && (errCode == JSStreamNotFoundErr))
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_DeleteObjectStore(jsCtx *js, const char *bucket)
{
    natsStatus  s       = NATS_OK;
    jsErrCode   errCode = 0;
    char        *stream = NULL;

    if ((js == NULL) || nats_IsStringEmpty(bucket))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (!nats_validBucketName(bucket))
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrInvalidStoreName);

    if (nats_asprintf(&stream, objNameTmpl, bucket) < 0)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    else
        s = js_DeleteStream(js, stream, NULL, &errCode);

    NATS_FREE(stream);

    if ((s == NATS_NOT_FOUND) && (errCode == JSStreamNotFoundErr))
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

void
objStoreNamesList_Destroy(objStoreNamesList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        NATS_FREE(list->List[i]);
    NATS_FREE(list->List);
    NATS_FREE(list);
}

natsStatus
js_ObjectStoreNames(objStoreNamesList **list, jsCtx *js)
{
    natsStatus          s       = NATS_OK;
    jsStreamNamesList   *snl    = NULL;
    objStoreNamesList   *osl    = NULL;
    int                 i       = 0;
    jsOptions           opts;

    if ((list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    jsOptions_Init(&opts);
    opts.Stream.Info.SubjectsFilter = objAllChunksFilter;
    s = js_StreamNames(&snl, js, &opts, NULL);
    // Return early if there were no streams.
    if (s == NATS_NOT_FOUND)
    {
        // We don't update the error stack for "not found" since we consider
        // this a "normal" error.
        return s;
    }
    osl = (objStoreNamesList*) NATS_CALLOC(1, sizeof(objStoreNamesList));
    if (osl == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        // We will allocate based on snl->Count, which may be more than needed
        // if some stream names don't start with "OBJ_".
        osl->List = (char**) NATS_CALLOC(snl->Count, sizeof(char*));
        if (osl->List == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    for (i=0; (s == NATS_OK) && (i<snl->Count); i++)
    {
        // Exclude the ones that don't start with "OBJ_".
        if (strstr(snl->List[i], objNamePrefix) != snl->List[i])
            continue;
        // Remove the "OBJ_" prefix.
        DUP_STRING(s, osl->List[osl->Count], snl->List[i] + 4);
        if (s == NATS_OK)
            osl->Count++;
    }
    // Don't need that anymore.
    jsStreamNamesList_Destroy(snl);
    // If success, but we did not actually add any name (because none started
    // with "OBJ_", switch state to NATS_NOT_FOUND.
    if ((s == NATS_OK) && (osl->Count == 0))
        s = NATS_NOT_FOUND;

    if (s == NATS_OK)
        *list = osl;
    else
    {
        objStoreNamesList_Destroy(osl);
        // If not found, return error without updating error stack.
        if (s == NATS_NOT_FOUND)
            return s;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

void
objStoreStatus_Destroy(objStoreStatus *status)
{
    if (status == NULL)
        return;

    NATS_FREE((char*) status->Bucket);
    jsStreamInfo_Destroy(status->StreamInfo);
    NATS_FREE(status);
}

void
objStoreStatusesList_Destroy(objStoreStatusesList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        objStoreStatus_Destroy(list->List[i]);
    NATS_FREE(list->List);
    NATS_FREE(list);
}

static natsStatus
_createObjStoreStatus(objStoreStatus **new_oss, jsStreamInfo *info)
{
    natsStatus      s       = NATS_OK;
    objStoreStatus  *oss    = NULL;
    jsStreamConfig  *cfg    = info->Config; // Verified to be not NULL.

    oss = (objStoreStatus*) NATS_CALLOC(1, sizeof(objStoreStatus));
    if (oss == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // We remove the prefix (we know it contains it)
    DUP_STRING(s, oss->Bucket, cfg->Name + 4);
    if (s != NATS_OK)
    {
        NATS_FREE(oss);
        return NATS_UPDATE_ERR_STACK(s);
    }
    oss->Description = cfg->Description;
    oss->TTL = NATS_NANOS_TO_MILLIS(cfg->MaxAge);
    oss->Storage = cfg->Storage;
    oss->Replicas = (int) cfg->Replicas;
    oss->Size = info->State.Bytes;
    oss->BackingStore = objBackingStore;
    oss->Metadata.List = cfg->Metadata.List;
    oss->Metadata.Count = cfg->Metadata.Count;
    oss->StreamInfo = info;
    oss->IsCompressed = cfg->Compression != js_StorageCompressionNone;
    *new_oss = oss;
    return NATS_OK;
}

natsStatus
js_ObjectStoreStatuses(objStoreStatusesList **list, jsCtx *js)
{
    natsStatus              s       = NATS_OK;
    jsStreamInfoList        *sil    = NULL;
    objStoreStatusesList    *osl    = NULL;
    jsOptions               opts;
    int                     i;

    if ((list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    jsOptions_Init(&opts);
    opts.Stream.Info.SubjectsFilter = objAllChunksFilter;
    s = js_Streams(&sil, js, &opts, NULL);
    // Return early if there were no streams.
    if (s == NATS_NOT_FOUND)
    {
        // We don't update the error stack for "not found" since we consider
        // this a "normal" error.
        return s;
    }
    osl = (objStoreStatusesList*) NATS_CALLOC(1, sizeof(objStoreStatusesList));
    if (osl == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        // We will allocate based on sil->Count, which may be more than needed
        // if some stream names don't start with "OBJ_".
        osl->List = (objStoreStatus**) NATS_CALLOC(sil->Count, sizeof(char*));
        if (osl->List == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    for (i=0; (s == NATS_OK) && (i<sil->Count); i++)
    {
        jsStreamInfo *info = sil->List[i];

        if (info->Config == NULL)
            continue;

        // Exclude the ones that don't start with "OBJ_".
        if (strstr(info->Config->Name, objNamePrefix) != info->Config->Name)
            continue;

        s = _createObjStoreStatus(&(osl->List[osl->Count]), info);
        // On success remove the stream info from the list so that
        // we don't destroy it on cleanup on exit. Do not decrement
        // sil->Count since there may be gaps in the list if we happen
        // to have some info with name that do not start with "OBJ_".
        if (s == NATS_OK)
        {
            osl->Count++;
            sil->List[i] = NULL;
        }
    }
    // Don't need that anymore.
    jsStreamInfoList_Destroy(sil);
    // If success, but we did not actually add any name (because none started
    // with "OBJ_", switch state to NATS_NOT_FOUND.
    if ((s == NATS_OK) && (osl->Count == 0))
        s = NATS_NOT_FOUND;

    if (s == NATS_OK)
        *list = osl;
    else
    {
        objStoreStatusesList_Destroy(osl);
        // If not found, return error without updating error stack.
        if (s == NATS_NOT_FOUND)
            return s;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

void
objStore_Destroy(objStore *obs)
{
    _releaseObs(obs);
}

//////////////////////////////////////////////////////////////////////////////
// objStoreMeta APIs
//////////////////////////////////////////////////////////////////////////////


natsStatus
objStoreMeta_Init(objStoreMeta *meta)
{
    if (meta == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(meta, 0, sizeof(objStoreMeta));
    return NATS_OK;
}

static void
_destroyObjStoreLink(objStoreLink *link)
{
    if (link == NULL)
        return;

    NATS_FREE((char*) link->Bucket);
    NATS_FREE((char*) link->Name);
    NATS_FREE(link);
}

static void
_destroyObjStoreMeta(objStoreMeta *meta, bool freeObj)
{
    int i;

    if (meta == NULL)
        return;

    NATS_FREE((char*) meta->Name);
    NATS_FREE((char*) meta->Description);
    natsHeader_Destroy(meta->Headers);
    for (i=0; i<2*meta->Metadata.Count; i++)
        NATS_FREE((char*) meta->Metadata.List[i]);
    NATS_FREE((char*) meta->Metadata.List);
    _destroyObjStoreLink(meta->Opts.Link);
    if (freeObj)
        NATS_FREE(meta);
}

static natsStatus
_createObjStoreLink(objStoreLink **new_link)
{
    natsStatus      s       = NATS_OK;
    objStoreLink    *link   = NULL;

    link = (objStoreLink*) NATS_CALLOC(1, sizeof(objStoreLink));
    if (link == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
        *new_link = link;
    else
        _destroyObjStoreLink(link);
    return NATS_UPDATE_ERR_STACK(s);
}

// Will make a deep copy of `meta` into `clone` structure.
// The `clone` structure is already memset to '0's.
static natsStatus
_objStoreMeta_cloneInto(objStoreMeta *clone, objStoreMeta *meta)
{
    natsStatus s = NATS_OK;

    DUP_STRING(s, clone->Name, meta->Name);
    IF_OK_DUP_STRING(s, clone->Description, meta->Description);
    IFOK(s, nats_cloneMetadata(&clone->Metadata, &(meta->Metadata)));
    if (s == NATS_OK)
    {
        clone->Opts.ChunkSize = meta->Opts.ChunkSize;
        if (meta->Opts.Link != NULL)
        {
            s = _createObjStoreLink(&(clone->Opts.Link));
            if ((s == NATS_OK) && !nats_IsStringEmpty(meta->Opts.Link->Bucket))
                DUP_STRING(s, clone->Opts.Link->Bucket, meta->Opts.Link->Bucket);
            if ((s == NATS_OK) && !nats_IsStringEmpty(meta->Opts.Link->Name))
                DUP_STRING(s, clone->Opts.Link->Name, meta->Opts.Link->Name);
        }
    }
    if (s != NATS_OK)
        _destroyObjStoreMeta(clone, false);

    return NATS_UPDATE_ERR_STACK(s);
}

//////////////////////////////////////////////////////////////////////////////
// objStore management APIs
//////////////////////////////////////////////////////////////////////////////

natsStatus
objStoreOptions_Init(objStoreOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(objStoreOptions));
    return NATS_OK;
}

void
objStoreInfo_Destroy(objStoreInfo *info)
{
    if (info == NULL)
        return;

    _destroyObjStoreMeta(&(info->Meta), false);
    NATS_FREE((char*) info->Bucket);
    NATS_FREE((char*) info->NUID);
    NATS_FREE((char*) info->Digest);
    NATS_FREE(info);
}

static natsStatus
_encodeName(char **en, const char *name)
{
    natsStatus  s = nats_Base64URL_EncodeString((const unsigned char*) name, (int) strlen(name), en);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalObjStoreLink(nats_JSON *json, const char *fieldName, objStoreLink **new_link)
{
    natsStatus      s       = NATS_OK;
    nats_JSON       *obj    = NULL;
    objStoreLink    *link   = NULL;

    s = nats_JSONGetObject(json, fieldName, &obj);
    if ((s == NATS_OK) && (obj == NULL))
        return NATS_OK;

    link = (objStoreLink*) NATS_CALLOC(1, sizeof(objStoreLink));
    if (link == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    IFOK(s, nats_JSONGetStr(obj, "bucket", (char**) &(link->Bucket)));
    IFOK(s, nats_JSONGetStr(obj, "name", (char**) &(link->Name)));

    if (s == NATS_OK)
        *new_link = link;
    else
        _destroyObjStoreLink(link);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalObjStoreInfoMetaOptions(nats_JSON *json, const char *fieldName, objStoreMetaOptions *opts)
{
    natsStatus  s       = NATS_OK;
    nats_JSON   *obj    = NULL;

    s = nats_JSONGetObject(json, fieldName, &obj);
    if ((s == NATS_OK) && (obj == NULL))
        return NATS_OK;

    s = _unmarshalObjStoreLink(obj, "link", &(opts->Link));
    IFOK(s, nats_JSONGetUInt32(obj, "max_chunk_size", &(opts->ChunkSize)));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalObjStoreInfo(objStoreInfo **new_info, const char *data, int dataLen)
{
    natsStatus      s       = NATS_OK;
    nats_JSON       *json   = NULL;
    objStoreInfo    *info   = (objStoreInfo*) NATS_CALLOC(1, sizeof(objStoreInfo));

    if (info == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONParse(&json, data, dataLen);
    IFOK(s, nats_JSONGetStr(json, "name", (char**) &(info->Meta.Name)));
    IFOK(s, nats_JSONGetStr(json, "description", (char**) &(info->Meta.Description)));
    IFOK(s, nats_unmarshalHeader(json, "headers", &(info->Meta.Headers)));
    IFOK(s, nats_unmarshalMetadata(json, "metadata", &(info->Meta.Metadata)));
    IFOK(s, _unmarshalObjStoreInfoMetaOptions(json, "options", &(info->Meta.Opts)));
    IFOK(s, nats_JSONGetStr(json, "bucket", (char**) &(info->Bucket)));
    IFOK(s, nats_JSONGetStr(json, "nuid", (char**) &(info->NUID)));
    IFOK(s, nats_JSONGetULong(json, "size", &(info->Size)));
    IFOK(s, nats_JSONGetTime(json, "mtime", &(info->ModTime)));
	IFOK(s, nats_JSONGetUInt32(json, "chunks", &(info->Chunks)));
    IFOK(s, nats_JSONGetStr(json, "digest", (char**) &(info->Digest)));
    IFOK(s, nats_JSONGetBool(json, "deleted", &(info->Deleted)));

    nats_JSONDestroy(json);

    if (s == NATS_OK)
        *new_info = info;
    else
        objStoreInfo_Destroy(info);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalObjStoreInfo(natsBuffer *buf, objStoreInfo *info)
{
    natsStatus s;

    s = natsBuf_AppendByte(buf, '{');
    IFOK(s, nats_marshalString(buf, false, false, "name", info->Meta.Name));
    IFOK(s, nats_marshalString(buf, true, true, "description", info->Meta.Description));
    IFOK(s, nats_marshalHeader(buf, true, true, "headers", info->Meta.Headers));
    IFOK(s, nats_marshalMetadata(buf, true, "metadata", &(info->Meta.Metadata)));
    IFOK(s, natsBuf_Append(buf, ",\"options\":{", -1));
    if (s == NATS_OK)
    {
        bool comma = false;

        if (info->Meta.Opts.ChunkSize > 0)
        {
            comma = true;
            s = nats_marshalULong(buf, false, "max_chunk_size", (uint64_t) info->Meta.Opts.ChunkSize);
        }
        if ((s == NATS_OK) && (info->Meta.Opts.Link != NULL))
        {
            if (comma)
                s = natsBuf_AppendByte(buf, ',');
            IFOK(s, natsBuf_Append(buf, "\"link\":{", -1));
            IFOK(s, nats_marshalString(buf, false, false, "bucket", info->Meta.Opts.Link->Bucket));
            IFOK(s, nats_marshalString(buf, true, true, "name", info->Meta.Opts.Link->Name));
            IFOK(s, natsBuf_AppendByte(buf, '}'));
        }
    }
    IFOK(s, natsBuf_AppendByte(buf, '}'));
    IFOK(s, nats_marshalString(buf, false, true, "bucket", info->Bucket));
    IFOK(s, nats_marshalString(buf, false, true, "nuid", info->NUID));
    IFOK(s, nats_marshalULong(buf, true, "size", info->Size));
    IFOK(s, nats_marshalTimeUTC(buf, true, "mtime", info->ModTime));
    IFOK(s, nats_marshalULong(buf, true, "chunks", (uint64_t) info->Chunks));
    IFOK(s, nats_marshalString(buf, true, true, "digest", info->Digest));
    if ((s == NATS_OK) && info->Deleted)
    {
        s = natsBuf_Append(buf, ",\"deleted\":true", -1);
    }
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_checkElapsed(int64_t *elapsed, int64_t start, int64_t timeout)
{
    *elapsed = nats_Now() - start;
    if (*elapsed > timeout)
        return nats_setDefaultError(NATS_TIMEOUT);
    return NATS_OK;
}

static natsStatus
_getInfo(objStoreInfo **new_info, objStore *obs, int64_t start, int64_t timeout, const char *name, objStoreOptions *opts)
{
    natsStatus      s           = NATS_OK;
    objStoreInfo    *info       = NULL;
    bool            showDeleted = (opts != NULL ? opts->ShowDeleted : false);
    char            *metaSubj   = NULL;
    char            *encName    = NULL;
    int64_t         elapsed     = 0;

    if (new_info == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (nats_IsStringEmpty(name))
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrNameIsRequired);

    s = _encodeName(&encName, name);
    if ((s == NATS_OK) && (nats_asprintf(&metaSubj, objMetaPreTmpl, obs->name, encName) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        natsMsg                 *msg  = NULL;
        jsOptions               jo;
        jsDirectGetMsgOptions   dgmo;

        jsOptions_Init(&jo);
        s = _checkElapsed(&elapsed, start, timeout);
        if (s == NATS_OK)
        {
            jo.Wait = timeout-elapsed;

            jsDirectGetMsgOptions_Init(&dgmo);
            dgmo.LastBySubject = metaSubj;
        }
        IFOK(s, js_DirectGetMsg(&msg, obs->js, obs->streamName, &jo, &dgmo));
        if (s == NATS_OK)
        {
            s = _unmarshalObjStoreInfo(&info, natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
            if (s != NATS_OK)
                s = nats_setError(s, "%s", obsErrBadObjectMeta);
        }
        if (s == NATS_OK)
            info->ModTime = natsMsg_GetTime(msg);
        natsMsg_Destroy(msg);
    }
    if ((s == NATS_OK) && !showDeleted && info->Deleted)
        s = NATS_NOT_FOUND;

    NATS_FREE(encName);
    NATS_FREE(metaSubj);

    if (s == NATS_OK)
        *new_info = info;
    else
    {
        objStoreInfo_Destroy(info);
        if (s == NATS_NOT_FOUND)
        {
            nats_clearLastError();
            return s;
        }
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_GetInfo(objStoreInfo **new_info, objStore *obs, const char *name, objStoreOptions *opts)
{
    natsStatus  s;
    int64_t     start   = 0;
    int64_t     timeout = 0;

    if (obs == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    start = nats_Now();
    s = _getInfo(new_info, obs, start, timeout, name, opts);
    if (s == NATS_NOT_FOUND)
        return s;
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_publishMeta(jsCtx *js, objStoreInfo *info, int64_t timeout)
{
    natsStatus  s       = NATS_OK;;
    char        *subj   = NULL;
    char        *en     = NULL;
    natsMsg     *mm     = NULL;
    natsBuffer  buf;

    s = natsBuf_Init(&buf, 256);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    // marshal the object into json, don't store an actual time
    info->ModTime = 0;
    s = _marshalObjStoreInfo(&buf, info);
    IFOK(s, _encodeName(&en, info->Meta.Name));
    if ((s == NATS_OK) && (nats_asprintf(&subj, objMetaPreTmpl, info->Bucket, en) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    IFOK(s, natsMsg_Create(&mm, subj, NULL, natsBuf_Data(&buf), natsBuf_Len(&buf)));
    IFOK(s, natsMsgHeader_Set(mm, JSMsgRollup, JSMsgRollupSubject));
    if (s == NATS_OK)
    {
        jsPubOptions po;

        jsPubOptions_Init(&po);
        po.MaxWait = timeout;
        js_PublishMsg(NULL, js, mm, &po, NULL);
    }

    natsMsg_Destroy(mm);
    NATS_FREE(subj);
    NATS_FREE(en);
    natsBuf_Cleanup(&buf);

	// set the ModTime in case it's returned to the user, even though it's not the correct time.
	info->ModTime = nats_NowInNanoSeconds();

	return NATS_UPDATE_ERR_STACK(s);
}


natsStatus
objStore_UpdateMeta(objStore *obs, const char *name, objStoreMeta *meta)
{
    natsStatus      s       = NATS_OK;
    objStoreInfo    *info   = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;
    int64_t         timeout = 0;

    if ((obs == NULL) || (meta == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    // Grab the current meta.
    s = _getInfo(&info, obs, start, timeout, name, NULL);
    if (s != NATS_OK)
    {
        if (s == NATS_NOT_FOUND)
            s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrUpdateMetaDelete);

        return NATS_UPDATE_ERR_STACK(s);
    }
    s = _checkElapsed(&elapsed, start, timeout);
    if (s == NATS_OK)
    {
        // If the new name is different from the old, and it exists, error
        // If there was an error that was not NATS_NOT_FOUND, error.
        if (strcmp(name, meta->Name) != 0)
        {
            objStoreInfo    *existingInfo = NULL;
            objStoreOptions so;

            objStoreOptions_Init(&so);
            so.ShowDeleted = true;

            s = _getInfo(&existingInfo, obs, start, timeout, meta->Name, &so);
            if (s == NATS_NOT_FOUND)
                s = NATS_OK;
            else if ((s == NATS_OK) && (!existingInfo->Deleted))
                s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrObjectAlreadyExists);

            objStoreInfo_Destroy(existingInfo);
        }
    }
    if (s == NATS_OK)
    {
        const char      *orgName    = info->Meta.Name;
        const char      *orgDesc    = info->Meta.Description;
        natsHeader      *orgHdr     = info->Meta.Headers;
        natsMetadata    orgMD       = info->Meta.Metadata;

        info->Meta.Name = meta->Name;
        info->Meta.Description = meta->Description;
        info->Meta.Headers = meta->Headers;
        info->Meta.Metadata = meta->Metadata;

        s = _checkElapsed(&elapsed, start, timeout);
        IFOK(s, _publishMeta(obs->js, info, timeout-elapsed));

        // Restore original values so that we don't free user's provided data
        // from the `meta` parameter.
        info->Meta.Name         = orgName;
        info->Meta.Description  = orgDesc;
        info->Meta.Headers      = orgHdr;
        info->Meta.Metadata     = orgMD;
    }
    if (s == NATS_OK)
    {
        // did the name of this object change? We just stored the meta under the new name
        // so delete the meta from the old name via purge stream for subject.
        if (strcmp(name, meta->Name) != 0)
        {
            char *metaSubj  = NULL;
            char *en        = NULL;

            s = _encodeName(&en, name);
            if ((s == NATS_OK) && (nats_asprintf(&metaSubj, objMetaPreTmpl, obs->name, en) < 0))
                s = nats_setDefaultError(NATS_NO_MEMORY);
            IFOK(s, _checkElapsed(&elapsed, start, timeout))
            if (s == NATS_OK)
            {
                jsOptions jo;

                jsOptions_Init(&jo);
                jo.Wait = timeout-elapsed;
                jo.Stream.Purge.Subject = metaSubj;
                s = js_PurgeStream(obs->js, obs->streamName, &jo, NULL);
            }
            NATS_FREE(metaSubj);
            NATS_FREE(en);
        }
    }
    objStoreInfo_Destroy(info);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_Delete(objStore *obs, const char *name)
{
    natsStatus      s;
    objStoreInfo    *info       = NULL;
    char            *chunkSubj  = NULL;
    int64_t         start       = nats_Now();
    int64_t         elapsed     = 0;
    int64_t         timeout     = 0;

    if (obs == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    s = _getInfo(&info, obs, start, timeout, name, NULL);
    if (s != NATS_OK)
    {
        if (s == NATS_NOT_FOUND)
            return s;

        return NATS_UPDATE_ERR_STACK(s);
    }
    if (nats_IsStringEmpty(info->NUID))
        s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrBadObjectMeta);
    IFOK(s, _checkElapsed(&elapsed, start, timeout));
    if (s == NATS_OK)
    {
        const char *orgDigest = info->Digest;

        // Place a rollup delete marker and publis the info.
        info->Deleted = true;
        info->Size    = 0;
        info->Chunks  = 0;
        info->Digest  = NULL;
        s = _publishMeta(obs->js, info, timeout-elapsed);

        // Restore original digest for proper cleanup.
        info->Digest = orgDigest;
    }
    if ((s == NATS_OK) && (nats_asprintf(&chunkSubj, objChunksPreTmpl, obs->name, info->NUID) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    IFOK(s, _checkElapsed(&elapsed, start, timeout));
    if (s == NATS_OK)
    {
        jsOptions jo;

        jsOptions_Init(&jo);
        jo.Stream.Purge.Subject = chunkSubj;
        jo.Wait = timeout-elapsed;
        s = js_PurgeStream(obs->js, obs->streamName, &jo, NULL);
    }

    objStoreInfo_Destroy(info);
    NATS_FREE(chunkSubj);

    return NATS_UPDATE_ERR_STACK(s);
}

static bool
_isLink(objStoreInfo *info)
{
    return info->Meta.Opts.Link != NULL;
}

static natsStatus
_addLink(objStoreInfo **new_info, objStore *obs, const char *name, const char *objBucket, const char *objName)
{
    natsStatus      s       = NATS_OK;
    objStoreInfo    *einfo  = NULL;
    objStoreInfo    *info   = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;
    int64_t         timeout = 0;
    char            nuid[NUID_BUFFER_LEN+1];
    objStoreOptions o;

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    // If object with link's name is found, error.
	// If link with link's name is found, that's okay to overwrite.
	// If there was an error that was not NATS_NOT_FOUND, error.
    objStoreOptions_Init(&o);
    o.ShowDeleted = true;
    s = _getInfo(&einfo, obs, start, timeout, name, &o);
    if ((s == NATS_OK) && !_isLink(einfo))
        s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrObjectAlreadyExists);
    else if (s == NATS_NOT_FOUND)
        s = NATS_OK;

    // create the info/meta for the link
    if (s == NATS_OK)
    {
        info = (objStoreInfo*) NATS_CALLOC(1, sizeof(objStoreInfo));
        if (info == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            info->ModTime = nats_NowInNanoSeconds();
            info->Meta.Opts.Link = (objStoreLink*) NATS_CALLOC(1, sizeof(objStoreLink));
            if (info->Meta.Opts.Link == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
    }
    IF_OK_DUP_STRING(s, info->Meta.Name, name);
    IF_OK_DUP_STRING(s, info->Meta.Opts.Link->Bucket, objBucket);
    if ((s == NATS_OK) && !nats_IsStringEmpty(objName))
        DUP_STRING(s, info->Meta.Opts.Link->Name, objName);
    IF_OK_DUP_STRING(s, info->Bucket, obs->name);
    IFOK(s, natsNUID_Next(nuid, sizeof(nuid)));
    IF_OK_DUP_STRING(s, info->NUID, nuid);
    IFOK(s, _checkElapsed(&elapsed, start, timeout));

    // Put the link object
    IFOK(s, _publishMeta(obs->js, info, timeout-elapsed));

    // Destroy existing info object.
    objStoreInfo_Destroy(einfo);

    // IF ok and user wants the info back, return it.
    if ((s == NATS_OK) && (new_info != NULL))
    {
        *new_info = info;
        return NATS_OK;
    }

    // User did not want or there was an error, destroy it.
    objStoreInfo_Destroy(info);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_AddLink(objStoreInfo **new_info, objStore *obs, const char *name, objStoreInfo *obj)
{
    natsStatus s;

    if ((obs == NULL) || (obj == NULL) || nats_IsStringEmpty(obj->Meta.Name))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (obj->Deleted)
        return nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrNoLinkToDeleted);
    if (_isLink(obj))
        return nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrNoLinkToLink);

    s = _addLink(new_info, obs, name, obj->Bucket, obj->Meta.Name);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_AddBucketLink(objStoreInfo **new_info, objStore *obs, const char *name, objStore *bucket)
{
    natsStatus s;

    if ((obs == NULL) || (bucket == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _addLink(new_info, obs, name, bucket->name, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_Seal(objStore *obs)
{
    natsStatus      s;
    jsStreamInfo    *si     = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;
    int64_t         timeout = 0;

    if (obs == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    s = js_GetStreamInfo(&si, obs->js, obs->streamName, NULL, NULL);
    IFOK(s, _checkElapsed(&elapsed, start, timeout));
    if (s == NATS_OK)
    {
        jsOptions jo;

        jsOptions_Init(&jo);
        jo.Wait = timeout-elapsed;

        si->Config->Sealed = true;
        s = js_UpdateStream(NULL, obs->js, si->Config, &jo, NULL);
    }

    jsStreamInfo_Destroy(si);

    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_freeWatcher(objStoreWatcher *w)
{
    objStore *obs = w->obs;
    natsSubscription_Destroy(w->sub);
    natsMutex_Destroy(w->mu);
    NATS_FREE(w);
    _releaseObs(obs);
}

static void
_releaseWatcher(objStoreWatcher *w)
{
    bool doFree;

    if (w == NULL)
        return;

    natsMutex_Lock(w->mu);
    doFree = (--(w->refs) == 0);
    natsMutex_Unlock(w->mu);

    if (doFree)
        _freeWatcher(w);
}

natsStatus
objStoreWatcher_Stop(objStoreWatcher *w)
{
    natsStatus s = NATS_OK;

    if (w == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    natsMutex_Lock(w->mu);
    if (!w->stopped)
    {
        w->stopped = true;
        s = natsSubscription_Unsubscribe(w->sub);
    }
    natsMutex_Unlock(w->mu);

    return NATS_UPDATE_ERR_STACK(s);
}

void
objStoreWatcher_Destroy(objStoreWatcher *w)
{
    objStoreWatcher_Stop(w);
    _releaseWatcher(w);
}

natsStatus
objStoreWatchOptions_Init(objStoreWatchOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(objStoreWatchOptions));
    return NATS_OK;
}

natsStatus
objStore_Watch(objStoreWatcher **new_watcher, objStore *obs, objStoreWatchOptions *opts)
{
    natsStatus              s           = NATS_OK;
    objStoreWatcher         *w          = NULL;
    char                    *allMeta    = NULL;
    int64_t                 start       = nats_Now();
    int64_t                 elapsed     = 0;
    int64_t                 timeout     = 0;
    objStoreWatchOptions    *o          = NULL;
    objStoreWatchOptions    lo;

    if ((new_watcher == NULL) || (obs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    objStoreWatchOptions_Init(&lo);
    o = (opts == NULL ? &lo : opts);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    w = (objStoreWatcher*) NATS_CALLOC(1, sizeof(objStoreWatcher));
    if (w == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    _retainObs(obs);
    w->obs          = obs;
    w->refs         = 1;
    w->ignoreDel    = o->IgnoreDeletes;
    s = natsMutex_Create(&w->mu);
    if ((s == NATS_OK) && (nats_asprintf(&allMeta, objAllMetaPreTmpl, obs->name) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);
    if (s == NATS_OK)
    {
        jsErrCode   jErr = 0;
        natsMsg     *msg = NULL;

        // if there are no messages on the stream and we are not watching
	    // updates only, we will send the marker.
        s = js_GetLastMsg(&msg, obs->js, obs->streamName, allMeta, NULL, &jErr);
        if (!o->UpdatesOnly)
        {
            if (s == NATS_NOT_FOUND)
            {
                nats_clearLastError();
                s = NATS_OK;
                w->initDone  = true;
                w->retMarker = true;
            }
        }
        else if (s == NATS_OK)
        {
            w->initDone = true;
        }
        natsMsg_Destroy(msg);
    }
    IFOK(s, _checkElapsed(&elapsed, start, timeout))
    {
        jsOptions       jo;
        jsSubOptions    so;

        jsOptions_Init(&jo);
        jo.Wait = timeout-elapsed;

        // Used ordered consumer to deliver results.
        jsSubOptions_Init(&so);
        so.Ordered = true;
        so.Stream  = obs->streamName;
        if (o->UpdatesOnly)
            so.Config.DeliverPolicy = js_DeliverNew;
        s = js_SubscribeSync(&(w->sub), obs->js, allMeta, &jo, &so, NULL);
    }
    if (s == NATS_OK)
        *new_watcher = w;
    else
        _freeWatcher(w);

    NATS_FREE(allMeta);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStoreWatcher_Next(objStoreInfo **new_info, objStoreWatcher *w, int64_t timeout)
{
    natsStatus      s       = NATS_OK;
    objStoreInfo    *info   = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;

    if ((new_info == NULL) || (w == NULL) || (timeout <= 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    *new_info = NULL;

    natsMutex_Lock(w->mu);
GET_NEXT:
    if (w->stopped)
    {
        s = nats_setDefaultError(NATS_ILLEGAL_STATE);
    }
    if (w->retMarker)
    {
        // We return a NULL info and switch this OFF so we don't get back here.
        w->retMarker = false;
    }
    else
    {
        natsMsg     *msg    = NULL;
        int64_t     tm      = 0;
        uint64_t    pending = 0;
        bool        next    = false;

        // Get the next message.
        w->refs++;
        natsMutex_Unlock(w->mu);

        s = natsSubscription_NextMsg(&msg, w->sub, timeout-elapsed);

        natsMutex_Lock(w->mu);
        if (w->stopped)
        {
            natsMutex_Unlock(w->mu);
            _releaseWatcher(w);
            return NATS_ILLEGAL_STATE;
        }
        w->refs--;
        IFOK(s, _unmarshalObjStoreInfo(&info, natsMsg_GetData(msg), natsMsg_GetDataLength(msg)));
        IFOK(s, js_getMetaData(natsMsg_GetReply(msg), NULL, NULL, NULL, NULL, NULL, NULL, &tm, &pending, 2));
        if (s == NATS_OK)
        {
            if (!w->ignoreDel || !info->Deleted)
            {
                info->ModTime = tm;
            }
            else
            {
                objStoreInfo_Destroy(info);
                info = NULL;
                // Check remaining time.
                s = _checkElapsed(&elapsed, start, timeout);
                if (s == NATS_OK)
                    next = true;
            }
            if (!w->initDone && pending == 0)
            {
                w->initDone  = true;
                w->retMarker = true;
            }
        }
        natsMsg_Destroy(msg);

        if (next)
            goto GET_NEXT;
    }
    natsMutex_Unlock(w->mu);

    if (s == NATS_OK)
        *new_info = info;

    return NATS_UPDATE_ERR_STACK(s);
}

void
objStoreInfoList_Destroy(objStoreInfoList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        objStoreInfo_Destroy(list->List[i]);
    NATS_FREE(list->List);
    NATS_FREE(list);
}

int obsInitialListCap = obsInitialListCapValue;

natsStatus
objStore_List(objStoreInfoList **new_list, objStore *obs, objStoreOptions *opts)
{
    natsStatus              s       = NATS_OK;
    objStoreInfoList        *list   = NULL;
    objStoreWatcher         *w      = NULL;
    int64_t                 start   = nats_Now();
    int64_t                 elapsed = 0;
    int64_t                 timeout = 0;
    int                     count   = 0;
    int                     cap     = obsInitialListCap;
    objStoreWatchOptions    o;

    if ((new_list == NULL) || (obs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    objStoreWatchOptions_Init(&o);
    // If not option to ask for "ShowDeleted", then we ignore them.
    if ((opts == NULL) || !opts->ShowDeleted)
        o.IgnoreDeletes = true;

    s = objStore_Watch(&w, obs, &o);
    while ((s == NATS_OK) && ((s = _checkElapsed(&elapsed, start, timeout)) == NATS_OK))
    {
        objStoreInfo *info = NULL;

        s = objStoreWatcher_Next(&info, w, timeout-elapsed);
        if ((s == NATS_OK) && (info == NULL))
        {
            break;
        }
        else if (s == NATS_OK)
        {
            if (list == NULL)
            {
                list = (objStoreInfoList*) NATS_CALLOC(1, sizeof(objStoreInfo));
                if (list == NULL)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            if ((s == NATS_OK) && ((list->List == NULL) || (count == cap)))
            {
                if (list->List != NULL)
                    cap *= 2;

                list->List = (objStoreInfo**) NATS_REALLOC(list->List, cap * sizeof(objStoreInfo*));
                if (list->List == NULL)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
                else
                {
                    int i;
                    for (i=count; i<cap; i++)
                        list->List[i] = NULL;
                }
            }
            if (s == NATS_OK)
            {
                list->List[count++] = info;
                list->Count++;
            }
        }
    }
    // This will also take care of stopping it.
    objStoreWatcher_Destroy(w);

    if (s == NATS_OK)
    {
        if (count == 0)
        {
            objStoreInfoList_Destroy(list);
            return NATS_NOT_FOUND;
        }
        *new_list = list;
    }
    else
        objStoreInfoList_Destroy(list);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_Status(objStoreStatus **new_sts, objStore *obs)
{
    natsStatus      s       = NATS_OK;
    jsStreamInfo    *si     = NULL;

    if ((new_sts == NULL) || (obs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = js_GetStreamInfo(&si, obs->js, obs->streamName, NULL, NULL);
    IFOK(s, _createObjStoreStatus(new_sts, si));

    if (s != NATS_OK)
    {
        jsStreamInfo_Destroy(si);
        if (s == NATS_NOT_FOUND)
            return s;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

//////////////////////////////////////////////////////////////////////////////
// objStore PUT APIs
//////////////////////////////////////////////////////////////////////////////

static void
_freePut(objStorePut *put)
{
    objStoreInfo_Destroy(put->info);
    NATS_FREE(put->metaSubj);
    NATS_FREE(put->echunkSubj);
    NATS_FREE(put->chunkSubj);
    NATS_FREE(put->errTxt);
    nats_hashDestroy(put->h);
    _releaseObs(put->obs);
    natsMutex_Destroy(put->mu);
    NATS_FREE(put);
}

static void
_setPutErr(objStorePut *put, natsStatus err, char *errTxt)
{
    natsMutex_Lock(put->mu);
    if (put->err == NATS_OK)
    {
        put->err    = err;
        put->errTxt = errTxt;
    }
    else
        NATS_FREE(errTxt);
    natsMutex_Unlock(put->mu);
}

static natsStatus
_getPutErr(objStorePut *put)
{
    natsStatus s = NATS_OK;

    natsMutex_Lock(put->mu);
    if (put->err != NATS_OK)
    {
        if (nats_IsStringEmpty(put->errTxt))
            s = nats_setDefaultError(put->err);
        else
            s = nats_setError(put->err, "%s", put->errTxt);
    }
    natsMutex_Unlock(put->mu);
    return s;
}

static void
_putErrHandler(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    objStorePut *put    = (objStorePut*) closure;
    char        *errTxt = NULL;

    if ((!nats_IsStringEmpty(pae->ErrText)) &&
        (nats_asprintf(&errTxt, "%s (%d)", pae->ErrText, pae->ErrCode) < 0))
    {
        // Nothing we can do. We can't use static error text here
        // because errTxt would be freed later.
        errTxt = NULL;
    }
    _setPutErr(put, pae->Err, errTxt);
}

static void
_retainPut(objStorePut *put)
{
    natsMutex_Lock(put->mu);
    put->refs++;
    natsMutex_Unlock(put->mu);
}

static void
_releasePut(objStorePut *put)
{
    int refs;

    natsMutex_Lock(put->mu);
    refs = --(put->refs);
    natsMutex_Unlock(put->mu);
    if (refs == 0)
        _freePut(put);
}

static natsStatus
_getDigestValue(char **digest, nats_hash *h)
{
    natsStatus      s   = NATS_OK;
    char            *d  = NULL;
    unsigned int    len = 0;
    unsigned char   val[NATS_HASH_MAX_LEN];

    s = nats_hashSum(h, val, &len);
    IFOK(s, nats_Base64URL_EncodeString((const unsigned char*) val, (int) len, &d));
    if ((s == NATS_OK) && (nats_asprintf(digest, objDigestTmpl, d)) < 0)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    NATS_FREE(d);
    return NATS_UPDATE_ERR_STACK(s);
}

static void
_purgeChunks(objStorePut *put, int64_t timeout)
{
    jsOptions       opts;
    jsPubOptions    po;
    int64_t         start;
    int64_t         remaining;

    jsOptions_Init(&opts);
    jsPubOptions_Init(&po);

    // We will not update the error stack for those calls.
    nats_doNotUpdateErrStack(true);
    // Keep track of when we start.
    start = nats_Now();
    // Use the full timeout to wait for pub async to complete.
    po.MaxWait = timeout;
    js_PublishAsyncComplete(put->pubJS, &po);

    // Set the purge subject
    opts.Stream.Purge.Subject = put->chunkSubj;
    // Compute the remaining time, but use a minimum for the purge call.
    remaining = timeout - (nats_Now() - start);
    if (remaining < 1000)
        remaining = 1000;
    opts.Wait = remaining;
    js_PurgeStream(put->obs->js, put->obs->streamName, &opts, NULL);
    nats_doNotUpdateErrStack(false);
}

natsStatus
objStorePut_Add(objStorePut *put, const void *data, int dataLen)
{
    natsStatus  s           = NATS_OK;
    int         remaining   = dataLen;
    int         off         = 0;
    int         chunkSize   = 0;

    if ((put == NULL) || ((data == NULL) && (dataLen > 0)) ||
        ((data != NULL) && (dataLen == 0)) || (dataLen < 0))
    {
        return nats_setDefaultError(NATS_INVALID_ARG);
    }

    s = _getPutErr(put);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (dataLen == 0)
        return NATS_OK;

    chunkSize = (int) put->info->Meta.Opts.ChunkSize;

    do {
        int         size = (remaining > chunkSize ? chunkSize : remaining);
        const void  *chd = (const void*) (((const char *)data)+off);

        // Indicate that we are doing a publish, so if there is error before
        // completion, then we will need to purge the partial chunks.
        put->pcof = true;

        // Update hash.
        s = nats_hashWrite(put->h, chd, size);

        // Send the message itself.
        IFOK(s, js_PublishAsync(put->pubJS, put->chunkSubj, chd, size, NULL));
        if (s == NATS_OK)
        {
            // Update totals.
            put->sent++;
            put->total += (uint64_t) size;

            // Check for async pub error.
            s = _getPutErr(put);
            if (s != NATS_OK)
                return NATS_UPDATE_ERR_STACK(s);
        }
        if (s == NATS_OK)
        {
            off += size;
            remaining -= size;
        }
    } while ((s == NATS_OK) && (remaining > 0));

    if (s != NATS_OK)
        _setPutErr(put, s, NULL);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStorePut_Complete(objStoreInfo **new_info, objStorePut *put, int64_t timeout)
{
    natsStatus  s       = NATS_OK;
    natsMsg     *mm     = NULL;
    int64_t     start   = nats_Now();
    int64_t     elapsed = 0;
    natsBuffer  buf;

    if ((put == NULL) || (timeout < 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (timeout == 0)
    {
        js_lock(put->pubJS);
        timeout = put->pubJS->opts.Wait;
        js_unlock(put->pubJS);
    }

    s = _getPutErr(put);
    if (s != NATS_OK)
    {
        _purgeChunks(put, timeout);
        return NATS_UPDATE_ERR_STACK(s);
    }

    jsPubOptions po;

    jsPubOptions_Init(&po);
    po.MaxWait = timeout;

    // Place meta info.
    put->info->Size      = put->total;
    put->info->Chunks    = put->sent;

    // Initialize the buffer.
    s = natsBuf_Init(&buf, 512);

    // Compute the digest
    IFOK(s, _getDigestValue((char **) &(put->info->Digest), put->h));

    // Marshal the info object into buffer.
    IFOK(s, _marshalObjStoreInfo(&buf, put->info));

    // Create the meta message.
    IFOK(s, natsMsg_Create(&mm, put->metaSubj, NULL, natsBuf_Data(&buf), natsBuf_Len(&buf)));
    IFOK(s, natsMsgHeader_Set(mm, JSMsgRollup, JSMsgRollupSubject));
    IFOK(s, js_PublishMsgAsync(put->pubJS, &mm, &po));
    if (s == NATS_OK)
    {
        s = _checkElapsed(&elapsed, start, timeout);
        if (s == NATS_OK)
            po.MaxWait = timeout-elapsed;
        // Wait for all to be processed.
        IFOK(s, js_PublishAsyncComplete(put->pubJS, &po));
        // Check for async error
        if (s == NATS_OK)
            s = _getPutErr(put);
    }
    if (s == NATS_OK)
    {
        // This time is not actually the correct time.
        put->info->ModTime = nats_NowInNanoSeconds();

        // Delete any original chunks if needed.
        if (put->echunkSubj != NULL)
        {
            jsOptions opts;

            jsOptions_Init(&opts);
            opts.Stream.Purge.Subject = put->echunkSubj;
            if (_checkElapsed(&elapsed, start, timeout) != NATS_OK)
            {
                // We have already expired, use a nominal low timeout.
                opts.Wait = 1000;
            }
            else
                opts.Wait = timeout-elapsed;
            nats_doNotUpdateErrStack(true);
            js_PurgeStream(put->obs->js, put->obs->streamName, &opts, NULL);
            nats_doNotUpdateErrStack(false);
        }

        // Return the info if requested (and dissociate from the put object).
        if (new_info != NULL)
        {
            *new_info = put->info;
            put->info = NULL;
        }
        put->pcof = false;
    }
    if (s != NATS_OK)
    {
        // If we failed (say waiting for pub async to complete), we could have
        // already exhausted the timeout, so if that is the case, let's use
        // a 1 second timeout to try purge partials.
        elapsed = nats_Now() - start;
        if (elapsed < timeout)
            timeout = timeout-elapsed;
        else
            timeout = 1000;

        _purgeChunks(put, timeout);
    }

    natsMsg_Destroy(mm);
    natsBuf_Cleanup(&buf);

    return NATS_UPDATE_ERR_STACK(s);
}

void
objStorePut_Destroy(objStorePut *put)
{
    if (put == NULL)
        return;

    if (put->pcof)
        _purgeChunks(put, jsDefaultRequestWait);

    jsCtx_Destroy(put->pubJS);

    _releasePut(put);
}

static void
_onPutJSReleased(void *arg)
{
    objStorePut *put = (objStorePut*) arg;

    _releasePut(put);
}

natsStatus
objStore_Put(objStorePut **new_put, objStore *obs, objStoreMeta *pMeta)
{
    natsStatus      s           = NATS_OK;
    objStorePut     *put        = NULL;
    objStoreMeta    *meta       = NULL;
    objStoreInfo    *einfo      = NULL;
    char            *encMetaName= NULL;
    char            nuid[NUID_BUFFER_LEN+1];

    if ((new_put == NULL) || (obs == NULL) || (pMeta == NULL))
       return nats_setDefaultError(NATS_INVALID_ARG);

    if (nats_IsStringEmpty(pMeta->Name))
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrBadObjectMeta);

    if (pMeta->Opts.Link != NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", obsErrLinkNotAllowed);

    put = (objStorePut*) NATS_CALLOC(1, sizeof(objStorePut));
    if (put == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = natsMutex_Create(&(put->mu));
    if (s != NATS_OK)
    {
        NATS_FREE(put);
        return NATS_UPDATE_ERR_STACK(s);
    }

    put->refs = 1;
    put->obs  = obs;
    _retainObs(obs);
    put->info = (objStoreInfo*) NATS_CALLOC(1, sizeof(objStoreInfo));
    if (put->info == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);
    // Clone the user provided meta into our info object.
    if (s == NATS_OK)
    {
        // Point to the embedded Meta structure in the objStoreInfo structure,
        meta = &(put->info->Meta);
        // Make a deep copy into it.
        s = _objStoreMeta_cloneInto(meta, pMeta);
        if ((s == NATS_OK) && (meta->Opts.ChunkSize == 0))
            meta->Opts.ChunkSize = obsDefaultChunkSize;
    }
    // Set the info's bucket name based on our object store name.
    IF_OK_DUP_STRING(s, put->info->Bucket, obs->name);

    // Create the new nuid so chunks go on a new subject if the name is re-used.
    IFOK(s, natsNUID_Next(nuid, sizeof(nuid)));
    IF_OK_DUP_STRING(s, put->info->NUID, nuid);
    if ((s == NATS_OK) && (nats_asprintf(&(put->chunkSubj), objChunksPreTmpl, obs->name, nuid) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);

    // Encode the meta name, which is going to be used for creating the meta subject.
    IFOK(s, _encodeName(&encMetaName, meta->Name));
    if ((s == NATS_OK) && (nats_asprintf(&(put->metaSubj), objMetaPreTmpl, obs->name, encMetaName) < 0))
        s = nats_setDefaultError(NATS_NO_MEMORY);

    // Create our own JS context to handle errors etc.
    if (s == NATS_OK)
    {
        jsOptions pubJSOpts;

        jsOptions_Init(&pubJSOpts);
        pubJSOpts.PublishAsync.ErrHandler           = _putErrHandler;
        pubJSOpts.PublishAsync.ErrHandlerClosure    = (void*) put;

        s = natsConnection_JetStream(&(put->pubJS), obs->js->nc, &pubJSOpts);
        if (s == NATS_OK)
        {
            _retainPut(put);
            js_setOnReleasedCb(put->pubJS, _onPutJSReleased, (void*) put);
        }
    }

    // Create hash for digest.
    IFOK(s, nats_hashNew(&(put->h)));

    // Grab existing meta info (einfo). Ok to be found or not found, any other error is a problem.
	if (s == NATS_OK)
	{
        objStoreOptions so;

        objStoreOptions_Init(&so);
        so.ShowDeleted = true;
	    s = objStore_GetInfo(&einfo, obs, meta->Name, &so);
        // If there is an existing info and it was not deleted, collect the chunk
        // subject which will be the deciding factor to purge the old chunks when
        // adding data iscomplete.
        if ((s == NATS_OK) && (!einfo->Deleted))
        {
            if (nats_asprintf(&(put->echunkSubj), objChunksPreTmpl, obs->name, einfo->NUID) < 0)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else if (s == NATS_NOT_FOUND)
		    s = NATS_OK;
	}

    if (s == NATS_OK)
        *new_put = put;
    else
        objStorePut_Destroy(put);

    NATS_FREE(encMetaName);
    objStoreInfo_Destroy(einfo);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_PutString(objStoreInfo **new_info, objStore *obs, const char *name, const char *data)
{
    natsStatus      s       = NATS_OK;
    objStorePut     *put    = NULL;
    int             len     = 0;
    objStoreMeta    meta;

    if (nats_IsStringEmpty(data))
        data = NULL;
    else
        len = (int) strlen(data);

    objStoreMeta_Init(&meta);
    meta.Name = name;
    s = objStore_Put(&put, obs, &meta);
    IFOK(s, objStorePut_Add(put, (const void*) data, len));
    IFOK(s, objStorePut_Complete(new_info, put, 0));
    objStorePut_Destroy(put);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_PutBytes(objStoreInfo **new_info, objStore *obs, const char *name, const void *data, int dataLen)
{
    natsStatus      s       = NATS_OK;
    objStorePut     *put    = NULL;
    objStoreMeta    meta;

    objStoreMeta_Init(&meta);
    meta.Name = name;
    s = objStore_Put(&put, obs, &meta);
    IFOK(s, objStorePut_Add(put, data, dataLen));
    IFOK(s, objStorePut_Complete(new_info, put, 0));
    objStorePut_Destroy(put);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_PutFile(objStoreInfo **new_info, objStore *obs, const char *fileName)
{
    natsStatus      s       = NATS_OK;
    FILE            *f      = NULL;
    objStorePut     *put    = NULL;
    void            *chunk  = NULL;
    objStoreMeta    meta;

    if ((obs == NULL) || nats_IsStringEmpty(fileName))
        return nats_setDefaultError(NATS_INVALID_ARG);

    f = fopen(fileName, "r");
    if (f == NULL)
        return nats_setError(NATS_ERR, "error opening file '%s': %d (%s)",
                             fileName, errno, strerror(errno));

    objStoreMeta_Init(&meta);
    meta.Name = fileName;
    s = objStore_Put(&put, obs, &meta);
    if (s == NATS_OK)
    {
        chunk = NATS_MALLOC(obsDefaultChunkSize);
        if (chunk == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    while (s == NATS_OK)
    {
        size_t n = fread(chunk, 1, (size_t) obsDefaultChunkSize, f);
        if (n == 0)
            break;

        s = objStorePut_Add(put, (const void*) chunk, (int) n);
    }
    IFOK(s, objStorePut_Complete(new_info, put, 0));

    fclose(f);
    objStorePut_Destroy(put);
    NATS_FREE(chunk);

    return NATS_UPDATE_ERR_STACK(s);
}

//////////////////////////////////////////////////////////////////////////////
// objStore GET APIs
//////////////////////////////////////////////////////////////////////////////

void
objStoreGet_Destroy(objStoreGet *get)
{
    if (get == NULL)
        return;

    objStoreInfo_Destroy(get->info);
    nats_hashDestroy(get->digest);
    natsSubscription_Destroy(get->sub);
    _releaseObs(get->obs);
    NATS_FREE(get);
}

static natsStatus
_get(objStoreGet **new_get, objStore *obs, int64_t start, int64_t timeout, const char *name, objStoreOptions *opts)
{
    natsStatus      s           = NATS_OK;
    objStoreInfo    *info       = NULL;
    objStoreGet     *get        = NULL;
    char            *chunkSubj  = NULL;

    // Grab meta info.
    s = _getInfo(&info, obs, start, timeout, name, opts);
    if (s != NATS_OK)
    {
        if (s == NATS_NOT_FOUND)
            return s;
        return NATS_UPDATE_ERR_STACK(s);
    }
    if (nats_IsStringEmpty(info->NUID))
        s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrBadObjectMeta);
    else if (nats_asprintf(&chunkSubj, objChunksPreTmpl, obs->name, info->NUID) < 0)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    // Check for object links. If single objects we do a pass through.
    if ((s == NATS_OK) && _isLink(info))
    {
        // We know that info->Meta.Opts.Link != NULL.
        if (nats_IsStringEmpty(info->Meta.Opts.Link->Name))
            s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrCantGetBucket);
        if (s == NATS_OK)
        {
            const char *lbuck = info->Meta.Opts.Link->Bucket;

            if (nats_IsStringEmpty(lbuck))
                s = nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrBadObjectMeta);
            else
            {
                // is the link in the same bucket?
                if (strcmp(lbuck, obs->name) == 0)
                {
                    s = _get(new_get, obs, start, timeout, info->Meta.Opts.Link->Name, opts);
                }
                else
                {
                    objStore *lobs = NULL;

                    // different bucket
                    s = js_ObjectStore(&lobs, obs->js, lbuck);
                    if (s == NATS_OK)
                    {
                        s = _get(new_get, lobs, start, timeout, info->Meta.Opts.Link->Name, opts);

                        // Destroy the `lobs` object now.
                        objStore_Destroy(lobs);
                    }
                }
            }
        }
        // Destroy the meta's info object we got at the top of this function.
        objStoreInfo_Destroy(info);

        // Free this now.
        NATS_FREE(chunkSubj);

        if (s == NATS_NOT_FOUND)
            return s;

        return NATS_UPDATE_ERR_STACK(s);
	}
    if (s == NATS_OK)
    {
        get = (objStoreGet*) NATS_CALLOC(1, sizeof(objStoreGet));
        if (get == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
            _retainObs(obs);
    }
    if (s == NATS_OK)
    {
        get->obs         = obs;
        get->info        = info;
        get->remaining   = info->Size;

        if (info->Size == 0)
        {
            // Free this now.
            NATS_FREE(chunkSubj);

            *new_get = get;
            return NATS_OK;
        }
        // Since now `get` owns `info`, let's clear `info` reference so that we can
        // do proper cleanup at the end in case of error.
        info = NULL;
    }
    IFOK(s, nats_hashNew(&(get->digest)));
    if (s == NATS_OK)
    {
        jsOptions       jo;
        jsSubOptions    so;
        int64_t         elapsed = 0;

        s = _checkElapsed(&elapsed, start, timeout);
        if (s == NATS_OK)
        {
            jsOptions_Init(&jo);
            jo.Wait = timeout-elapsed;

            jsSubOptions_Init(&so);
            so.Ordered = true;
            so.Stream = obs->streamName;
            // Use pushJS here.
            s = js_SubscribeSync(&(get->sub), obs->pushJS, chunkSubj, &jo, &so, NULL);
            IFOK(s, natsSubscription_SetPendingLimits(get->sub, -1, -1));
        }
    }
    // Free this, regardless of status.
    NATS_FREE(chunkSubj);

    // Success, return the `get` object.
    if (s == NATS_OK)
        *new_get = get;
    else
    {
        // In case it was not associated with the `get` object.
        objStoreInfo_Destroy(info);
        objStoreGet_Destroy(get);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_Get(objStoreGet **new_get, objStore *obs, const char *name, objStoreOptions *opts)
{
    natsStatus  s       = NATS_OK;
    int64_t     start   = nats_Now();
    int64_t     timeout = 0;

    if ((new_get == NULL) || (obs == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

     // Get the max duration based on the js context's Wait time (expressed in ms).
    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    s = _get(new_get, obs, start, timeout, name, opts);
    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStoreGet_Info(const objStoreInfo **info, objStoreGet *get)
{
    if ((info == NULL) || (get == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    *info = get->info;
    return NATS_OK;
}

static natsStatus
_readInto(bool *done, void **new_data, void *pdata, int *dataLen, objStoreGet *get, bool alloc, int64_t timeout)
{
    natsStatus  s       = NATS_OK;
    natsMsg     *msg    = NULL;
    void        *data   = NULL;
    int         len     = 0;

    // Check if we are done, if so, return error.
    if (get->done)
        return nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrReadComplete);

    // It could be that there is no data to read.
    if (get->remaining == 0)
    {
        *done = true;
        get->done = true;
        return NATS_OK;
    }

    s = natsSubscription_NextMsg(&msg, get->sub, timeout);
    if (s == NATS_OK)
    {
        const char *mdata = natsMsg_GetData(msg);

        len = natsMsg_GetDataLength(msg);
        if ((uint64_t) len > get->remaining)
            s = nats_setError(NATS_ILLEGAL_STATE, "expected remaining %" PRIu64 " bytes, got %d", get->remaining, len);
        else
        {
            if (alloc)
            {
                data = NATS_MALLOC(len);
                if (data == NULL)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            else
            {
                data = pdata;
            }
            if (s == NATS_OK)
            {
                s = nats_hashWrite(get->digest, (const void*) mdata, len);
                if (s == NATS_OK)
                {
                    memcpy(data, (const void*) mdata, len);
                    get->remaining -= (uint64_t) len;
                }
            }
        }
    }
    if (s == NATS_OK)
    {
        // Mark as done if no more bytes to read.
        get->done = (get->remaining == 0 ? true : false);
        if (get->done)
        {
            char *digest = NULL;

            s = _getDigestValue(&digest, get->digest);
            if ((s == NATS_OK) && (strcmp((const char*) digest, get->info->Digest) != 0))
                s = nats_setError(NATS_ERR, "%s", obsErrDigestMismatch);

            NATS_FREE(digest);
        }
    }
    natsMsg_Destroy(msg);
    if (s == NATS_OK)
    {
        if (alloc)
            *new_data = data;
        *dataLen = len;
        *done    = get->done;
    }
    else if (alloc)
    {
        NATS_FREE(data);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStoreGet_Read(bool *done, void **new_data, int *dataLen, objStoreGet *get, int64_t timeout)
{
    natsStatus s = NATS_OK;

    if ((done == NULL) || (new_data == NULL) || (dataLen == NULL) || (get == NULL) || (timeout <= 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    // Initialize the output.
    *new_data   = NULL;
    *dataLen    = 0;
    *done       = false;

    s = _readInto(done, new_data, NULL, dataLen, get, true, timeout);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_readAll(void **new_data, int *dataLen, objStoreGet *get, bool forString, int64_t timeout)
{
    natsStatus  s       = NATS_OK;
    int64_t     start   = nats_Now();
    int64_t     elapsed = 0;
    void        *data   = NULL;
    void        *pdata  = NULL;
    int         len     = 0;

    if ((new_data == NULL) || (dataLen == NULL) || (get == NULL) || (timeout <= 0))
        return nats_setDefaultError(NATS_INVALID_ARG);

    // Initialize the output.
    *new_data   = NULL;
    *dataLen    = 0;

    if (get->done)
        return nats_setError(NATS_ILLEGAL_STATE, "%s", obsErrReadComplete);

    len = (int) get->remaining;
    if (len == 0)
    {
        get->done = true;
        return NATS_OK;
    }

    // Alloc for the remaining of bytes that need to be read.
    data = NATS_MALLOC(len + (forString ? 1 : 0));
    if (data == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    pdata = data;
    while ((s = _checkElapsed(&elapsed, start, timeout)) == NATS_OK)
    {
        bool    done    = false;
        int     cl      = 0;

        s = _readInto(&done, NULL, pdata, &cl, get, false, timeout-elapsed);
        if (s == NATS_OK)
        {
            pdata = (void*) (((char*) pdata) + cl);
            if (done)
                break;
        }
    }
    if (s == NATS_OK)
    {
        if (forString)
            ((char*) data)[len] = '\0';

        *new_data = data;
        *dataLen  = len;
    }
    else
    {
        NATS_FREE(data);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStoreGet_ReadAll(void **new_data, int *dataLen, objStoreGet *get, int64_t timeout)
{
    natsStatus s;

    s = _readAll(new_data, dataLen, get, false, timeout);
    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_getBytes(void **new_data, int *dataLen, objStore *obs, bool forString, const char *name, objStoreOptions *opts)
{
    natsStatus      s       = NATS_OK;
    objStoreGet     *get    = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;
    int64_t         timeout = 0;

    if (obs == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    s = objStore_Get(&get, obs, name, opts);
    IFOK(s, _checkElapsed(&elapsed, start, timeout));
    IFOK(s, _readAll(new_data, dataLen, get, forString, timeout-elapsed));

    objStoreGet_Destroy(get);

    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_GetString(char **new_str, objStore *obs, const char *name, objStoreOptions *opts)
{
    natsStatus  s   = NATS_OK;
    int         len = 0;

    s = _getBytes((void**) new_str, &len, obs, true, name, opts);
    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_GetBytes(void **new_data, int *dataLen, objStore *obs, const char *name, objStoreOptions *opts)
{
    natsStatus s;

    s = _getBytes(new_data, dataLen, obs, false, name, opts);
    if (s == NATS_NOT_FOUND)
        return s;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
objStore_GetFile(objStore *obs, const char *name, const char *fileName, objStoreOptions *opts)
{
    natsStatus      s       = NATS_OK;
    FILE            *f      = NULL;
    objStoreGet     *get    = NULL;
    int64_t         start   = nats_Now();
    int64_t         elapsed = 0;
    int64_t         timeout = 0;

    if ((obs == NULL) || nats_IsStringEmpty(fileName))
        return nats_setDefaultError(NATS_INVALID_ARG);

    f = fopen(fileName, "w");
    if (f == NULL)
        return nats_setError(NATS_ERR, "error opening file '%s': %d (%s)",
                                fileName, errno, strerror(errno));

    js_lock(obs->js);
    timeout = obs->js->opts.Wait;
    js_unlock(obs->js);

    s = objStore_Get(&get, obs, name, opts);
    if (s == NATS_OK)
    {
        while ((s = _checkElapsed(&elapsed, start, timeout)) == NATS_OK)
        {
            void    *data = NULL;
            int     len   = 0;
            bool    done  = false;

            s = objStoreGet_Read(&done, &data, &len, get, timeout-elapsed);
            if (s == NATS_OK)
            {
                fwrite((const void*) data, 1, (size_t) len, f);
                if (ferror(f))
                {
                    s = nats_setError(NATS_ERR, "error writing into file '%s': %d (%s)",
                                        fileName, errno, strerror(errno));
                }
            }
            // Free the data now.
            NATS_FREE(data);
            if ((s == NATS_OK) && done)
                break;
        }
    }
    // Close the file.
    if (fclose(f))
    {
        s = nats_setError(NATS_ERR, "error closing file '%s': %d (%s)",
                            fileName, errno, strerror(errno));
    }
    // Destroy the `get` object now.
    objStoreGet_Destroy(get);

    if (s != NATS_OK)
    {
        // On error, we will delete the file.
        remove(fileName);
        // On "not found" error, do not update the error stack.
        if (s == NATS_NOT_FOUND)
            return s;
    }
    return NATS_UPDATE_ERR_STACK(s);
}
