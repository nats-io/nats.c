// Copyright 2021-2022 The NATS Authors
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
#include "mem.h"
#include "util.h"
#include "js.h"

typedef enum
{
    jsStreamActionCreate = 1,
    jsStreamActionUpdate,
    jsStreamActionGet,

} jsStreamAction;

typedef struct apiPaged
{
    int64_t total;
    int64_t offset;
    int64_t limit;

} apiPaged;

static natsStatus
_marshalTimeUTC(natsBuffer *buf, bool sep, const char *fieldName, int64_t timeUTC)
{
    natsStatus  s  = NATS_OK;
    char        dbuf[36] = {'\0'};

    s = nats_EncodeTimeUTC(dbuf, sizeof(dbuf), timeUTC);
    if (s != NATS_OK)
        return nats_setError(NATS_ERR, "unable to encode data for field '%s' value %" PRId64, fieldName, timeUTC);

    if (sep)
        s = natsBuf_AppendByte(buf, ',');

    IFOK(s, natsBuf_AppendByte(buf, '"'));
    IFOK(s, natsBuf_Append(buf, fieldName, -1));
    IFOK(s, natsBuf_Append(buf, "\":\"", -1));
    IFOK(s, natsBuf_Append(buf, dbuf, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));

    return NATS_UPDATE_ERR_STACK(s);
}

//
// Stream related functions
//

static natsStatus
_checkStreamName(const char *stream)
{
    if (nats_IsStringEmpty(stream))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamNameRequired);

    if (strchr(stream, '.') != NULL)
        return nats_setError(NATS_INVALID_ARG, "%s '%s' (cannot contain '.')", jsErrInvalidStreamName, stream);

    return NATS_OK;
}

static void
_destroyPlacement(jsPlacement *placement)
{
    int i;

    if (placement == NULL)
        return;

    NATS_FREE((char*)placement->Cluster);
    for (i=0; i<placement->TagsLen; i++)
        NATS_FREE((char*) placement->Tags[i]);
    NATS_FREE((char**) placement->Tags);
    NATS_FREE(placement);
}

static void
_destroyExternalStream(jsExternalStream *external)
{
    if (external == NULL)
        return;

    NATS_FREE((char*) external->APIPrefix);
    NATS_FREE((char*) external->DeliverPrefix);
    NATS_FREE(external);
}

static void
_destroyStreamSource(jsStreamSource *source)
{
    if (source == NULL)
        return;

    NATS_FREE((char*) source->Name);
    NATS_FREE((char*) source->FilterSubject);
    _destroyExternalStream(source->External);
    NATS_FREE(source);
}

static void
_destroyRePublish(jsRePublish *rp)
{
    if (rp == NULL)
        return;

    NATS_FREE((char*) rp->Source);
    NATS_FREE((char*) rp->Destination);
    NATS_FREE(rp);
}

void js_destroyStreamConfig(jsStreamConfig *cfg)
{
    int i;

    if (cfg == NULL)
        return;

    NATS_FREE((char*) cfg->Name);
    NATS_FREE((char*) cfg->Description);
    for (i=0; i<cfg->SubjectsLen; i++)
        NATS_FREE((char*) cfg->Subjects[i]);
    NATS_FREE((char**) cfg->Subjects);
    NATS_FREE((char*) cfg->Template);
    _destroyPlacement(cfg->Placement);
    _destroyStreamSource(cfg->Mirror);
    for (i=0; i<cfg->SourcesLen; i++)
        _destroyStreamSource(cfg->Sources[i]);
    NATS_FREE(cfg->Sources);
    _destroyRePublish(cfg->RePublish);
    nats_freeMetadata(&(cfg->Metadata));
    NATS_FREE((char *)cfg->SubjectTransform.Source);
    NATS_FREE((char *)cfg->SubjectTransform.Destination);
    NATS_FREE(cfg);
}

static void
_destroyPeerInfo(jsPeerInfo *peer)
{
    if (peer == NULL)
        return;

    NATS_FREE(peer->Name);
    NATS_FREE(peer);
}

static void
_destroyClusterInfo(jsClusterInfo *cluster)
{
    int i;

    if (cluster == NULL)
        return;

    NATS_FREE(cluster->Name);
    NATS_FREE(cluster->Leader);
    for (i=0; i<cluster->ReplicasLen; i++)
        _destroyPeerInfo(cluster->Replicas[i]);
    NATS_FREE(cluster->Replicas);
    NATS_FREE(cluster);
}

static void
_destroyStreamSourceInfo(jsStreamSourceInfo *info)
{
    int i;

    if (info == NULL)
        return;

    NATS_FREE(info->Name);
    NATS_FREE((char*)info->FilterSubject);
    for (i=0; i < info->SubjectTransformsLen; i++)
    {
        NATS_FREE((char *)info->SubjectTransforms[i].Source);
        NATS_FREE((char *)info->SubjectTransforms[i].Destination);
    }
    NATS_FREE(info->SubjectTransforms);
    _destroyExternalStream(info->External);
    NATS_FREE(info);
}

static void
_destroyLostStreamData(jsLostStreamData *lost)
{
    if (lost == NULL)
        return;

    NATS_FREE(lost->Msgs);
    NATS_FREE(lost);
}

static void
_destroyStreamStateSubjects(jsStreamStateSubjects *subjects)
{
    int i;

    if (subjects == NULL)
        return;

    for (i=0; i<subjects->Count; i++)
        NATS_FREE((char*) subjects->List[i].Subject);
    NATS_FREE(subjects->List);
    NATS_FREE(subjects);
}

void
js_cleanStreamState(jsStreamState *state)
{
    if (state == NULL)
        return;

    NATS_FREE(state->Deleted);
    _destroyLostStreamData(state->Lost);
    _destroyStreamStateSubjects(state->Subjects);
}

static void
_destroyStreamAlternate(jsStreamAlternate *sa)
{
    if (sa == NULL)
        return;

    NATS_FREE((char*) sa->Name);
    NATS_FREE((char*) sa->Domain);
    NATS_FREE((char*) sa->Cluster);
    NATS_FREE(sa);
}

void
jsStreamInfo_Destroy(jsStreamInfo *si)
{
    int i;

    if (si == NULL)
        return;

    js_destroyStreamConfig(si->Config);
    _destroyClusterInfo(si->Cluster);
    js_cleanStreamState(&(si->State));
    _destroyStreamSourceInfo(si->Mirror);
    for (i=0; i<si->SourcesLen; i++)
        _destroyStreamSourceInfo(si->Sources[i]);
    NATS_FREE(si->Sources);
    for (i=0; i<si->AlternatesLen; i++)
        _destroyStreamAlternate(si->Alternates[i]);
    NATS_FREE(si->Alternates);
    NATS_FREE(si);
}

static natsStatus
_unmarshalExternalStream(nats_JSON *json, const char *fieldName, jsExternalStream **new_external)
{
    jsExternalStream        *external = NULL;
    nats_JSON               *obj      = NULL;
    natsStatus              s;

    s = nats_JSONGetObject(json, fieldName, &obj);
    if (obj == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    external = (jsExternalStream*) NATS_CALLOC(1, sizeof(jsExternalStream));
    if (external == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(obj, "api", (char**) &(external->APIPrefix));
    IFOK(s, nats_JSONGetStr(obj, "deliver", (char**) &(external->DeliverPrefix)));

    if (s == NATS_OK)
        *new_external = external;
    else
        _destroyExternalStream(external);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalExternalStream(jsExternalStream *external, const char *fieldName, natsBuffer *buf)
{
    natsStatus s = NATS_OK;

    IFOK(s, natsBuf_Append(buf, ",\"", -1));
    IFOK(s, natsBuf_Append(buf, fieldName, -1));
    IFOK(s, natsBuf_Append(buf, "\":{\"api\":\"", -1));
    IFOK(s, natsBuf_Append(buf, external->APIPrefix, -1));
    if ((s == NATS_OK) && !nats_IsStringEmpty(external->DeliverPrefix))
    {
        IFOK(s, natsBuf_Append(buf, "\",\"deliver\":\"", -1));
        IFOK(s, natsBuf_Append(buf, external->DeliverPrefix, -1));
    }
    IFOK(s, natsBuf_Append(buf, "\"}", -1));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamSource(nats_JSON *json, const char *fieldName, jsStreamSource **new_source)
{
    jsStreamSource      *source = NULL;
    nats_JSON           *obj    = NULL;
    natsStatus          s;

    if (fieldName != NULL)
    {
        s = nats_JSONGetObject(json, fieldName, &obj);
        if (obj == NULL)
            return NATS_UPDATE_ERR_STACK(s);
    }
    else
    {
        obj = json;
    }

    source = (jsStreamSource*) NATS_CALLOC(1, sizeof(jsStreamSource));
    if (source == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(obj, "name", (char**) &(source->Name));
    IFOK(s, nats_JSONGetULong(obj, "opt_start_seq", &(source->OptStartSeq)));
    IFOK(s, nats_JSONGetTime(obj, "opt_start_time", &(source->OptStartTime)));
    IFOK(s, nats_JSONGetStr(obj, "filter_subject", (char**) &(source->FilterSubject)));
    IFOK(s, _unmarshalExternalStream(obj, "external", &(source->External)));

    if (s == NATS_OK)
        *new_source = source;
    else
        _destroyStreamSource(source);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalStreamSource(jsStreamSource *source, const char *fieldName, natsBuffer *buf)
{
    natsStatus  s = NATS_OK;

    if (fieldName != NULL)
    {
        IFOK(s, natsBuf_Append(buf, ",\"", -1));
        IFOK(s, natsBuf_Append(buf, fieldName, -1));
        IFOK(s, natsBuf_Append(buf, "\":", -1));
    }
    IFOK(s, natsBuf_Append(buf, "{\"name\":\"", -1));
    IFOK(s, natsBuf_Append(buf, source->Name, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    if ((s == NATS_OK) && (source->OptStartSeq > 0))
        s = nats_marshalLong(buf, true, "opt_start_seq", source->OptStartSeq);
    if ((s == NATS_OK) && (source->OptStartTime > 0))
        IFOK(s, _marshalTimeUTC(buf, true, "opt_start_time", source->OptStartTime));
    if (source->FilterSubject != NULL)
    {
        IFOK(s, natsBuf_Append(buf, ",\"filter_subject\":\"", -1));
        IFOK(s, natsBuf_Append(buf, source->FilterSubject, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if (source->External != NULL)
        IFOK(s, _marshalExternalStream(source->External, "external", buf));

    IFOK(s, natsBuf_AppendByte(buf, '}'));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalPlacement(nats_JSON *json, const char *fieldName, jsPlacement **new_placement)
{
    jsPlacement         *placement = NULL;
    nats_JSON           *jpl       = NULL;
    natsStatus          s;

    s = nats_JSONGetObject(json, fieldName, &jpl);
    if (jpl == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    placement = (jsPlacement*) NATS_CALLOC(1, sizeof(jsPlacement));
    if (placement == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(jpl, "cluster", (char**) &(placement->Cluster));
    IFOK(s, nats_JSONGetArrayStr(jpl, "tags", (char***) &(placement->Tags), &(placement->TagsLen)));

    if (s == NATS_OK)
        *new_placement = placement;
    else
        _destroyPlacement(placement);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalPlacement(jsPlacement *placement, natsBuffer *buf)
{
    natsStatus  s;

    s = natsBuf_Append(buf, ",\"placement\":{\"cluster\":\"", -1);
    IFOK(s, natsBuf_Append(buf, placement->Cluster, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    if (placement->TagsLen > 0)
    {
        int i;

        IFOK(s, natsBuf_Append(buf, ",\"tags\":[\"", -1));
        for (i=0; (s == NATS_OK) && (i<placement->TagsLen); i++)
        {
            IFOK(s, natsBuf_Append(buf, placement->Tags[i], -1));
            IFOK(s, natsBuf_AppendByte(buf, '"'));
            if (i < placement->TagsLen-1)
                IFOK(s, natsBuf_Append(buf, ",\"", -1));
        }
        IFOK(s, natsBuf_AppendByte(buf, ']'));
    }
    IFOK(s, natsBuf_AppendByte(buf, '}'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalRetentionPolicy(nats_JSON *json, const char *fieldName, jsRetentionPolicy *policy)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, fieldName, &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsRetPolicyLimitsStr) == 0)
        *policy = js_LimitsPolicy;
    else if (strcmp(str, jsRetPolicyInterestStr) == 0)
        *policy = js_InterestPolicy;
    else if (strcmp(str, jsRetPolicyWorkQueueStr) == 0)
        *policy = js_WorkQueuePolicy;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal retention policy '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalRetentionPolicy(jsRetentionPolicy policy, natsBuffer *buf)
{
    natsStatus  s;
    const char  *rp = NULL;

    s = natsBuf_Append(buf, ",\"retention\":\"", -1);
    switch (policy)
    {
        case js_LimitsPolicy:       rp = jsRetPolicyLimitsStr;     break;
        case js_InterestPolicy:     rp = jsRetPolicyInterestStr;   break;
        case js_WorkQueuePolicy:    rp = jsRetPolicyWorkQueueStr;  break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid retention policy %d", (int) policy);
    }
    IFOK(s, natsBuf_Append(buf, rp, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalDiscardPolicy(nats_JSON *json, const char *fieldName, jsDiscardPolicy *policy)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, fieldName, &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsDiscardPolicyOldStr) == 0)
        *policy = js_DiscardOld;
    else if (strcmp(str, jsDiscardPolicyNewStr) == 0)
        *policy = js_DiscardNew;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal discard policy '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalDiscardPolicy(jsDiscardPolicy policy, natsBuffer *buf)
{
    natsStatus  s;
    const char  *dp = NULL;

    s = natsBuf_Append(buf, ",\"discard\":\"", -1);
    switch (policy)
    {
        case js_DiscardOld: dp = jsDiscardPolicyOldStr;   break;
        case js_DiscardNew: dp = jsDiscardPolicyNewStr;   break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid discard policy %d", (int) policy);
    }
    IFOK(s, natsBuf_Append(buf, dp, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStorageType(nats_JSON *json, const char *fieldName, jsStorageType *storage)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, "storage", &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsStorageTypeFileStr) == 0)
        *storage = js_FileStorage;
    else if (strcmp(str, jsStorageTypeMemStr) == 0)
        *storage = js_MemoryStorage;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal storage type '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalStorageType(jsStorageType storage, natsBuffer *buf)
{
    natsStatus  s;
    const char  *st = NULL;

    s = natsBuf_Append(buf, ",\"storage\":\"", -1);
    switch (storage)
    {
        case js_FileStorage:    st = jsStorageTypeFileStr; break;
        case js_MemoryStorage:  st = jsStorageTypeMemStr;  break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid storage type %d", (int) storage);
    }
    IFOK(s, natsBuf_Append(buf, st, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStorageCompression(nats_JSON *json, const char *fieldName, jsStorageCompression *compression)
{
    natsStatus s = NATS_OK;
    const char *str = NULL;

    s = nats_JSONGetStrPtr(json, "compression", &str);
    if (str == NULL)
            return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsStorageCompressionNoneStr) == 0)
            *compression = js_StorageCompressionNone;
    else if (strcmp(str, jsStorageCompressionS2Str) == 0)
            *compression = js_StorageCompressionS2;
    else
            s = nats_setError(NATS_ERR, "unable to unmarshal storage compression '%s'", str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalStorageCompression(jsStorageCompression compression, natsBuffer *buf)
{
    natsStatus s;
    const char *st = NULL;

    s = natsBuf_Append(buf, ",\"compression\":\"", -1);
    switch (compression)
    {
    case js_StorageCompressionNone:
            st = jsStorageCompressionNoneStr;
            break;
    case js_StorageCompressionS2:
            st = jsStorageCompressionS2Str;
            break;
    default:
            return nats_setError(NATS_INVALID_ARG, "invalid storage type %d", (int)compression);
    }
    IFOK(s, natsBuf_Append(buf, st, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalSubjectTransformConfig(nats_JSON *obj, jsSubjectTransformConfig *cfg)
{
    natsStatus s = NATS_OK;

    memset(cfg, 0, sizeof(jsSubjectTransformConfig));
    if (obj == NULL)
    {
        return NATS_OK;
    }

    IFOK(s, nats_JSONGetStr(obj, "src", (char **)&(cfg->Source)));
    IFOK(s, nats_JSONGetStr(obj, "dest", (char **)&(cfg->Destination)));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalSubjectTransformConfig(jsSubjectTransformConfig *cfg, natsBuffer *buf)
{
    natsStatus s;
    if (cfg == NULL || (nats_IsStringEmpty(cfg->Source) && nats_IsStringEmpty(cfg->Destination)))
        return NATS_OK;

    s = natsBuf_Append(buf, ",\"subject_transform\":{", -1);
    IFOK(s, natsBuf_Append(buf, "\"src\":\"", -1));
    if (cfg->Source != NULL)
        IFOK(s, natsBuf_Append(buf, cfg->Source, -1));
    IFOK(s, natsBuf_Append(buf, "\",\"dest\":\"", -1));
    if (cfg->Destination != NULL)
        IFOK(s, natsBuf_Append(buf, cfg->Destination, -1));
    IFOK(s, natsBuf_Append(buf, "\"}", -1));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalStreamConsumerLimits(jsStreamConsumerLimits *limits, natsBuffer *buf)
{
    natsStatus s;
    if (limits == NULL || (limits->InactiveThreshold == 0 && limits->MaxAckPending == 0))
        return NATS_OK;

    s = natsBuf_Append(buf, ",\"consumer_limits\":{", -1);
    IFOK(s, nats_marshalLong(buf, false, "inactive_threshold", limits->InactiveThreshold));
    IFOK(s, nats_marshalLong(buf, true, "max_ack_pending", limits->MaxAckPending));
    IFOK(s, natsBuf_AppendByte(buf, '}'));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamConsumerLimits(nats_JSON *obj, jsStreamConsumerLimits *limits)
{
    natsStatus s = NATS_OK;

    memset(limits, 0, sizeof(*limits));
    if (obj == NULL)
    {
        return NATS_OK;
    }

    IFOK(s, nats_JSONGetLong(obj, "inactive_threshold", &limits->InactiveThreshold));
    IFOK(s, nats_JSONGetInt(obj, "max_ack_pending", &limits->MaxAckPending));
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalRePublish(nats_JSON *json, const char *fieldName, jsRePublish **new_republish)
{
    jsRePublish         *rp     = NULL;
    nats_JSON           *jsm    = NULL;
    natsStatus          s;

    s = nats_JSONGetObject(json, fieldName, &jsm);
    if (jsm == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    rp = (jsRePublish*) NATS_CALLOC(1, sizeof(jsRePublish));
    if (rp == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(jsm, "src", (char**) &(rp->Source));
    IFOK(s, nats_JSONGetStr(jsm, "dest", (char**) &(rp->Destination)));
    IFOK(s, nats_JSONGetBool(jsm, "headers_only", &(rp->HeadersOnly)));

    if (s == NATS_OK)
        *new_republish = rp;
    else
        _destroyRePublish(rp);

    return NATS_UPDATE_ERR_STACK(s);

}

natsStatus
js_unmarshalStreamConfig(nats_JSON *json, const char *fieldName, jsStreamConfig **new_cfg)
{
    nats_JSON           *jcfg       = NULL;
    jsStreamConfig      *cfg        = NULL;
    nats_JSON           **sources   = NULL;
    int                 sourcesLen  = 0;
    nats_JSON           *obj        = NULL; 
    natsStatus          s;

    if (fieldName != NULL)
    {
        s = nats_JSONGetObject(json, fieldName, &jcfg);
        if (jcfg == NULL)
            return NATS_UPDATE_ERR_STACK(s);
    }
    else
    {
        jcfg = json;
    }

    cfg = (jsStreamConfig*) NATS_CALLOC(1, sizeof(jsStreamConfig));
    if (cfg == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(jcfg, "name", (char**) &(cfg->Name));
    IFOK(s, nats_JSONGetStr(jcfg, "description", (char**) &(cfg->Description)));
    IFOK(s, nats_JSONGetArrayStr(jcfg, "subjects", (char***) &(cfg->Subjects), &(cfg->SubjectsLen)));
    IFOK(s, _unmarshalRetentionPolicy(jcfg, "retention", &(cfg->Retention)));
    IFOK(s, nats_JSONGetLong(jcfg, "max_consumers", &(cfg->MaxConsumers)));
    IFOK(s, nats_JSONGetLong(jcfg, "max_msgs", &(cfg->MaxMsgs)));
    IFOK(s, nats_JSONGetLong(jcfg, "max_bytes", &(cfg->MaxBytes)));
    IFOK(s, nats_JSONGetLong(jcfg, "max_age", &(cfg->MaxAge)));
    IFOK(s, nats_JSONGetLong(jcfg, "max_msgs_per_subject", &(cfg->MaxMsgsPerSubject)));
    IFOK(s, nats_JSONGetInt32(jcfg, "max_msg_size", &(cfg->MaxMsgSize)));
    IFOK(s, _unmarshalDiscardPolicy(jcfg, "discard", &(cfg->Discard)));
    IFOK(s, _unmarshalStorageType(jcfg, "storage", &(cfg->Storage)));
    IFOK(s, nats_JSONGetLong(jcfg, "num_replicas", &(cfg->Replicas)));
    IFOK(s, nats_JSONGetBool(jcfg, "no_ack", &(cfg->NoAck)));
    IFOK(s, nats_JSONGetStr(jcfg, "template_owner", (char**) &(cfg->Template)));
    IFOK(s, nats_JSONGetLong(jcfg, "duplicate_window", &(cfg->Duplicates)));
    IFOK(s, _unmarshalPlacement(jcfg, "placement", &(cfg->Placement)));
    IFOK(s, _unmarshalStreamSource(jcfg, "mirror", &(cfg->Mirror)));
    // Get the sources and unmarshal if present
    IFOK(s, nats_JSONGetArrayObject(jcfg, "sources", &sources, &sourcesLen));
    if ((s == NATS_OK) && (sources != NULL))
    {
        int i;

        cfg->Sources = (jsStreamSource**) NATS_CALLOC(sourcesLen, sizeof(jsStreamSource*));
        if (cfg->Sources == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i=0; (s == NATS_OK) && (i<sourcesLen); i++)
        {
            s = _unmarshalStreamSource(sources[i], NULL, &(cfg->Sources[i]));
            if (s == NATS_OK)
                cfg->SourcesLen++;
        }
        // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
        NATS_FREE(sources);
    }
    IFOK(s, nats_JSONGetBool(jcfg, "sealed", &(cfg->Sealed)));
    IFOK(s, nats_JSONGetBool(jcfg, "deny_delete", &(cfg->DenyDelete)));
    IFOK(s, nats_JSONGetBool(jcfg, "deny_purge", &(cfg->DenyPurge)));
    IFOK(s, nats_JSONGetBool(jcfg, "allow_rollup_hdrs", &(cfg->AllowRollup)));
    IFOK(s, _unmarshalRePublish(jcfg, "republish", &(cfg->RePublish)));
    IFOK(s, nats_JSONGetBool(jcfg, "allow_direct", &(cfg->AllowDirect)));
    IFOK(s, nats_JSONGetBool(jcfg, "mirror_direct", &(cfg->MirrorDirect)));
    IFOK(s, nats_JSONGetBool(jcfg, "discard_new_per_subject", &(cfg->DiscardNewPerSubject)));

    IFOK(s, nats_unmarshalMetadata(jcfg, "metadata", &(cfg->Metadata)));
    IFOK(s, _unmarshalStorageCompression(jcfg, "storage", &(cfg->Compression)));
    IFOK(s, nats_JSONGetULong(jcfg, "first_seq", &(cfg->FirstSeq)));
    IFOK(s, nats_JSONGetObject(jcfg, "subject_transform", &obj));
    IFOK(s, _unmarshalSubjectTransformConfig(obj, &(cfg->SubjectTransform)));
    obj = NULL;
    IFOK(s, nats_JSONGetObject(jcfg, "consumer_limits", &obj));
    IFOK(s, _unmarshalStreamConsumerLimits(obj, &(cfg->ConsumerLimits)));

    if (s == NATS_OK)
        *new_cfg = cfg;
    else
        js_destroyStreamConfig(cfg);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_marshalStreamConfig(natsBuffer **new_buf, jsStreamConfig *cfg)
{
    natsBuffer *buf = NULL;
    natsStatus s;

    s = natsBuf_Create(&buf, 256);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = natsBuf_Append(buf, "{\"name\":\"", -1);
    IFOK(s, natsBuf_Append(buf, cfg->Name, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));
    if ((s == NATS_OK) && !nats_IsStringEmpty(cfg->Description))
    {
        IFOK(s, natsBuf_Append(buf, ",\"description\":\"", -1));
        IFOK(s, natsBuf_Append(buf, cfg->Description, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (cfg->SubjectsLen > 0))
    {
        int i;

        IFOK(s, natsBuf_Append(buf, ",\"subjects\":[\"", -1));
        for (i=0; (s == NATS_OK) && (i<cfg->SubjectsLen); i++)
        {
            IFOK(s, natsBuf_Append(buf, cfg->Subjects[i], -1));
            IFOK(s, natsBuf_AppendByte(buf, '"'));
            if (i < cfg->SubjectsLen - 1)
                IFOK(s, natsBuf_Append(buf, ",\"", -1));
        }
        IFOK(s, natsBuf_AppendByte(buf, ']'));
    }
    IFOK(s, _marshalRetentionPolicy(cfg->Retention, buf));

    IFOK(s, nats_marshalLong(buf, true, "max_consumers", cfg->MaxConsumers));
    IFOK(s, nats_marshalLong(buf, true, "max_msgs", cfg->MaxMsgs));
    IFOK(s, nats_marshalLong(buf, true, "max_bytes", cfg->MaxBytes));
    IFOK(s, nats_marshalLong(buf, true, "max_age", cfg->MaxAge));
    IFOK(s, nats_marshalLong(buf, true, "max_msg_size", (int64_t) cfg->MaxMsgSize));
    IFOK(s, nats_marshalLong(buf, true, "max_msgs_per_subject", cfg->MaxMsgsPerSubject));

    IFOK(s, _marshalDiscardPolicy(cfg->Discard, buf));

    IFOK(s, _marshalStorageType(cfg->Storage, buf));

    IFOK(s, nats_marshalLong(buf, true, "num_replicas", cfg->Replicas));

    if ((s == NATS_OK) && cfg->NoAck)
        IFOK(s, natsBuf_Append(buf, ",\"no_ack\":true", -1));

    if ((s == NATS_OK) && (cfg->Template != NULL))
    {
        IFOK(s, natsBuf_Append(buf, ",\"template_owner\":\"", -1));
        IFOK(s, natsBuf_Append(buf, cfg->Template, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }

    if ((s == NATS_OK) && (cfg->Duplicates != 0))
        IFOK(s, nats_marshalLong(buf, true, "duplicate_window", cfg->Duplicates));

    if ((s == NATS_OK) && (cfg->Placement != NULL))
        IFOK(s, _marshalPlacement(cfg->Placement, buf));

    if ((s == NATS_OK) && (cfg->Mirror != NULL))
        IFOK(s, _marshalStreamSource(cfg->Mirror, "mirror", buf));

    if ((s == NATS_OK) && (cfg->SourcesLen > 0))
    {
        int i;

        IFOK(s, natsBuf_Append(buf, ",\"sources\":[", -1));
        for (i=0; (s == NATS_OK) && (i < cfg->SourcesLen); i++)
        {
            IFOK(s, _marshalStreamSource(cfg->Sources[i], NULL, buf));
            if ((s == NATS_OK) && (i < cfg->SourcesLen-1))
                IFOK(s, natsBuf_AppendByte(buf, ','));
        }
        IFOK(s, natsBuf_AppendByte(buf, ']'));
    }

    if ((s == NATS_OK) && cfg->Sealed)
        IFOK(s, natsBuf_Append(buf, ",\"sealed\":true", -1));
    if ((s == NATS_OK) && cfg->DenyDelete)
        IFOK(s, natsBuf_Append(buf, ",\"deny_delete\":true", -1));
    if ((s == NATS_OK) && cfg->DenyPurge)
        IFOK(s, natsBuf_Append(buf, ",\"deny_purge\":true", -1));
    if ((s == NATS_OK) && cfg->AllowRollup)
        IFOK(s, natsBuf_Append(buf, ",\"allow_rollup_hdrs\":true", -1));
    if ((s == NATS_OK) && (cfg->RePublish != NULL) && !nats_IsStringEmpty(cfg->RePublish->Destination))
    {
        // "dest" is not omitempty, in that the field will always be present.
        IFOK(s, natsBuf_Append(buf, ",\"republish\":{\"dest\":\"", -1));
        IFOK(s, natsBuf_Append(buf, cfg->RePublish->Destination, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
        // Now the source...
        if (!nats_IsStringEmpty(cfg->RePublish->Source))
        {
            IFOK(s, natsBuf_Append(buf, ",\"src\":\"", -1))
            IFOK(s, natsBuf_Append(buf, cfg->RePublish->Source, -1));
            IFOK(s, natsBuf_AppendByte(buf, '"'));
        }
        if (cfg->RePublish->HeadersOnly)
            IFOK(s, natsBuf_Append(buf, ",\"headers_only\":true", -1));
        IFOK(s, natsBuf_AppendByte(buf, '}'));
    }
    if ((s == NATS_OK) && cfg->AllowDirect)
        IFOK(s, natsBuf_Append(buf, ",\"allow_direct\":true", -1));
    if ((s == NATS_OK) && cfg->MirrorDirect)
        IFOK(s, natsBuf_Append(buf, ",\"mirror_direct\":true", -1));
    if ((s == NATS_OK) && cfg->DiscardNewPerSubject)
        IFOK(s, natsBuf_Append(buf, ",\"discard_new_per_subject\":true", -1));

    IFOK(s, nats_marshalMetadata(buf, true, "metadata", cfg->Metadata));
    IFOK(s, _marshalStorageCompression(cfg->Compression, buf));
    IFOK(s, nats_marshalULong(buf, true, "first_seq", cfg->FirstSeq));
    IFOK(s, _marshalSubjectTransformConfig(&cfg->SubjectTransform, buf));
    IFOK(s, _marshalStreamConsumerLimits(&cfg->ConsumerLimits, buf));

    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
        *new_buf = buf;
    else
        natsBuf_Destroy(buf);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalLostStreamData(nats_JSON *pjson, const char *fieldName, jsLostStreamData **new_lost)
{
    natsStatus              s       = NATS_OK;
    jsLostStreamData        *lost   = NULL;
    nats_JSON               *json   = NULL;

    s = nats_JSONGetObject(pjson, fieldName, &json);
    if (json == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    lost = (jsLostStreamData*) NATS_CALLOC(1, sizeof(jsLostStreamData));
    if (lost == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    IFOK(s, nats_JSONGetArrayULong(json, "msgs", &(lost->Msgs), &(lost->MsgsLen)));
    IFOK(s, nats_JSONGetULong(json, "bytes", &(lost->Bytes)));

    if (s == NATS_OK)
        *new_lost = lost;
    else
        _destroyLostStreamData(lost);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_fillSubjectsList(void *userInfo, const char *subject, nats_JSONField *f)
{
    jsStreamStateSubjects   *subjs  = (jsStreamStateSubjects*) userInfo;
    natsStatus              s       = NATS_OK;
    int                     i       = subjs->Count;

    DUP_STRING(s, subjs->List[i].Subject, subject);
    if (s == NATS_OK)
    {
        subjs->List[i].Msgs = f->value.vuint;
        subjs->Count = i+1;
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamStateSubjects(nats_JSON *pjson, const char *fieldName, jsStreamStateSubjects **subjects)
{
    nats_JSON               *json = NULL;
    natsStatus              s     = NATS_OK;
    jsStreamStateSubjects   *subjs= NULL;
    int                     n;

    s = nats_JSONGetObject(pjson, fieldName, &json);
    if (json == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if ((n = natsStrHash_Count(json->fields)) > 0)
    {
        subjs = NATS_CALLOC(1, sizeof(jsStreamStateSubjects));
        if (subjs == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (s == NATS_OK)
        {
            subjs->List = NATS_CALLOC(n, sizeof(jsStreamStateSubject));
            if (subjs->List == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        IFOK(s, nats_JSONRange(json, TYPE_NUM, TYPE_UINT, _fillSubjectsList, (void*) subjs));

        if (s == NATS_OK)
            *subjects = subjs;
        else
            _destroyStreamStateSubjects(subjs);
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_unmarshalStreamState(nats_JSON *pjson, const char *fieldName, jsStreamState *state)
{
    nats_JSON   *json = NULL;
    natsStatus  s;

    s = nats_JSONGetObject(pjson, fieldName, &json);
    if (json == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    s = nats_JSONGetULong(json, "messages", &(state->Msgs));
    IFOK(s, nats_JSONGetULong(json, "bytes", &(state->Bytes)));
    IFOK(s, nats_JSONGetULong(json, "first_seq", &(state->FirstSeq)));
    IFOK(s, nats_JSONGetTime(json, "first_ts", &(state->FirstTime)));
    IFOK(s, nats_JSONGetULong(json, "last_seq", &(state->LastSeq)));
    IFOK(s, nats_JSONGetTime(json, "last_ts", &(state->LastTime)));
    IFOK(s, nats_JSONGetULong(json, "num_deleted", &(state->NumDeleted)));
    IFOK(s, nats_JSONGetArrayULong(json, "deleted", &(state->Deleted), &(state->DeletedLen)));
    IFOK(s, _unmarshalLostStreamData(json, "lost", &(state->Lost)));
    IFOK(s, nats_JSONGetLong(json, "consumer_count", &(state->Consumers)));
    IFOK(s, nats_JSONGetLong(json, "num_subjects", &(state->NumSubjects)));
    IFOK(s, _unmarshalStreamStateSubjects(json, "subjects", &(state->Subjects)));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalPeerInfo(nats_JSON *json, jsPeerInfo **new_pi)
{
    jsPeerInfo      *pi = NULL;
    natsStatus      s;

    pi = (jsPeerInfo*) NATS_CALLOC(1, sizeof(jsPeerInfo));
    if (pi == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(json, "name", &(pi->Name));
    IFOK(s, nats_JSONGetBool(json, "current", &(pi->Current)));
    IFOK(s, nats_JSONGetBool(json, "offline", &(pi->Offline)));
    IFOK(s, nats_JSONGetLong(json, "active", &(pi->Active)));
    IFOK(s, nats_JSONGetULong(json, "lag", &(pi->Lag)));

    if (s == NATS_OK)
        *new_pi = pi;
    else
        _destroyPeerInfo(pi);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalClusterInfo(nats_JSON *pjson, const char *fieldName, jsClusterInfo **new_ci)
{
    jsClusterInfo       *ci         = NULL;
    nats_JSON           *json       = NULL;
    nats_JSON           **replicas  = NULL;
    int                 replicasLen = 0;
    natsStatus          s;

    s = nats_JSONGetObject(pjson, fieldName, &json);
    if (json == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    ci = (jsClusterInfo*) NATS_CALLOC(1, sizeof(jsClusterInfo));
    if (ci == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(json, "name", &(ci->Name));
    IFOK(s, nats_JSONGetStr(json, "leader", &(ci->Leader)));
    IFOK(s, nats_JSONGetArrayObject(json, "replicas", &replicas, &replicasLen));
    if ((s == NATS_OK) && (replicas != NULL))
    {
        int i;

        ci->Replicas = (jsPeerInfo**) NATS_CALLOC(replicasLen, sizeof(jsPeerInfo*));
        if (ci->Replicas == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i=0; (s == NATS_OK) && (i<replicasLen); i++)
        {
            s = _unmarshalPeerInfo(replicas[i], &(ci->Replicas[i]));
            if (s == NATS_OK)
                ci->ReplicasLen++;
        }
        // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
        NATS_FREE(replicas);
    }
    if (s == NATS_OK)
        *new_ci = ci;
    else
        _destroyClusterInfo(ci);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamSourceInfo(nats_JSON *pjson, const char *fieldName, jsStreamSourceInfo **new_src)
{
    nats_JSON               *json = NULL;
    jsStreamSourceInfo      *ssi  = NULL;
    natsStatus              s;
    nats_JSON               **subjectTransforms = NULL;
    int                     subjectTransformsLen = 0;

    if (fieldName != NULL)
    {
        s = nats_JSONGetObject(pjson, fieldName, &json);
        if (json == NULL)
            return NATS_UPDATE_ERR_STACK(s);
    }
    else
    {
        json = pjson;
    }

    ssi = (jsStreamSourceInfo*) NATS_CALLOC(1, sizeof(jsStreamSourceInfo));
    if (ssi == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(json, "name", &(ssi->Name));
    IFOK(s, _unmarshalExternalStream(json, "external", &(ssi->External)));
    IFOK(s, nats_JSONGetULong(json, "lag", &(ssi->Lag)));
    IFOK(s, nats_JSONGetLong(json, "active", &(ssi->Active)));
    IFOK(s, nats_JSONGetStr(json, "filter_subject", (char **)&(ssi->FilterSubject)));

    // Get the sources and unmarshal if present
    IFOK(s, nats_JSONGetArrayObject(json, "subject_transforms", &subjectTransforms, &subjectTransformsLen));
    if ((s == NATS_OK) && (subjectTransforms != NULL))
    {
        int i;

        ssi->SubjectTransforms = (jsSubjectTransformConfig *)NATS_CALLOC(subjectTransformsLen, sizeof(jsSubjectTransformConfig));
        if (ssi->SubjectTransforms == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i = 0; (s == NATS_OK) && (i < subjectTransformsLen); i++)
        {
            s = _unmarshalSubjectTransformConfig(subjectTransforms[i],  &(ssi->SubjectTransforms[i]));
            if (s == NATS_OK)
                ssi->SubjectTransformsLen++;
        }
        // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
        NATS_FREE(subjectTransforms);
    }

    if (s == NATS_OK)
        *new_src = ssi;
    else
        _destroyStreamSourceInfo(ssi);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamAlternate(nats_JSON *json, jsStreamAlternate **new_alt)
{
    jsStreamAlternate   *sa         = NULL;
    natsStatus          s           = NATS_OK;

    sa = (jsStreamAlternate*) NATS_CALLOC(1, sizeof(jsStreamAlternate));
    if (sa == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(json, "name", (char**) &(sa->Name));
    IFOK(s, nats_JSONGetStr(json, "domain", (char**) &(sa->Domain)));
    IFOK(s, nats_JSONGetStr(json, "cluster", (char**) &(sa->Cluster)));

    if (s == NATS_OK)
        *new_alt = sa;
    else
        _destroyStreamAlternate(sa);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamInfoPaged(nats_JSON *json, jsStreamInfo **new_si, apiPaged *page)
{
    jsStreamInfo        *si         = NULL;
    nats_JSON           **sources   = NULL;
    int                 sourcesLen  = 0;
    nats_JSON           **alts      = NULL;
    int                 altsLen     = 0;
    natsStatus          s;

    si = (jsStreamInfo*) NATS_CALLOC(1, sizeof(jsStreamInfo));
    if (si == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // Get the config object
    s = js_unmarshalStreamConfig(json, "config", &(si->Config));
    IFOK(s, nats_JSONGetTime(json, "created", &(si->Created)));
    IFOK(s, js_unmarshalStreamState(json, "state", &(si->State)));
    IFOK(s, _unmarshalClusterInfo(json, "cluster", &(si->Cluster)));
    IFOK(s, _unmarshalStreamSourceInfo(json, "mirror", &(si->Mirror)));
    IFOK(s, nats_JSONGetArrayObject(json, "sources", &sources, &sourcesLen));
    if ((s == NATS_OK) && (sources != NULL))
    {
        int i;

        si->Sources = (jsStreamSourceInfo**) NATS_CALLOC(sourcesLen, sizeof(jsStreamSourceInfo*));
        if (si->Sources == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i=0; (s == NATS_OK) && (i<sourcesLen); i++)
        {
            s = _unmarshalStreamSourceInfo(sources[i], NULL, &(si->Sources[i]));
            if (s == NATS_OK)
                si->SourcesLen++;
        }
        // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
        NATS_FREE(sources);
    }
    IFOK(s, nats_JSONGetArrayObject(json, "alternates", &alts, &altsLen));
    if ((s == NATS_OK) && (alts != NULL))
    {
        int i;

        si->Alternates = (jsStreamAlternate**) NATS_CALLOC(altsLen, sizeof(jsStreamAlternate*));
        if (si->Alternates == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i=0; (s == NATS_OK) && (i<altsLen); i++)
        {
            s = _unmarshalStreamAlternate(alts[i], &(si->Alternates[i]));
            if (s == NATS_OK)
                si->AlternatesLen++;
        }
        // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
        NATS_FREE(alts);
    }
    if ((s == NATS_OK) && (page != NULL))
    {
        IFOK(s, nats_JSONGetLong(json, "total", &page->total));
        IFOK(s, nats_JSONGetLong(json, "offset", &page->offset));
        IFOK(s, nats_JSONGetLong(json, "limit", &page->limit));
    }

    if (s == NATS_OK)
        *new_si = si;
    else
        jsStreamInfo_Destroy(si);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_unmarshalStreamInfo(nats_JSON *json, jsStreamInfo **new_si)
{
    natsStatus s = _unmarshalStreamInfoPaged(json, new_si, NULL);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamCreateResp(jsStreamInfo **new_si, apiPaged *page, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // If the error code is JSStreamNotFoundErr then pick NATS_NOT_FOUND.
        if (ar.Error.ErrCode == JSStreamNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else if (new_si != NULL)
    {
        // At this point we need to unmarshal the stream info itself.
        s = _unmarshalStreamInfoPaged(json, new_si, page);
    }

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsStreamConfig_Init(jsStreamConfig *cfg)
{
    if (cfg == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(cfg, 0, sizeof(jsStreamConfig));
    cfg->Retention      = js_LimitsPolicy;
    cfg->MaxConsumers   = -1;
    cfg->MaxMsgs        = -1;
    cfg->MaxBytes       = -1;
    cfg->MaxMsgSize     = -1;
    cfg->Storage        = js_FileStorage;
    cfg->Discard        = js_DiscardOld;
    cfg->Replicas       = 1;
    cfg->Compression    = js_StorageCompressionNone;
    return NATS_OK;
}

static void
_restoreMirrorAndSourcesExternal(jsStreamConfig *cfg)
{
    int i;

    // We are guaranteed that if a source's Domain is set, there was originally
    // no External value. So free any External value and reset to NULL to
    // restore the original setting.
    if ((cfg->Mirror != NULL) && !nats_IsStringEmpty(cfg->Mirror->Domain))
    {
        _destroyExternalStream(cfg->Mirror->External);
        cfg->Mirror->External = NULL;
    }
    for (i=0; i<cfg->SourcesLen; i++)
    {
        jsStreamSource *src = cfg->Sources[i];
        if ((src != NULL) && !nats_IsStringEmpty(src->Domain))
        {
            _destroyExternalStream(src->External);
            src->External = NULL;
        }
    }
}

static natsStatus
_convertDomain(jsStreamSource *src)
{
    jsExternalStream *e = NULL;

    e = (jsExternalStream*) NATS_CALLOC(1, sizeof(jsExternalStream));
    if (e == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (nats_asprintf((char**) &(e->APIPrefix), jsExtDomainT, src->Domain) < 0)
    {
        NATS_FREE(e);
        return nats_setDefaultError(NATS_NO_MEMORY);
    }
    src->External = e;
    return NATS_OK;
}

static natsStatus
_convertMirrorAndSourcesDomain(bool *converted, jsStreamConfig *cfg)
{
    natsStatus  s   = NATS_OK;
    bool        cm  = false;
    bool        cs  = false;
    int         i;

    *converted = false;

    if ((cfg->Mirror != NULL) && !nats_IsStringEmpty(cfg->Mirror->Domain))
    {
        if (cfg->Mirror->External != NULL)
            return nats_setError(NATS_INVALID_ARG, "%s", "mirror's domain and external are both set");
        cm = true;
    }
    for (i=0; i<cfg->SourcesLen; i++)
    {
        jsStreamSource *src = cfg->Sources[i];
        if ((src != NULL) && !nats_IsStringEmpty(src->Domain))
        {
            if (src->External != NULL)
                return nats_setError(NATS_INVALID_ARG, "%s", "source's domain and external are both set");
            cs = true;
        }
    }
    if (!cm && !cs)
        return NATS_OK;

    if (cm)
        s = _convertDomain(cfg->Mirror);
    if ((s == NATS_OK) && cs)
    {
        for (i=0; (s == NATS_OK) && (i<cfg->SourcesLen); i++)
        {
            jsStreamSource *src = cfg->Sources[i];
            if ((src != NULL) && !nats_IsStringEmpty(src->Domain))
                s = _convertDomain(src);
        }
    }
    if (s == NATS_OK)
        *converted = true;
    else
        _restoreMirrorAndSourcesExternal(cfg);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_addOrUpdate(jsStreamInfo **new_si, jsStreamAction action, jsCtx *js, jsStreamConfig *cfg, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    const char          *apiT   = NULL;
    bool                freePfx = false;
    bool                msc     = false;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (cfg == NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamConfigRequired);

    s = _checkStreamName(cfg->Name);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    switch (action)
    {
        case jsStreamActionCreate: apiT = jsApiStreamCreateT; break;
        case jsStreamActionUpdate: apiT = jsApiStreamUpdateT; break;
        default: abort();
    }
    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, apiT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, cfg->Name) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    if ((s == NATS_OK) && (action == jsStreamActionCreate))
        s = _convertMirrorAndSourcesDomain(&msc, cfg);

    // Marshal the stream create/update request
    IFOK(s, js_marshalStreamConfig(&buf, cfg));

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), o.Wait));

    // If we got a response, check for error or return the stream info result.
    IFOK(s, _unmarshalStreamCreateResp(new_si, NULL, resp, errCode));

    // If mirror and/or sources were converted for the domain, then we need
    // to restore the original values (which will free the memory that was
    // allocated for the conversion).
    if (msc)
        _restoreMirrorAndSourcesExternal(cfg);

    // Make sure the 2.10 config fields actually worked, in case the server is
    // older.
    if ((s == NATS_OK) && (new_si != NULL) && (*new_si != NULL)
        && (cfg->Compression != (*new_si)->Config->Compression)
        && (cfg->FirstSeq != (*new_si)->Config->FirstSeq)
        && (cfg->Metadata.Count != (*new_si)->Config->Metadata.Count)
        && nats_StringEquals(cfg->SubjectTransform.Source, (*new_si)->Config->SubjectTransform.Source)
        && nats_StringEquals(cfg->SubjectTransform.Destination, (*new_si)->Config->SubjectTransform.Destination)
        && (cfg->ConsumerLimits.InactiveThreshold != (*new_si)->Config->ConsumerLimits.InactiveThreshold)
        && (cfg->ConsumerLimits.MaxAckPending != (*new_si)->Config->ConsumerLimits.MaxAckPending)
        )
    {
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamConfigRequired);
    }

    natsBuf_Destroy(buf);
    natsMsg_Destroy(resp);
    NATS_FREE(subj);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_AddStream(jsStreamInfo **new_si, jsCtx *js, jsStreamConfig *cfg, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s = _addOrUpdate(new_si, jsStreamActionCreate, js, cfg, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_UpdateStream(jsStreamInfo **new_si, jsCtx *js, jsStreamConfig *cfg, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s = _addOrUpdate(new_si, jsStreamActionUpdate, js, cfg, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalStreamInfoReq(natsBuffer **new_buf, struct jsOptionsStreamInfo *o, int64_t offset)
{
    natsBuffer  *buf = *new_buf;
    natsStatus  s    = NATS_OK;

    if (!o->DeletedDetails && nats_IsStringEmpty(o->SubjectsFilter))
        return NATS_OK;

    if (buf == NULL)
        s = natsBuf_Create(&buf, 30);

    IFOK(s, natsBuf_AppendByte(buf, '{'));
    if ((s == NATS_OK) && o->DeletedDetails)
        s = natsBuf_Append(buf, "\"deleted_details\":true", -1);
    if ((s == NATS_OK) && !nats_IsStringEmpty(o->SubjectsFilter))
    {
        if (o->DeletedDetails)
            s = natsBuf_AppendByte(buf, ',');
        IFOK(s, natsBuf_Append(buf, "\"subjects_filter\":\"", -1));
        IFOK(s, natsBuf_Append(buf, o->SubjectsFilter, -1));
        IFOK(s, natsBuf_AppendByte(buf, '\"'));
        if ((s == NATS_OK) && (offset > 0))
            IFOK(s, nats_marshalLong(buf, true, "offset", offset));
    }
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (*new_buf == NULL)
        *new_buf = buf;

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_GetStreamInfo(jsStreamInfo **new_si, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    char                *req    = NULL;
    int                 reqLen  = 0;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    jsOptions           o;
    // For subjects pagination
    int64_t                 offset  = 0;
    bool                    doPage  = false;
    bool                    done    = false;
    jsStreamInfo            *si     = NULL;
    jsStreamStateSubjects   *subjects = NULL;
    apiPaged                page;

    if (errCode != NULL)
        *errCode = 0;

    if ((js == NULL) || (new_si == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiStreamInfoT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }

    if (!nats_IsStringEmpty(o.Stream.Info.SubjectsFilter))
    {
        doPage = true;
        memset(&page, 0, sizeof(apiPaged));
    }

    do
    {
        // This will return a buffer if the request was marshal'ed
        // (due to presence of options)
        IFOK(s, _marshalStreamInfoReq(&buf, &(o.Stream.Info), offset));
        if ((s == NATS_OK) && (buf != NULL) && (natsBuf_Len(buf) > 0))
        {
            req = natsBuf_Data(buf);
            reqLen = natsBuf_Len(buf);
        }

        // Send the request
        IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, req, reqLen, o.Wait));

        // If we got a response, check for error or return the stream info result.
        IFOK(s, _unmarshalStreamCreateResp(&si, &page, resp, errCode));

        // If there was paging, we need to collect subjects until we get them all.
        if ((s == NATS_OK) && doPage)
        {
            if (si->State.Subjects != NULL)
            {
                int sc = si->State.Subjects->Count;
                offset += (int64_t) sc;
                if (subjects == NULL)
                    subjects = si->State.Subjects;
                else
                {
                    jsStreamStateSubject    *list   = subjects->List;
                    int                     prev    = subjects->Count;

                    list = (jsStreamStateSubject*) NATS_REALLOC(list, (prev + sc) * sizeof(jsStreamStateSubject));
                    if (list == NULL)
                        s = nats_setDefaultError(NATS_NO_MEMORY);
                    else
                    {
                        int i;
                        for (i=0; i<sc; i++)
                        {
                            list[prev+i].Subject = si->State.Subjects->List[i].Subject;
                            list[prev+i].Msgs = si->State.Subjects->List[i].Msgs;
                        }
                        NATS_FREE(si->State.Subjects->List);
                        NATS_FREE(si->State.Subjects);
                        subjects->List = list;
                        subjects->Count += sc;
                    }
                }
                if (s == NATS_OK)
                    si->State.Subjects = NULL;
            }
            done = offset >= page.total;
            if (!done)
            {
                jsStreamInfo_Destroy(si);
                si = NULL;
                // Reset the request buffer, we may be able to reuse.
                natsBuf_Reset(buf);
            }
        }
        natsMsg_Destroy(resp);
        resp = NULL;
    }
    while ((s == NATS_OK) && doPage && !done);

    natsBuf_Destroy(buf);
    NATS_FREE(subj);

    if (s == NATS_OK)
    {
        if (doPage && (subjects != NULL))
            si->State.Subjects = subjects;

        *new_si = si;
    }
    else
    {
        if (subjects != NULL)
            _destroyStreamStateSubjects(subjects);

        if (s == NATS_NOT_FOUND)
        {
            nats_clearLastError();
            return s;
        }
    }
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalSuccessResp(bool *success, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    *success = false;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // For stream or consumer not found, return NATS_NOT_FOUND instead of NATS_ERR.
        if ((ar.Error.ErrCode == JSStreamNotFoundErr)
            || (ar.Error.ErrCode == JSConsumerNotFoundErr))
        {
            s = NATS_NOT_FOUND;
        }
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
    {
        s = nats_JSONGetBool(json, "success", success);
    }

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalPurgeRequest(natsBuffer **new_buf, struct jsOptionsStreamPurge *opts)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    bool                comma   = false;

    if (nats_IsStringEmpty(opts->Subject) && (opts->Sequence == 0) && (opts->Keep == 0))
        return NATS_OK;

    if ((opts->Sequence > 0) && (opts->Keep > 0))
        return nats_setError(NATS_INVALID_ARG,
                             "Sequence (%" PRIu64 ") and Keep (%" PRIu64 " are mutually exclusive",
                             opts->Sequence, opts->Keep);

    s = natsBuf_Create(&buf, 128);
    IFOK(s, natsBuf_AppendByte(buf, '{'));
    if ((s == NATS_OK) && !nats_IsStringEmpty(opts->Subject))
    {
        s = natsBuf_Append(buf, "\"filter\":\"", -1);
        IFOK(s, natsBuf_Append(buf, opts->Subject, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
        comma = true;
    }
    if ((s == NATS_OK) && (opts->Sequence > 0))
        s = nats_marshalULong(buf, comma, "seq", opts->Sequence);

    if ((s == NATS_OK) && (opts->Keep > 0))
        s = nats_marshalULong(buf, comma, "keep", opts->Keep);

    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
        *new_buf = buf;
    else
        natsBuf_Destroy(buf);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_purgeOrDelete(bool purge, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    const char          *apiT   = (purge ? jsApiStreamPurgeT : jsApiStreamDeleteT);
    bool                freePfx = false;
    natsBuffer          *buf    = NULL;
    const void          *data   = NULL;
    int                 dlen    = 0;
    bool                success = false;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, apiT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    if ((s == NATS_OK) && purge)
    {
        s = _marshalPurgeRequest(&buf, &(o.Stream.Purge));
        if ((s == NATS_OK) && (buf != NULL))
        {
            data = (const void*) natsBuf_Data(buf);
            dlen = natsBuf_Len(buf);
        }
    }

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, js->nc, subj, data, dlen, o.Wait));

    IFOK(s, _unmarshalSuccessResp(&success, resp, errCode));
    if ((s == NATS_OK) && !success)
        s = nats_setError(NATS_ERR, "failed to %s stream '%s'", (purge ? "purge" : "delete"), stream);

    natsBuf_Destroy(buf);
    natsMsg_Destroy(resp);
    NATS_FREE(subj);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_PurgeStream(jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s = _purgeOrDelete(true, js, stream, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_DeleteStream(jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s = _purgeOrDelete(false, js, stream, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_decodeBytesLen(nats_JSON *json, const char *field, const char **str, int *strLen, int *decodedLen)
{
    natsStatus  s    = NATS_OK;

    s = nats_JSONGetStrPtr(json, field, str);
    if ((s == NATS_OK) && (*str != NULL))
        s = nats_Base64_DecodeLen(*str, strLen, decodedLen);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStoredMsg(nats_JSON *json, natsMsg **new_msg)
{
    natsStatus  s;
    natsMsg     *msg    = NULL;
    const char  *subject= NULL;
    const char  *hdrs   = NULL;
    const char  *data   = NULL;
    int         hdrsl   = 0;
    int         dhdrsl  = 0;
    int         datal   = 0;
    int         ddatal  = 0;

    s = nats_JSONGetStrPtr(json, "subject", &subject);
    IFOK(s, _decodeBytesLen(json, "hdrs", &hdrs, &hdrsl, &dhdrsl));
    IFOK(s, _decodeBytesLen(json, "data", &data, &datal, &ddatal));
    if ((s == NATS_OK) && (subject != NULL))
    {
        s = natsMsg_create(&msg, subject, (int) strlen(subject),
                           NULL, 0, NULL, dhdrsl+ddatal, dhdrsl);
        if (s == NATS_OK)
        {
            if ((hdrs != NULL) && (dhdrsl > 0))
                nats_Base64_DecodeInPlace(hdrs, hdrsl, (unsigned char*) msg->hdr);
            if ((data != NULL) && (ddatal > 0))
                nats_Base64_DecodeInPlace(data, datal, (unsigned char*) msg->data);
        }
        IFOK(s, nats_JSONGetULong(json, "seq", &(msg->seq)));
        IFOK(s, nats_JSONGetTime(json, "time", &(msg->time)));
    }
    if (s == NATS_OK)
        *new_msg = msg;

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalGetMsgResp(natsMsg **msg, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // If the error code is JSNoMessageFoundErr then pick NATS_NOT_FOUND.
        if (ar.Error.ErrCode == JSNoMessageFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
    {
        nats_JSON *mjson = NULL;

        s = nats_JSONGetObject(json, "message", &mjson);
        if ((s == NATS_OK) && (mjson == NULL))
            s = nats_setError(NATS_NOT_FOUND, "%s", "message content not found");
        else
            s = _unmarshalStoredMsg(mjson, msg);
    }

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_getMsg(natsMsg **msg, jsCtx *js, const char *stream, uint64_t seq, const char *subject, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s = NATS_OK;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    jsOptions           o;
    char                buffer[64];
    natsBuffer          buf;

    if ((msg == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (nats_IsStringEmpty(stream))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamNameRequired);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiMsgGetT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_InitWithBackend(&buf, buffer, 0, sizeof(buffer)));
    IFOK(s, natsBuf_AppendByte(&buf, '{'));
    if ((s == NATS_OK) && (seq > 0))
    {
       s = nats_marshalULong(&buf, false, "seq", seq);
    }
    else
    {
        IFOK(s, natsBuf_Append(&buf, "\"last_by_subj\":\"", -1));
        IFOK(s, natsBuf_Append(&buf, subject, -1));
        IFOK(s, natsBuf_AppendByte(&buf, '"'));
    }
    IFOK(s, natsBuf_AppendByte(&buf, '}'));

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, js->nc, subj, natsBuf_Data(&buf), natsBuf_Len(&buf), o.Wait));
    // Unmarshal response
    IFOK(s, _unmarshalGetMsgResp(msg, resp, errCode));

    natsBuf_Cleanup(&buf);
    natsMsg_Destroy(resp);
    NATS_FREE(subj);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_GetMsg(natsMsg **msg, jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    if (seq < 1)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _getMsg(msg, js, stream, seq, NULL, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_GetLastMsg(natsMsg **msg, jsCtx *js, const char *stream, const char *subject, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if (errCode != NULL)
        *errCode = 0;

    if (nats_IsStringEmpty(subject))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _getMsg(msg, js, stream, 0, subject, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsDirectGetMsgOptions_Init(jsDirectGetMsgOptions *opts)
{
    if (opts == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(opts, 0, sizeof(jsDirectGetMsgOptions));
    return NATS_OK;
}

natsStatus
js_directGetMsgToJSMsg(const char *stream, natsMsg *msg)
{
    natsStatus          s;
    const char          *val = NULL;
    int64_t             seq  = 0;
    int64_t             tm   = 0;

    if ((msg->hdrLen == 0) && (msg->headers == NULL))
        return nats_setError(NATS_ERR, "%s", "direct get message response should have headers");

    // If the server returns an error (not found/bad request), we would receive
    // an empty body message with the Status header. Check for that.
    if ((natsMsg_GetDataLength(msg) == 0)
        && (natsMsgHeader_Get(msg, STATUS_HDR, &val) == NATS_OK))
    {
        if (strcmp(val, NOT_FOUND_STATUS) == 0)
            return nats_setDefaultError(NATS_NOT_FOUND);
        else
        {
            natsMsgHeader_Get(msg, DESCRIPTION_HDR, &val);
            return nats_setError(NATS_ERR, "error getting message: %s", val);
        }
    }

    s = natsMsgHeader_Get(msg, JSStream, &val);
    if ((s != NATS_OK) || nats_IsStringEmpty(val))
        return nats_setError(NATS_ERR, "missing stream name '%s'", val);

    val = NULL;
    s = natsMsgHeader_Get(msg, JSSequence, &val);
    if ((s != NATS_OK) || ((seq = nats_ParseInt64(val, (int) strlen(val))) == -1))
        return nats_setError(NATS_ERR, "missing or invalid sequence '%s'", val);

    val = NULL;
    s = natsMsgHeader_Get(msg, JSTimeStamp, &val);
    if ((s == NATS_OK) && !nats_IsStringEmpty(val))
        s = nats_parseTime((char*) val, &tm);
    if ((s != NATS_OK) || (tm == 0))
        return nats_setError(NATS_ERR, "missing or invalid timestamp '%s'", val);

    val = NULL;
    s = natsMsgHeader_Get(msg, JSSubject, &val);
    if ((s != NATS_OK) || nats_IsStringEmpty(val))
        return nats_setError(NATS_ERR, "missing or invalid subject '%s'", val);

    // Will point the message subject to the JSSubject header value.
    // This will remain in the message memory allocated block, even
    // if later the user changes the JSSubject header.
    msg->subject = val;
    msg->seq = seq;
    msg->time = tm;
    return NATS_OK;
}

natsStatus
js_DirectGetMsg(natsMsg **msg, jsCtx *js, const char *stream, jsOptions *opts, jsDirectGetMsgOptions *dgOpts)
{
    natsStatus          s = NATS_OK;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                comma   = false;
    bool                doLBS   = false;
    jsOptions           o;
    char                buffer[64];
    natsBuffer          buf;

    if ((msg == NULL) || (js == NULL) || (dgOpts == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (nats_IsStringEmpty(stream))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamNameRequired);

    doLBS = !nats_IsStringEmpty(dgOpts->LastBySubject);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (doLBS)
        {
            if (nats_asprintf(&subj, jsApiDirectMsgGetLastBySubjectT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream, dgOpts->LastBySubject) < 0)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        else
        {
            if (nats_asprintf(&subj, jsApiDirectMsgGetT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    if ((s == NATS_OK) && doLBS)
    {
        IFOK(s, natsConnection_Request(&resp, js->nc, subj, NULL, 0, o.Wait));
    }
    else if (s == NATS_OK)
    {
        IFOK(s, natsBuf_InitWithBackend(&buf, buffer, 0, sizeof(buffer)));
        IFOK(s, natsBuf_AppendByte(&buf, '{'));
        if ((s == NATS_OK) && (dgOpts->Sequence > 0))
        {
            comma = true;
            s = nats_marshalULong(&buf, false, "seq", dgOpts->Sequence);
        }
        if ((s == NATS_OK) && !nats_IsStringEmpty(dgOpts->NextBySubject))
        {
            if (comma)
                s = natsBuf_AppendByte(&buf, ',');

            comma = true;
            IFOK(s, natsBuf_Append(&buf, "\"next_by_subj\":\"", -1));
            IFOK(s, natsBuf_Append(&buf, dgOpts->NextBySubject, -1));
            IFOK(s, natsBuf_AppendByte(&buf, '"'));
        }
        IFOK(s, natsBuf_AppendByte(&buf, '}'));
        // Send the request
        IFOK(s, natsConnection_Request(&resp, js->nc, subj, natsBuf_Data(&buf), natsBuf_Len(&buf), o.Wait));

        natsBuf_Cleanup(&buf);
    }
    // Convert the response to a JS message returned to the user.
    IFOK(s, js_directGetMsgToJSMsg(stream, resp));

    NATS_FREE(subj);

    if (s == NATS_OK)
        *msg = resp;
    else
        natsMsg_Destroy(resp);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_deleteMsg(jsCtx *js, bool noErase, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                success = false;
    jsOptions           o;
    char                buffer[64];
    natsBuffer          buf;

    if (errCode != NULL)
        *errCode = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (nats_IsStringEmpty(stream))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrStreamNameRequired);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiMsgDeleteT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_InitWithBackend(&buf, buffer, 0, sizeof(buffer)));
    IFOK(s, natsBuf_AppendByte(&buf, '{'));
    IFOK(s, nats_marshalULong(&buf, false, "seq", seq));
    if ((s == NATS_OK) && noErase)
        s = natsBuf_Append(&buf, ",\"no_erase\":true", -1);
    IFOK(s, natsBuf_AppendByte(&buf, '}'));

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, js->nc, subj, natsBuf_Data(&buf), natsBuf_Len(&buf), o.Wait));

    IFOK(s, _unmarshalSuccessResp(&success, resp, errCode));
    if ((s == NATS_OK) && !success)
        s = nats_setError(NATS_ERR, "failed to delete message %" PRIu64, seq);

    natsBuf_Cleanup(&buf);
    natsMsg_Destroy(resp);
    NATS_FREE(subj);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_DeleteMsg(jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    s = _deleteMsg(js, true, stream, seq, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_EraseMsg(jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    s = _deleteMsg(js, false, stream, seq, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalStreamInfoListResp(natsStrHash *m, apiPaged *page, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // If the error code is JSStreamNotFoundErr then pick NATS_NOT_FOUND.
        if (ar.Error.ErrCode == JSStreamNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
    {
        nats_JSON   **streams  = NULL;
        int         streamsLen = 0;

        IFOK(s, nats_JSONGetLong(json, "total", &page->total));
        IFOK(s, nats_JSONGetLong(json, "offset", &page->offset));
        IFOK(s, nats_JSONGetLong(json, "limit", &page->limit));
        IFOK(s, nats_JSONGetArrayObject(json, "streams", &streams, &streamsLen));
        if ((s == NATS_OK) && (streams != NULL))
        {
            int i;

            for (i=0; (s == NATS_OK) && (i<streamsLen); i++)
            {
                jsStreamInfo *si    = NULL;
                jsStreamInfo *osi   = NULL;

                s = js_unmarshalStreamInfo(streams[i], &si);
                if ((s == NATS_OK) && ((si->Config == NULL) || nats_IsStringEmpty(si->Config->Name)))
                    s = nats_setError(NATS_ERR, "%s", "stream name missing from configuration");
                IFOK(s, natsStrHash_SetEx(m, (char*) si->Config->Name, true, true, si, (void**) &osi));
                if (osi != NULL)
                    jsStreamInfo_Destroy(osi);
            }
            // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
            NATS_FREE(streams);
        }
    }
    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_Streams(jsStreamInfoList **new_list, jsCtx *js, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                done    = false;
    int64_t             offset  = 0;
    int64_t             start   = 0;
    int64_t             timeout = 0;
    natsStrHash         *streams= NULL;
    jsStreamInfoList    *list   = NULL;
    jsOptions           o;
    apiPaged            page;

    if (errCode != NULL)
        *errCode = 0;

    if ((new_list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiStreamListT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_Create(&buf, 64));
    IFOK(s, natsStrHash_Create(&streams, 16));

    if (s == NATS_OK)
    {
        memset(&page, 0, sizeof(apiPaged));
        start = nats_Now();
    }

    do
    {
        IFOK(s, natsBuf_AppendByte(buf, '{'));
        IFOK(s, nats_marshalLong(buf, false, "offset", offset));
        if (!nats_IsStringEmpty(o.Stream.Info.SubjectsFilter))
        {
            IFOK(s, natsBuf_Append(buf, ",\"subject\":\"", -1));
            IFOK(s, natsBuf_Append(buf, o.Stream.Info.SubjectsFilter, -1));
            IFOK(s, natsBuf_AppendByte(buf, '\"'));
        }
        IFOK(s, natsBuf_AppendByte(buf, '}'));

        timeout = o.Wait - (nats_Now() - start);
        if (timeout <= 0)
            s = NATS_TIMEOUT;

        // Send the request
        IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), timeout));

        IFOK(s, _unmarshalStreamInfoListResp(streams, &page, resp, errCode));
        if (s == NATS_OK)
        {
            offset += page.limit;
            done = offset >= page.total;
            if (!done)
            {
                // Reset the request buffer, we may be able to reuse.
                natsBuf_Reset(buf);
            }
        }
        natsMsg_Destroy(resp);
        resp = NULL;
    }
    while ((s == NATS_OK) && !done);

    natsBuf_Destroy(buf);
    NATS_FREE(subj);

    if (s == NATS_OK)
    {
        if (natsStrHash_Count(streams) == 0)
        {
            natsStrHash_Destroy(streams);
            return NATS_NOT_FOUND;
        }
        list = (jsStreamInfoList*) NATS_CALLOC(1, sizeof(jsStreamInfoList));
        if (list == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            list->List = (jsStreamInfo**) NATS_CALLOC(natsStrHash_Count(streams), sizeof(jsStreamInfo*));
            if (list->List == NULL)
            {
                NATS_FREE(list);
                list = NULL;
                s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            else
            {
                natsStrHashIter iter;
                void            *val = NULL;

                natsStrHashIter_Init(&iter, streams);
                while (natsStrHashIter_Next(&iter, NULL, (void**) &val))
                {
                    jsStreamInfo *si = (jsStreamInfo*) val;

                    list->List[list->Count++] = si;
                    natsStrHashIter_RemoveCurrent(&iter);
                }
                natsStrHashIter_Done(&iter);

                *new_list = list;
            }
        }
    }
    if (s != NATS_OK)
    {
        natsStrHashIter iter;
        void            *val = NULL;

        natsStrHashIter_Init(&iter, streams);
        while (natsStrHashIter_Next(&iter, NULL, (void**) &val))
        {
            jsStreamInfo *si = (jsStreamInfo*) val;

            natsStrHashIter_RemoveCurrent(&iter);
            jsStreamInfo_Destroy(si);
        }
        natsStrHashIter_Done(&iter);
    }
    natsStrHash_Destroy(streams);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsStreamInfoList_Destroy(jsStreamInfoList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        jsStreamInfo_Destroy(list->List[i]);

    NATS_FREE(list->List);
    NATS_FREE(list);
}

static natsStatus
_unmarshalNamesListResp(const char *fieldName, natsStrHash *m, apiPaged *page, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // If the error code is JSStreamNotFoundErr then pick NATS_NOT_FOUND.
        if (ar.Error.ErrCode == JSStreamNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
    {
        char    **names  = NULL;
        int     namesLen = 0;

        IFOK(s, nats_JSONGetLong(json, "total", &page->total));
        IFOK(s, nats_JSONGetLong(json, "offset", &page->offset));
        IFOK(s, nats_JSONGetLong(json, "limit", &page->limit));
        IFOK(s, nats_JSONGetArrayStr(json, fieldName, &names, &namesLen));
        if ((s == NATS_OK) && (names != NULL))
        {
            int i;

            for (i=0; (s == NATS_OK) && (i<namesLen); i++)
                s = natsStrHash_Set(m, names[i], true, NULL, NULL);

            // Free the array of JSON objects that was allocated by nats_JSONGetArrayStr.
            for (i=0; i<namesLen; i++)
                NATS_FREE(names[i]);
            NATS_FREE(names);
        }
    }
    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_StreamNames(jsStreamNamesList **new_list, jsCtx *js, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                done    = false;
    int64_t             offset  = 0;
    int64_t             start   = 0;
    int64_t             timeout = 0;
    natsStrHash         *names  = NULL;
    jsStreamNamesList   *list   = NULL;
    jsOptions           o;
    apiPaged            page;

    if (errCode != NULL)
        *errCode = 0;

    if ((new_list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiStreamNamesT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_Create(&buf, 64));
    IFOK(s, natsStrHash_Create(&names, 16));

    if (s == NATS_OK)
    {
        memset(&page, 0, sizeof(apiPaged));
        start = nats_Now();
    }

    do
    {
        IFOK(s, natsBuf_AppendByte(buf, '{'));
        IFOK(s, nats_marshalLong(buf, false, "offset", offset));
        if (!nats_IsStringEmpty(o.Stream.Info.SubjectsFilter))
        {
            IFOK(s, natsBuf_Append(buf, ",\"subject\":\"", -1));
            IFOK(s, natsBuf_Append(buf, o.Stream.Info.SubjectsFilter, -1));
            IFOK(s, natsBuf_AppendByte(buf, '\"'));
        }
        IFOK(s, natsBuf_AppendByte(buf, '}'));

        timeout = o.Wait - (nats_Now() - start);
        if (timeout <= 0)
            s = NATS_TIMEOUT;

        // Send the request
        IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), timeout));

        IFOK(s, _unmarshalNamesListResp("streams", names, &page, resp, errCode));
        if (s == NATS_OK)
        {
            offset += page.limit;
            done = offset >= page.total;
            if (!done)
            {
                // Reset the request buffer, we may be able to reuse.
                natsBuf_Reset(buf);
            }
        }
        natsMsg_Destroy(resp);
        resp = NULL;
    }
    while ((s == NATS_OK) && !done);

    natsBuf_Destroy(buf);
    NATS_FREE(subj);

    if (s == NATS_OK)
    {
        if (natsStrHash_Count(names) == 0)
        {
            natsStrHash_Destroy(names);
            return NATS_NOT_FOUND;
        }
        list = (jsStreamNamesList*) NATS_CALLOC(1, sizeof(jsStreamNamesList));
        if (list == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            list->List = (char**) NATS_CALLOC(natsStrHash_Count(names), sizeof(char*));
            if (list->List == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                natsStrHashIter iter;
                char            *sname = NULL;

                natsStrHashIter_Init(&iter, names);
                while ((s == NATS_OK) && natsStrHashIter_Next(&iter, &sname, NULL))
                {
                    char *copyName = NULL;

                    DUP_STRING(s, copyName, sname);
                    if (s == NATS_OK)
                    {
                        list->List[list->Count++] = copyName;
                        natsStrHashIter_RemoveCurrent(&iter);
                    }
                }
                natsStrHashIter_Done(&iter);
            }
            if (s == NATS_OK)
                *new_list = list;
            else
                jsStreamNamesList_Destroy(list);
        }
    }
    natsStrHash_Destroy(names);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsStreamNamesList_Destroy(jsStreamNamesList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        NATS_FREE(list->List[i]);

    NATS_FREE(list->List);
    NATS_FREE(list);
}

//
// Account related functions
//

static natsStatus
_unmarshalAccLimits(nats_JSON *json, jsAccountLimits *limits)
{
    natsStatus  s;
    nats_JSON   *obj = NULL;

    s = nats_JSONGetObject(json, "limits", &obj);
    if (obj == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    IFOK(s, nats_JSONGetLong(obj, "max_memory", &(limits->MaxMemory)));
    IFOK(s, nats_JSONGetLong(obj, "max_storage", &(limits->MaxStore)));
    IFOK(s, nats_JSONGetLong(obj, "max_streams", &(limits->MaxStreams)));
    IFOK(s, nats_JSONGetLong(obj, "max_consumers", &(limits->MaxConsumers)));
    IFOK(s, nats_JSONGetLong(obj, "max_ack_pending", &(limits->MaxAckPending)));
    IFOK(s, nats_JSONGetLong(obj, "memory_max_stream_bytes", &(limits->MemoryMaxStreamBytes)));
    IFOK(s, nats_JSONGetLong(obj, "storage_max_stream_bytes", &(limits->StoreMaxStreamBytes)));
    IFOK(s, nats_JSONGetBool(obj, "max_bytes_required", &(limits->MaxBytesRequired)));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_fillTier(void *userInfo, const char *subject, nats_JSONField *f)
{
    jsAccountInfo   *ai     = (jsAccountInfo*) userInfo;
    natsStatus      s       = NATS_OK;
    jsTier          *t      = NULL;
    nats_JSON       *json   = f->value.vobj;

    t = (jsTier*) NATS_CALLOC(1, sizeof(jsTier));
    if (t == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    ai->Tiers[ai->TiersLen++] = t;

    DUP_STRING(s, t->Name, subject);
    IFOK(s, nats_JSONGetULong(json, "memory", &(t->Memory)));
    IFOK(s, nats_JSONGetULong(json, "storage", &(t->Store)));
    IFOK(s, nats_JSONGetLong(json, "streams", &(t->Streams)));
    IFOK(s, nats_JSONGetLong(json, "consumers", &(t->Consumers)));
    IFOK(s, _unmarshalAccLimits(json, &(t->Limits)));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalAccTiers(nats_JSON *json, jsAccountInfo *ai)
{
    nats_JSON               *obj    = NULL;
    natsStatus              s       = NATS_OK;
    int                     n;

    s = nats_JSONGetObject(json, "tier", &obj);
    if (obj == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    n = natsStrHash_Count(obj->fields);
    if (n == 0)
        return NATS_OK;

    ai->Tiers = (jsTier**) NATS_CALLOC(n, sizeof(jsTier*));
    if (ai->Tiers == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONRange(obj, TYPE_OBJECT, 0, _fillTier, (void*) ai);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_unmarshalAccountInfo(nats_JSON *json, jsAccountInfo **new_ai)
{
    natsStatus          s;
    nats_JSON           *obj = NULL;
    jsAccountInfo       *ai  = NULL;

    ai = (jsAccountInfo*) NATS_CALLOC(1, sizeof(jsAccountInfo));
    if (ai == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetULong(json, "memory", &(ai->Memory));
    IFOK(s, nats_JSONGetULong(json, "storage", &(ai->Store)));
    IFOK(s, nats_JSONGetLong(json, "streams", &(ai->Streams)));
    IFOK(s, nats_JSONGetLong(json, "consumers", &(ai->Consumers)));
    IFOK(s, nats_JSONGetStr(json, "domain", &(ai->Domain)));
    IFOK(s, nats_JSONGetObject(json, "api", &obj));
    if ((s == NATS_OK) && (obj != NULL))
    {
        IFOK(s, nats_JSONGetULong(obj, "total", &(ai->API.Total)));
        IFOK(s, nats_JSONGetULong(obj, "errors", &(ai->API.Errors)));
        obj = NULL;
    }
    IFOK(s, _unmarshalAccLimits(json, &(ai->Limits)));
    IFOK(s, _unmarshalAccTiers(json, ai));

    if (s == NATS_OK)
        *new_ai = ai;
    else
        jsAccountInfo_Destroy(ai);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalAccountInfoResp(jsAccountInfo **new_ai, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;
        s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
        s = js_unmarshalAccountInfo(json, new_ai);

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_GetAccountInfo(jsAccountInfo **new_ai, jsCtx *js, jsOptions *opts, jsErrCode *errCode)
{
    natsMsg             *resp   = NULL;
    char                *subj   = NULL;
    natsConnection      *nc     = NULL;
    natsStatus          s       = NATS_OK;
    bool                freePfx = false;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if (new_ai == NULL || js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiAccountInfo, js_lenWithoutTrailingDot(o.Prefix), o.Prefix) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, NULL, 0, o.Wait));

    // If we get a response, unmarshal the response
    IFOK(s, _unmarshalAccountInfoResp(new_ai, resp, errCode));

    // Common cleanup that is done regardless of success or failure.
    NATS_FREE(subj);
    natsMsg_Destroy(resp);
    return NATS_UPDATE_ERR_STACK(s);
}

void
jsAccountInfo_Destroy(jsAccountInfo *ai)
{
    if (ai == NULL)
        return;

    if (ai->Tiers != NULL)
    {
        int i;
        for (i=0; i<ai->TiersLen; i++)
        {
            jsTier *t = ai->Tiers[i];

            NATS_FREE((char*) t->Name);
            NATS_FREE(t);
        }
        NATS_FREE(ai->Tiers);
    }
    NATS_FREE(ai->Domain);
    NATS_FREE(ai);
}

natsStatus
jsPlacement_Init(jsPlacement *placement)
{
    if (placement == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(placement, 0, sizeof(jsPlacement));
    return NATS_OK;
}

natsStatus
jsStreamSource_Init(jsStreamSource *source)
{
    if (source == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(source, 0, sizeof(jsStreamSource));
    return NATS_OK;

}

natsStatus
jsExternalStream_Init(jsExternalStream *external)
{
    if (external == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(external, 0, sizeof(jsExternalStream));
    return NATS_OK;
}

natsStatus
jsRePublish_Init(jsRePublish *rp)
{
    if (rp == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(rp, 0, sizeof(jsRePublish));
    return NATS_OK;
}

//
// Consumer related functions
//

static natsStatus
_marshalDeliverPolicy(natsBuffer *buf, jsDeliverPolicy p)
{
    natsStatus  s;
    const char  *dp = NULL;

    s = natsBuf_Append(buf, "\"deliver_policy\":\"", -1);
    switch (p)
    {
        case js_DeliverAll:             dp = jsDeliverAllStr;           break;
        case js_DeliverLast:            dp = jsDeliverLastStr;          break;
        case js_DeliverNew:             dp = jsDeliverNewStr;           break;
        case js_DeliverByStartSequence: dp = jsDeliverBySeqStr;         break;
        case js_DeliverByStartTime:     dp = jsDeliverByTimeStr;        break;
        case js_DeliverLastPerSubject:  dp = jsDeliverLastPerSubjectStr;break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid deliver policy %d", (int) p);
    }
    IFOK(s, natsBuf_Append(buf, dp, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalAckPolicy(natsBuffer *buf, jsAckPolicy p)
{
    natsStatus  s;
    const char  *ap;

    s = natsBuf_Append(buf, ",\"ack_policy\":\"", -1);
    switch (p)
    {
        case js_AckNone: ap = jsAckNoneStr; break;
        case js_AckAll: ap = jsAckAllStr; break;
        case js_AckExplicit: ap = jsAckExplictStr; break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid ack policy %d", (int)p);
    }
    IFOK(s, natsBuf_Append(buf, ap, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalReplayPolicy(natsBuffer *buf, jsReplayPolicy p)
{
    natsStatus  s;
    const char  *rp = NULL;

    s = natsBuf_Append(buf, ",\"replay_policy\":\"", -1);
    switch (p)
    {
        case js_ReplayOriginal: rp = jsReplayOriginalStr;  break;
        case js_ReplayInstant:  rp = jsReplayInstantStr;   break;
        default:
            return nats_setError(NATS_INVALID_ARG, "invalid replay policy %d", (int)p);
    }
    IFOK(s, natsBuf_Append(buf, rp, -1));
    IFOK(s, natsBuf_AppendByte(buf, '"'));

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalConsumerCreateReq(natsBuffer **new_buf, const char *stream, jsConsumerConfig *cfg)
{
    natsStatus      s    = NATS_OK;
    natsBuffer      *buf = NULL;

    // If not set, set some defaults
    if ((int) cfg->DeliverPolicy < 0)
        cfg->DeliverPolicy = js_DeliverAll;
    if ((int) cfg->AckPolicy < 0)
        cfg->AckPolicy = js_AckExplicit;
    if ((int) cfg->ReplayPolicy < 0)
        cfg->ReplayPolicy = js_ReplayInstant;

    s = natsBuf_Create(&buf, 256);
    IFOK(s, natsBuf_Append(buf, "{\"stream_name\":\"", -1));
    IFOK(s, natsBuf_Append(buf, stream, -1));
    IFOK(s, natsBuf_Append(buf, "\",\"config\":{", -1));
    // Marshal something that is always present first, so that the optionals
    // will always start with a "," and we know that there will be a field before that.
    IFOK(s, _marshalDeliverPolicy(buf, cfg->DeliverPolicy));
    if ((s == NATS_OK) && !nats_IsStringEmpty(cfg->Name))
    {
        s = natsBuf_Append(buf, ",\"name\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->Name, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (!nats_IsStringEmpty(cfg->Description)))
    {
        s = natsBuf_Append(buf, ",\"description\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->Description, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (!nats_IsStringEmpty(cfg->Durable)))
    {
        s = natsBuf_Append(buf, ",\"durable_name\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->Durable, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (!nats_IsStringEmpty(cfg->DeliverSubject)))
    {
        s = natsBuf_Append(buf, ",\"deliver_subject\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->DeliverSubject, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (!nats_IsStringEmpty(cfg->DeliverGroup)))
    {
        s = natsBuf_Append(buf, ",\"deliver_group\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->DeliverGroup, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (cfg->OptStartSeq > 0))
        s = nats_marshalLong(buf, true, "opt_start_seq", cfg->OptStartSeq);
    if ((s == NATS_OK) && (cfg->OptStartTime > 0))
        s = _marshalTimeUTC(buf, true, "opt_start_time", cfg->OptStartTime);
    IFOK(s, _marshalAckPolicy(buf, cfg->AckPolicy));
    if ((s == NATS_OK) && (cfg->AckWait > 0))
        s = nats_marshalLong(buf, true, "ack_wait", cfg->AckWait);
    if ((s == NATS_OK) && (cfg->MaxDeliver > 0))
        s = nats_marshalLong(buf, true, "max_deliver", cfg->MaxDeliver);
    if ((s == NATS_OK) && !nats_IsStringEmpty(cfg->FilterSubject))
    {
        s = natsBuf_Append(buf, ",\"filter_subject\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->FilterSubject, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (cfg->FilterSubjectsLen > 0))
    {
        int i;

        s = natsBuf_Append(buf, ",\"filter_subjects\":[", -1);
        for (i = 0; (s == NATS_OK) && (i < cfg->FilterSubjectsLen); i++)
        {
            if (i > 0)
                s = natsBuf_AppendByte(buf, ',');
            IFOK(s, natsBuf_AppendByte(buf, '"'));
            IFOK(s, natsBuf_Append(buf, cfg->FilterSubjects[i], -1));
            IFOK(s, natsBuf_AppendByte(buf, '"'));
        }

        IFOK(s, natsBuf_AppendByte(buf, ']'));
    }
    IFOK(s, nats_marshalMetadata(buf, true, "metadata", cfg->Metadata));
    if ((s == NATS_OK) && (cfg->PauseUntil > 0))
        s = _marshalTimeUTC(buf, true, "pause_until", cfg->PauseUntil);
    IFOK(s, _marshalReplayPolicy(buf, cfg->ReplayPolicy))
    if ((s == NATS_OK) && (cfg->RateLimit > 0))
        s = nats_marshalULong(buf, true, "rate_limit_bps", cfg->RateLimit);
    if ((s == NATS_OK) && !nats_IsStringEmpty(cfg->SampleFrequency))
    {
        s = natsBuf_Append(buf, ",\"sample_freq\":\"", -1);
        IFOK(s, natsBuf_Append(buf, cfg->SampleFrequency, -1));
        IFOK(s, natsBuf_AppendByte(buf, '"'));
    }
    if ((s == NATS_OK) && (cfg->MaxWaiting > 0))
        s = nats_marshalLong(buf, true, "max_waiting", cfg->MaxWaiting);
    if ((s == NATS_OK) && (cfg->MaxAckPending > 0))
        s = nats_marshalLong(buf, true, "max_ack_pending", cfg->MaxAckPending);
    if ((s == NATS_OK) && cfg->FlowControl)
        s = natsBuf_Append(buf, ",\"flow_control\":true", -1);
    if ((s == NATS_OK) && (cfg->Heartbeat > 0))
        s = nats_marshalLong(buf, true, "idle_heartbeat", cfg->Heartbeat);
    if ((s == NATS_OK) && cfg->HeadersOnly)
        s = natsBuf_Append(buf, ",\"headers_only\":true", -1);
    if ((s == NATS_OK) && (cfg->MaxRequestBatch > 0))
        s = nats_marshalLong(buf, true, "max_batch", cfg->MaxRequestBatch);
    if ((s == NATS_OK) && (cfg->MaxRequestExpires > 0))
        s = nats_marshalLong(buf, true, "max_expires", cfg->MaxRequestExpires);
    if ((s == NATS_OK) && (cfg->MaxRequestMaxBytes > 0))
        s = nats_marshalLong(buf, true, "max_bytes", cfg->MaxRequestMaxBytes);
    if ((s == NATS_OK) && (cfg->InactiveThreshold > 0))
        s = nats_marshalLong(buf, true, "inactive_threshold", cfg->InactiveThreshold);
    if ((s == NATS_OK) && (cfg->BackOff != NULL) && (cfg->BackOffLen > 0))
    {
        char    tmp[32];
        int     i;

        s = natsBuf_Append(buf, ",\"backoff\":[", -1);
        for (i=0; (s == NATS_OK) && (i<cfg->BackOffLen); i++)
        {
            snprintf(tmp, sizeof(tmp), "%" PRId64, cfg->BackOff[i]);
            if (i > 0)
                s = natsBuf_AppendByte(buf, ',');
            IFOK(s, natsBuf_Append(buf, tmp, -1));
        }
        IFOK(s, natsBuf_AppendByte(buf, ']'));
    }
    if ((s == NATS_OK) && (cfg->Replicas > 0))
        s = nats_marshalLong(buf, true, "num_replicas", cfg->Replicas);
    if ((s == NATS_OK) && cfg->MemoryStorage)
        s = natsBuf_Append(buf, ",\"mem_storage\":true", -1);
    IFOK(s, natsBuf_Append(buf, "}}", -1));

    if (s == NATS_OK)
        *new_buf = buf;
    else
        natsBuf_Destroy(buf);

    return NATS_UPDATE_ERR_STACK(s);
}

void
js_destroyConsumerConfig(jsConsumerConfig *cc)
{
    int i;

    if (cc == NULL)
        return;

    NATS_FREE((char*) cc->Name);
    NATS_FREE((char*) cc->Durable);
    NATS_FREE((char*) cc->Description);
    NATS_FREE((char*) cc->DeliverSubject);
    NATS_FREE((char*) cc->DeliverGroup);
    NATS_FREE((char*) cc->FilterSubject);
    for (i = 0; i < cc->FilterSubjectsLen; i++)
        NATS_FREE((char *)cc->FilterSubjects[i]);
    nats_freeMetadata(&(cc->Metadata));
    NATS_FREE((char *)cc->FilterSubjects);
    NATS_FREE((char *)cc->SampleFrequency);
    NATS_FREE(cc->BackOff);
    NATS_FREE(cc);
}

static natsStatus
_unmarshalDeliverPolicy(nats_JSON *json, const char *fieldName, jsDeliverPolicy *dp)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, fieldName, &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsDeliverAllStr) == 0)
        *dp = js_DeliverAll;
    else if (strcmp(str, jsDeliverLastStr) == 0)
        *dp = js_DeliverLast;
    else if (strcmp(str, jsDeliverNewStr) == 0)
        *dp = js_DeliverNew;
    else if (strcmp(str, jsDeliverBySeqStr) == 0)
        *dp = js_DeliverByStartSequence;
    else if (strcmp(str, jsDeliverByTimeStr) == 0)
        *dp = js_DeliverByStartTime;
    else if (strcmp(str, jsDeliverLastPerSubjectStr) == 0)
        *dp = js_DeliverLastPerSubject;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal delivery policy '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalAckPolicy(nats_JSON *json, const char *fieldName, jsAckPolicy *ap)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, fieldName, &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsAckNoneStr) == 0)
        *ap = js_AckNone;
    else if (strcmp(str, jsAckAllStr) == 0)
        *ap = js_AckAll;
    else if (strcmp(str, jsAckExplictStr) == 0)
        *ap = js_AckExplicit;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal ack policy '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalReplayPolicy(nats_JSON *json, const char *fieldName, jsReplayPolicy *rp)
{
    natsStatus  s    = NATS_OK;
    char        *str = NULL;

    s = nats_JSONGetStr(json, fieldName, &str);
    if (str == NULL)
        return NATS_UPDATE_ERR_STACK(s);

    if (strcmp(str, jsReplayOriginalStr) == 0)
        *rp = js_ReplayOriginal;
    else if (strcmp(str, jsReplayInstantStr) == 0)
        *rp = js_ReplayInstant;
    else
        s = nats_setError(NATS_ERR, "unable to unmarshal replay policy '%s'", str);

    NATS_FREE(str);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalConsumerConfig(nats_JSON *json, const char *fieldName, jsConsumerConfig **new_cc)
{
    natsStatus              s       = NATS_OK;
    jsConsumerConfig        *cc     = NULL;
    nats_JSON               *cjson  = NULL;

    cc = (jsConsumerConfig*) NATS_CALLOC(1, sizeof(jsConsumerConfig));
    if (cc == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetObject(json, fieldName, &cjson);
    if ((s == NATS_OK) && (cjson != NULL))
    {
        s = nats_JSONGetStr(cjson, "durable_name", (char**) &(cc->Durable));
        IFOK(s, nats_JSONGetStr(cjson, "name", (char**) &(cc->Name)));
        IFOK(s, nats_JSONGetStr(cjson, "description", (char**) &(cc->Description)));
        IFOK(s, nats_JSONGetStr(cjson, "deliver_subject", (char**) &(cc->DeliverSubject)));
        IFOK(s, nats_JSONGetStr(cjson, "deliver_group", (char**) &(cc->DeliverGroup)));
        IFOK(s, _unmarshalDeliverPolicy(cjson, "deliver_policy", &(cc->DeliverPolicy)));
        IFOK(s, nats_JSONGetULong(cjson, "opt_start_seq", &(cc->OptStartSeq)));
        IFOK(s, nats_JSONGetTime(cjson, "opt_start_time", &(cc->OptStartTime)));
        IFOK(s, _unmarshalAckPolicy(cjson, "ack_policy", &(cc->AckPolicy)));
        IFOK(s, nats_JSONGetLong(cjson, "ack_wait", &(cc->AckWait)));
        IFOK(s, nats_JSONGetLong(cjson, "max_deliver", &(cc->MaxDeliver)));
        IFOK(s, nats_JSONGetStr(cjson, "filter_subject", (char**) &(cc->FilterSubject)));
        IFOK(s, nats_JSONGetArrayStr(cjson, "filter_subjects", (char ***)&(cc->FilterSubjects), &(cc->FilterSubjectsLen)));
        IFOK(s, _unmarshalReplayPolicy(cjson, "replay_policy", &(cc->ReplayPolicy)));
        IFOK(s, nats_JSONGetULong(cjson, "rate_limit_bps", &(cc->RateLimit)));
        IFOK(s, nats_JSONGetStr(cjson, "sample_freq", (char**) &(cc->SampleFrequency)));
        IFOK(s, nats_JSONGetLong(cjson, "max_waiting", &(cc->MaxWaiting)));
        IFOK(s, nats_JSONGetLong(cjson, "max_ack_pending", &(cc->MaxAckPending)));
        IFOK(s, nats_JSONGetBool(cjson, "flow_control", &(cc->FlowControl)));
        IFOK(s, nats_JSONGetLong(cjson, "idle_heartbeat", &(cc->Heartbeat)));
        IFOK(s, nats_JSONGetBool(cjson, "headers_only", &(cc->HeadersOnly)));
        IFOK(s, nats_JSONGetLong(cjson, "max_batch", &(cc->MaxRequestBatch)));
        IFOK(s, nats_JSONGetLong(cjson, "max_expires", &(cc->MaxRequestExpires)));
        IFOK(s, nats_JSONGetLong(cjson, "max_bytes", &(cc->MaxRequestMaxBytes)));
        IFOK(s, nats_JSONGetLong(cjson, "inactive_threshold", &(cc->InactiveThreshold)));
        IFOK(s, nats_JSONGetArrayLong(cjson, "backoff", &(cc->BackOff), &(cc->BackOffLen)));
        IFOK(s, nats_JSONGetLong(cjson, "num_replicas", &(cc->Replicas)));
        IFOK(s, nats_JSONGetBool(cjson, "mem_storage", &(cc->MemoryStorage)));
        IFOK(s, nats_unmarshalMetadata(cjson, "metadata", &(cc->Metadata)));
    }

    if (s == NATS_OK)
        *new_cc = cc;
    else
        js_destroyConsumerConfig(cc);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalSeqInfo(nats_JSON *json, const char *fieldName, jsSequenceInfo *si)
{
    natsStatus  s    = NATS_OK;
    nats_JSON   *sij = NULL;

    s = nats_JSONGetObject(json, fieldName, &sij);
    if ((s == NATS_OK) && (sij != NULL))
    {
        IFOK(s, nats_JSONGetULong(sij, "consumer_seq", &(si->Consumer)));
        IFOK(s, nats_JSONGetULong(sij, "stream_seq", &(si->Stream)));
        IFOK(s, nats_JSONGetTime(sij, "last_active", &(si->Last)));
    }
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_unmarshalConsumerInfo(nats_JSON *json, jsConsumerInfo **new_ci)
{
    natsStatus          s   = NATS_OK;
    jsConsumerInfo      *ci = NULL;

    ci = (jsConsumerInfo*) NATS_CALLOC(1, sizeof(jsConsumerInfo));
    if (ci == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetStr(json, "stream_name", &(ci->Stream));
    IFOK(s, nats_JSONGetStr(json, "name", &(ci->Name)));
    IFOK(s, nats_JSONGetTime(json, "created", &(ci->Created)));
    IFOK(s, _unmarshalConsumerConfig(json, "config", &(ci->Config)));
    IFOK(s, _unmarshalSeqInfo(json, "delivered", &(ci->Delivered)));
    IFOK(s, _unmarshalSeqInfo(json, "ack_floor", &(ci->AckFloor)));
    IFOK(s, nats_JSONGetLong(json, "num_ack_pending", &(ci->NumAckPending)));
    IFOK(s, nats_JSONGetLong(json, "num_redelivered", &(ci->NumRedelivered)));
    IFOK(s, nats_JSONGetLong(json, "num_waiting", &(ci->NumWaiting)));
    IFOK(s, nats_JSONGetULong(json, "num_pending", &(ci->NumPending)));
    IFOK(s, _unmarshalClusterInfo(json, "cluster", &(ci->Cluster)));
    IFOK(s, nats_JSONGetBool(json, "push_bound", &(ci->PushBound)));
    IFOK(s, nats_JSONGetBool(json, "paused", &(ci->Paused)));
    IFOK(s, nats_JSONGetLong(json, "pause_remaining", &(ci->PauseRemaining)));
    if (s == NATS_OK)
        *new_ci = ci;
    else
        jsConsumerInfo_Destroy(ci);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalConsumerCreateOrGetResp(jsConsumerInfo **new_ci, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        if (ar.Error.ErrCode == JSConsumerNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else if (new_ci != NULL)
    {
        // At this point we need to unmarshal the consumer info itself.
        s = js_unmarshalConsumerInfo(json, new_ci);
    }

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_AddConsumer(jsConsumerInfo **new_ci, jsCtx *js,
               const char *stream, jsConsumerConfig *cfg,
               jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    natsConnection      *nc     = NULL;
    char                *subj   = NULL;
    bool                freePfx = false;
    natsMsg             *resp   = NULL;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    if (cfg == NULL)
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrConsumerConfigRequired);

    s = _checkStreamName(stream);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (!nats_IsStringEmpty(cfg->Name))
    {
        if ((s = js_checkConsName(cfg->Name, false)) != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }
    if (!nats_IsStringEmpty(cfg->Durable))
    {
        if ((s = js_checkConsName(cfg->Durable, true)) != NATS_OK)
            return NATS_UPDATE_ERR_STACK(s);
    }

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        int res;

        // If there is a Name in the config, this takes precedence.
        if (!nats_IsStringEmpty(cfg->Name))
        {
            // No subject filter, use <stream>.<consumer name>
            // otherwise, the filter subject goes at the end.
            if (!nats_IsStringEmpty(cfg->FilterSubject) && (cfg->FilterSubjectsLen == 0))
                res = nats_asprintf(&subj, jsApiConsumerCreateExWithFilterT,
                                    js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                                    stream, cfg->Name, cfg->FilterSubject);
            else
                res = nats_asprintf(&subj, jsApiConsumerCreateExT,
                                    js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                                    stream, cfg->Name);
        }
        else if (nats_IsStringEmpty(cfg->Durable))
            res = nats_asprintf(&subj, jsApiConsumerCreateT,
                                js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                                stream);
        else
            res = nats_asprintf(&subj, jsApiDurableCreateT,
                                js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                                stream, cfg->Durable);
        if (res < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, _marshalConsumerCreateReq(&buf, stream, cfg));

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), o.Wait));

    // If we got a response, check for error or return the consumer info result.
    IFOK(s, _unmarshalConsumerCreateOrGetResp(new_ci, resp, errCode));

    if ((s == NATS_OK)
        && (new_ci != NULL)
        && (cfg->FilterSubjectsLen > 0)
        && ((*new_ci)->Config->FilterSubjectsLen == 0))
    {
        s = nats_setError(NATS_INVALID_ARG, "%s", "multiple consumer filter subjects not supported by the server");
    }

    NATS_FREE(subj);
    natsMsg_Destroy(resp);
    natsBuf_Destroy(buf);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_UpdateConsumer(jsConsumerInfo **ci, jsCtx *js,
                  const char *stream, jsConsumerConfig *cfg,
                  jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s;

    if ((cfg != NULL) && nats_IsStringEmpty(cfg->Durable))
        return nats_setError(NATS_INVALID_ARG, "%s", jsErrDurRequired);

    s = js_AddConsumer(ci, js, stream, cfg, opts, errCode);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_GetConsumerInfo(jsConsumerInfo **new_ci, jsCtx *js,
                   const char *stream, const char *consumer,
                   jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    char                *subj   = NULL;
    bool                freePfx = false;
    natsConnection      *nc     = NULL;
    natsMsg             *resp   = NULL;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if ((js == NULL) || (new_ci == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    IFOK(s, js_checkConsName(consumer, false))
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiConsumerInfoT,
                          js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                          stream, consumer) < 0 )
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, NULL, 0, o.Wait));

    // If we got a response, check for error or return the consumer info result.
    IFOK(s, _unmarshalConsumerCreateOrGetResp(new_ci, resp, errCode));

    NATS_FREE(subj);
    natsMsg_Destroy(resp);

    if (s == NATS_NOT_FOUND)
    {
        nats_clearLastError();
        return s;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_DeleteConsumer(jsCtx *js, const char *stream, const char *consumer,
                  jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s = NATS_OK;
    char                *subj   = NULL;
    bool                freePfx = false;
    natsConnection      *nc     = NULL;
    natsMsg             *resp   = NULL;
    bool                success = false;
    jsOptions           o;

    if (errCode != NULL)
        *errCode = 0;

    if (js == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    IFOK(s, js_checkConsName(consumer, false))
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiConsumerDeleteT,
                          js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                          stream, consumer) < 0 )
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, NULL, 0, o.Wait));

    // If we got a response, check for error and success result.
    IFOK(s, _unmarshalSuccessResp(&success, resp, errCode));
    if ((s == NATS_OK) && !success)
        s = nats_setError(s, "failed to delete consumer '%s'", consumer);

    NATS_FREE(subj);
    natsMsg_Destroy(resp);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_unmarshalConsumerPauseResp(nats_JSON *json, jsConsumerPauseResponse **new_cpr)
{
    natsStatus s = NATS_OK;
    jsConsumerPauseResponse *cpr = NULL;

    cpr = (jsConsumerPauseResponse *)NATS_CALLOC(1, sizeof(jsConsumerPauseResponse));
    if (cpr == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    s = nats_JSONGetBool(json, "paused", &(cpr->Paused));
    IFOK(s, nats_JSONGetTime(json, "pause_until", &(cpr->PauseUntil)));
    IFOK(s, nats_JSONGetLong(json, "pause_remaining", &(cpr->PauseRemaining)));

    if (s == NATS_OK)
        *new_cpr = cpr;
    else
        jsConsumerPauseResponse_Destroy(cpr);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_unmarshalConsumerPauseResp(jsConsumerPauseResponse **new_cpr, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON *json = NULL;
    jsApiResponse ar;
    natsStatus s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int)ar.Error.ErrCode;

        if (ar.Error.ErrCode == JSConsumerNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else if (new_cpr != NULL)
    {
        // At this point we need to unmarshal the consumer info itself.
        s = js_unmarshalConsumerPauseResp(json, new_cpr);
    }

    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_marshalConsumerPauseReq(natsBuffer **new_buf, uint64_t pauseUntil)
{
    natsStatus s = NATS_OK;
    natsBuffer *buf = NULL;

    s = natsBuf_Create(&buf, 256);
    IFOK(s, natsBuf_AppendByte(buf, '{'));
    if ((s == NATS_OK) && (pauseUntil > 0)) {
        s = _marshalTimeUTC(buf, false, "pause_until", pauseUntil);
    }
    IFOK(s, natsBuf_AppendByte(buf, '}'));

    if (s == NATS_OK)
        *new_buf = buf;
    else
        natsBuf_Destroy(buf);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsConsumerPauseResponse_Destroy(jsConsumerPauseResponse *cpr)
{
    if (cpr == NULL)
        return;

    NATS_FREE(cpr);
}


natsStatus
js_PauseConsumer(jsConsumerPauseResponse **new_cpr, jsCtx *js,
                 const char *stream, const char *consumer,
                 uint64_t pauseUntil, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus s = NATS_OK;
    char *subj = NULL;
    bool freePfx = false;
    natsConnection *nc = NULL;
    natsBuffer *buf = NULL;
    natsMsg *resp = NULL;
    jsOptions o;

    if (errCode != NULL)
        *errCode = 0;

    if ((js == NULL) || (new_cpr == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    IFOK(s, js_checkConsName(consumer, false))
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiConsumerPauseT,
                          js_lenWithoutTrailingDot(o.Prefix), o.Prefix,
                          stream, consumer) < 0)
        {
            s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if (freePfx)
            NATS_FREE((char *)o.Prefix);
    }

    IFOK(s, _marshalConsumerPauseReq(&buf, pauseUntil));

    // Send the request
    IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), o.Wait));

    // If we got a response, check for error or return the consumer info result.
    IFOK(s, _unmarshalConsumerPauseResp(new_cpr, resp, errCode));

    NATS_FREE(subj);
    natsMsg_Destroy(resp);
    natsBuf_Destroy(buf);

    if (s == NATS_NOT_FOUND)
    {
        nats_clearLastError();
        return s;
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
jsConsumerConfig_Init(jsConsumerConfig *cc)
{
    if (cc == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    memset(cc, 0, sizeof(jsConsumerConfig));
    cc->AckPolicy       = -1;
    cc->DeliverPolicy   = -1;
    cc->ReplayPolicy    = -1;
    return NATS_OK;
}

void
jsConsumerInfo_Destroy(jsConsumerInfo *ci)
{
    if (ci == NULL)
        return;

    NATS_FREE(ci->Stream);
    NATS_FREE(ci->Name);
    js_destroyConsumerConfig(ci->Config);
    _destroyClusterInfo(ci->Cluster);
    NATS_FREE(ci);
}

static natsStatus
_unmarshalConsumerInfoListResp(natsStrHash *m, apiPaged *page, natsMsg *resp, jsErrCode *errCode)
{
    nats_JSON           *json = NULL;
    jsApiResponse       ar;
    natsStatus          s;

    s = js_unmarshalResponse(&ar, &json, resp);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    if (js_apiResponseIsErr(&ar))
    {
        if (errCode != NULL)
            *errCode = (int) ar.Error.ErrCode;

        // If the error code is JSStreamNotFoundErr then pick NATS_NOT_FOUND.
        if (ar.Error.ErrCode == JSStreamNotFoundErr)
            s = NATS_NOT_FOUND;
        else
            s = nats_setError(NATS_ERR, "%s", ar.Error.Description);
    }
    else
    {
        nats_JSON   **consumers  = NULL;
        int         consumersLen = 0;

        IFOK(s, nats_JSONGetLong(json, "total", &page->total));
        IFOK(s, nats_JSONGetLong(json, "offset", &page->offset));
        IFOK(s, nats_JSONGetLong(json, "limit", &page->limit));
        IFOK(s, nats_JSONGetArrayObject(json, "consumers", &consumers, &consumersLen));
        if ((s == NATS_OK) && (consumers != NULL))
        {
            int i;

            for (i=0; (s == NATS_OK) && (i<consumersLen); i++)
            {
                jsConsumerInfo *ci    = NULL;
                jsConsumerInfo *oci   = NULL;

                s = js_unmarshalConsumerInfo(consumers[i], &ci);
                if ((s == NATS_OK) && ((ci->Config == NULL) || nats_IsStringEmpty(ci->Name)))
                    s = nats_setError(NATS_ERR, "%s", "consumer name missing");
                IFOK(s, natsStrHash_SetEx(m, (char*) ci->Name, true, true, ci, (void**) &oci));
                if (oci != NULL)
                    jsConsumerInfo_Destroy(oci);
            }
            // Free the array of JSON objects that was allocated by nats_JSONGetArrayObject.
            NATS_FREE(consumers);
        }
    }
    js_freeApiRespContent(&ar);
    nats_JSONDestroy(json);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
js_Consumers(jsConsumerInfoList **new_list, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                done    = false;
    int64_t             offset  = 0;
    int64_t             start   = 0;
    int64_t             timeout = 0;
    natsStrHash         *cons   = NULL;
    jsConsumerInfoList  *list   = NULL;
    jsOptions           o;
    apiPaged            page;

    if (errCode != NULL)
        *errCode = 0;

    if ((new_list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiConsumerListT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_Create(&buf, 16));
    IFOK(s, natsStrHash_Create(&cons, 16));

    if (s == NATS_OK)
    {
        memset(&page, 0, sizeof(apiPaged));
        start = nats_Now();
    }

    do
    {
        IFOK(s, natsBuf_AppendByte(buf, '{'));
        IFOK(s, nats_marshalLong(buf, false, "offset", offset));
        IFOK(s, natsBuf_AppendByte(buf, '}'));

        timeout = o.Wait - (nats_Now() - start);
        if (timeout <= 0)
            s = NATS_TIMEOUT;

        // Send the request
        IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), timeout));

        IFOK(s, _unmarshalConsumerInfoListResp(cons, &page, resp, errCode));
        if (s == NATS_OK)
        {
            offset += page.limit;
            done = offset >= page.total;
            if (!done)
            {
                // Reset the request buffer, we may be able to reuse.
                natsBuf_Reset(buf);
            }
        }
        natsMsg_Destroy(resp);
        resp = NULL;
    }
    while ((s == NATS_OK) && !done);

    natsBuf_Destroy(buf);
    NATS_FREE(subj);

    if (s == NATS_OK)
    {
        if (natsStrHash_Count(cons) == 0)
        {
            natsStrHash_Destroy(cons);
            return NATS_NOT_FOUND;
        }
        list = (jsConsumerInfoList*) NATS_CALLOC(1, sizeof(jsConsumerInfoList));
        if (list == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            list->List = (jsConsumerInfo**) NATS_CALLOC(natsStrHash_Count(cons), sizeof(jsConsumerInfo*));
            if (list->List == NULL)
            {
                NATS_FREE(list);
                list = NULL;
                s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            else
            {
                natsStrHashIter iter;
                void            *val = NULL;

                natsStrHashIter_Init(&iter, cons);
                while (natsStrHashIter_Next(&iter, NULL, (void**) &val))
                {
                    jsConsumerInfo *ci = (jsConsumerInfo*) val;

                    list->List[list->Count++] = ci;
                    natsStrHashIter_RemoveCurrent(&iter);
                }
                natsStrHashIter_Done(&iter);

                *new_list = list;
            }
        }
    }
    if (s != NATS_OK)
    {
        natsStrHashIter iter;
        void            *val = NULL;

        natsStrHashIter_Init(&iter, cons);
        while (natsStrHashIter_Next(&iter, NULL, (void**) &val))
        {
            jsStreamInfo *si = (jsStreamInfo*) val;

            natsStrHashIter_RemoveCurrent(&iter);
            jsStreamInfo_Destroy(si);
        }
        natsStrHashIter_Done(&iter);
    }
    natsStrHash_Destroy(cons);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsConsumerInfoList_Destroy(jsConsumerInfoList *list)
{
    int i;

    if (list == NULL)
        return;

    for (i=0; i<list->Count; i++)
        jsConsumerInfo_Destroy(list->List[i]);

    NATS_FREE(list->List);
    NATS_FREE(list);
}

natsStatus
js_ConsumerNames(jsConsumerNamesList **new_list, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode)
{
    natsStatus          s       = NATS_OK;
    natsBuffer          *buf    = NULL;
    char                *subj   = NULL;
    natsMsg             *resp   = NULL;
    natsConnection      *nc     = NULL;
    bool                freePfx = false;
    bool                done    = false;
    int64_t             offset  = 0;
    int64_t             start   = 0;
    int64_t             timeout = 0;
    natsStrHash         *names  = NULL;
    jsConsumerNamesList *list   = NULL;
    jsOptions           o;
    apiPaged            page;

    if (errCode != NULL)
        *errCode = 0;

    if ((new_list == NULL) || (js == NULL))
        return nats_setDefaultError(NATS_INVALID_ARG);

    s = _checkStreamName(stream);
    if (s != NATS_OK)
        return NATS_UPDATE_ERR_STACK(s);

    s = js_setOpts(&nc, &freePfx, js, opts, &o);
    if (s == NATS_OK)
    {
        if (nats_asprintf(&subj, jsApiConsumerNamesT, js_lenWithoutTrailingDot(o.Prefix), o.Prefix, stream) < 0)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        if (freePfx)
            NATS_FREE((char*) o.Prefix);
    }
    IFOK(s, natsBuf_Create(&buf, 16));
    IFOK(s, natsStrHash_Create(&names, 16));

    if (s == NATS_OK)
    {
        memset(&page, 0, sizeof(apiPaged));
        start = nats_Now();
    }

    do
    {
        IFOK(s, natsBuf_AppendByte(buf, '{'));
        IFOK(s, nats_marshalLong(buf, false, "offset", offset));
        IFOK(s, natsBuf_AppendByte(buf, '}'));

        timeout = o.Wait - (nats_Now() - start);
        if (timeout <= 0)
            s = NATS_TIMEOUT;

        // Send the request
        IFOK_JSR(s, natsConnection_Request(&resp, nc, subj, natsBuf_Data(buf), natsBuf_Len(buf), timeout));

        IFOK(s, _unmarshalNamesListResp("consumers", names, &page, resp, errCode));
        if (s == NATS_OK)
        {
            offset += page.limit;
            done = offset >= page.total;
            if (!done)
            {
                // Reset the request buffer, we may be able to reuse.
                natsBuf_Reset(buf);
            }
        }
        natsMsg_Destroy(resp);
        resp = NULL;
    }
    while ((s == NATS_OK) && !done);

    natsBuf_Destroy(buf);
    NATS_FREE(subj);

    if (s == NATS_OK)
    {
        if (natsStrHash_Count(names) == 0)
        {
            natsStrHash_Destroy(names);
            return NATS_NOT_FOUND;
        }
        list = (jsConsumerNamesList*) NATS_CALLOC(1, sizeof(jsConsumerNamesList));
        if (list == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
        {
            list->List = (char**) NATS_CALLOC(natsStrHash_Count(names), sizeof(char*));
            if (list->List == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
            {
                natsStrHashIter iter;
                char            *sname = NULL;

                natsStrHashIter_Init(&iter, names);
                while ((s == NATS_OK) && natsStrHashIter_Next(&iter, &sname, NULL))
                {
                    char *copyName = NULL;

                    DUP_STRING(s, copyName, sname);
                    if (s == NATS_OK)
                    {
                        list->List[list->Count++] = copyName;
                        natsStrHashIter_RemoveCurrent(&iter);
                    }
                }
                natsStrHashIter_Done(&iter);
            }
            if (s == NATS_OK)
                *new_list = list;
            else
                jsConsumerNamesList_Destroy(list);
        }
    }
    natsStrHash_Destroy(names);

    return NATS_UPDATE_ERR_STACK(s);
}

void
jsConsumerNamesList_Destroy(jsConsumerNamesList *list)
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
js_cloneConsumerConfig(jsConsumerConfig *org, jsConsumerConfig **clone)
{
    natsStatus          s   = NATS_OK;
    jsConsumerConfig    *c  = NULL;

    *clone = NULL;
    if (org == NULL)
        return NATS_OK;

    c = (jsConsumerConfig*) NATS_CALLOC(1, sizeof(jsConsumerConfig));
    if (c == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    memcpy(c, org, sizeof(jsConsumerConfig));

    // Need to first set all pointers to NULL in case we fail to dup and then
    // do the cleanup.
    c->Name = NULL;
    c->Durable = NULL;
    c->Description = NULL;
    c->BackOff = NULL;
    c->FilterSubject = NULL;
    c->FilterSubjects = NULL;
    c->FilterSubjectsLen = 0;
    c->SampleFrequency = NULL;
    c->DeliverSubject = NULL;
    c->DeliverGroup = NULL;
    // Now dup all strings, etc...
    IF_OK_DUP_STRING(s, c->Name, org->Name);
    IF_OK_DUP_STRING(s, c->Durable, org->Durable);
    IF_OK_DUP_STRING(s, c->Description, org->Description);
    IF_OK_DUP_STRING(s, c->FilterSubject, org->FilterSubject);
    IF_OK_DUP_STRING(s, c->SampleFrequency, org->SampleFrequency);
    IF_OK_DUP_STRING(s, c->DeliverSubject, org->DeliverSubject);
    IF_OK_DUP_STRING(s, c->DeliverGroup, org->DeliverGroup);
    if ((s == NATS_OK) && (org->BackOff != NULL) && (org->BackOffLen > 0))
    {
        c->BackOff = (int64_t*) NATS_CALLOC(org->BackOffLen, sizeof(int64_t));
        if (c->BackOff == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
        else
            memcpy(c->BackOff, org->BackOff, org->BackOffLen*sizeof(int64_t));
    }
    if ((s == NATS_OK) && (org->FilterSubjects != NULL) && (org->FilterSubjectsLen > 0))
    {
        c->FilterSubjects = (const char **)NATS_CALLOC(org->FilterSubjectsLen, sizeof(const char *));
        if (c->FilterSubjects == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (int i = 0; (s == NATS_OK) && (i < org->FilterSubjectsLen); i++)
        {
            IF_OK_DUP_STRING(s, c->FilterSubjects[i], org->FilterSubjects[i]);
        }
        c->FilterSubjectsLen = org->FilterSubjectsLen;
    }
    IFOK(s, nats_cloneMetadata(&(c->Metadata), org->Metadata));
    if (s == NATS_OK)
        *clone = c;
    else
        js_destroyConsumerConfig(c);

    return NATS_UPDATE_ERR_STACK(s);
}
