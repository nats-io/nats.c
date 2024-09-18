// Copyright 2021-2024 The NATS Authors
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
#include "util.h"

#ifdef DEV_MODE
// For type safety

void js_lock(jsCtx *js);
void js_unlock(jsCtx *js);

#else
// We know what we are doing :-)

#define js_lock(js)     (natsMutex_Lock((js)->mu))
#define js_unlock(c)    (natsMutex_Unlock((js)->mu))

#endif // DEV_MODE

#define NATS_DEFAULT_ASYNC_FETCH_SIZE 128 // messages

extern const char*      jsDefaultAPIPrefix;
extern const int64_t    jsDefaultRequestWait;

#define jsMsgIdHdr                  "Nats-Msg-Id"
#define jsExpectedStreamHdr         "Nats-Expected-Stream"
#define jsExpectedLastSeqHdr        "Nats-Expected-Last-Sequence"
#define jsExpectedLastSubjSeqHdr    "Nats-Expected-Last-Subject-Sequence"
#define jsExpectedLastMsgIdHdr      "Nats-Expected-Last-Msg-Id"
#define jsConsumerStalledHdr        "Nats-Consumer-Stalled"

#define jsErrStreamNameRequired             "stream name is required"
#define jsErrConsumerNameRequired           "consumer name is required"
#define jsErrNoStreamMatchesSubject         "no stream matches subject"
#define jsErrPullSubscribeToPushConsumer    "cannot pull subscribe to push based consumer"
#define jsErrPullSubscribeRequired          "must use pull subscribe to bind to pull based consumer"
#define jsErrMsgNotBound                    "message not bound to a subscription"
#define jsErrMsgNotJS                       "not a JetStream message"
#define jsErrDurRequired                    "durable name is required"
#define jsErrNotAPullSubscription           "not a JetStream pull subscription"
#define jsErrNotAJetStreamSubscription      "not a JetStream subscription"
#define jsErrNotApplicableToPullSub         "not applicable to JetStream pull subscriptions"
#define jsErrNoHeartbeatForQueueSub         "a queue subscription cannot be created for a consumer with heartbeat"
#define jsErrNoFlowControlForQueueSub       "a queue subscription cannot be created for a consumer with flow control"
#define jsErrConsumerSeqMismatch            "consumer sequence mismatch"
#define jsErrOrderedConsNoDurable           "durable can not be set for an ordered consumer"
#define jsErrOrderedConsNoAckPolicy         "ack policy can not be set for an ordered consume"
#define jsErrOrderedConsNoMaxDeliver        "max deliver can not be set for an ordered consumer"
#define jsErrOrderedConsNoDeliverSubject    "deliver subject can not be set for an ordered consumer"
#define jsErrOrderedConsNoQueue             "queue can not be set for an ordered consumer"
#define jsErrOrderedConsNoBind              "can not bind existing consumer for an ordered consumer"
#define jsErrOrderedConsNoPullMode          "can not use pull mode for an ordered consumer"
#define jsErrStreamConfigRequired           "stream configuration required"
#define jsErrInvalidStreamName              "invalid stream name"
#define jsErrConsumerConfigRequired         "consumer configuration required"
#define jsErrInvalidDurableName             "invalid durable name"
#define jsErrInvalidConsumerName            "invalid consumer name"
#define jsErrConcurrentFetchNotAllowed      "concurrent fetch request not allowed"

#define jsCtrlHeartbeat     (1)
#define jsCtrlFlowControl   (2)

#define jsRetPolicyLimitsStr    "limits"
#define jsRetPolicyInterestStr  "interest"
#define jsRetPolicyWorkQueueStr "workqueue"

#define jsDiscardPolicyOldStr   "old"
#define jsDiscardPolicyNewStr   "new"

#define jsStorageTypeFileStr    "file"
#define jsStorageTypeMemStr     "memory"

#define jsStorageCompressionNoneStr "none"
#define jsStorageCompressionS2Str   "s2"

#define jsDeliverAllStr             "all"
#define jsDeliverLastStr            "last"
#define jsDeliverNewStr             "new"
#define jsDeliverBySeqStr           "by_start_sequence"
#define jsDeliverByTimeStr          "by_start_time"
#define jsDeliverLastPerSubjectStr  "last_per_subject"

#define jsAckNoneStr    "none"
#define jsAckAllStr     "all"
#define jsAckExplictStr "explicit"

#define jsReplayOriginalStr "original"
#define jsReplayInstantStr  "instant"

#define jsAckPrefix         "$JS.ACK."
#define jsAckPrefixLen      (8)

// Content of ACK messages sent to server
#define jsAckAck            "+ACK"
#define jsAckNak            "-NAK"
#define jsAckInProgress     "+WPI"
#define jsAckTerm           "+TERM"

// jsExtDomainT is used to create a StreamSource External APIPrefix
#define jsExtDomainT "$JS.%s.API"

// jsApiAccountInfo is for obtaining general information about JetStream.
#define jsApiAccountInfo "%.*s.INFO"

// jsApiStreamCreateT is the endpoint to create new streams.
#define jsApiStreamCreateT "%.*s.STREAM.CREATE.%s"

// jsApiStreamUpdateT is the endpoint to update existing streams.
#define jsApiStreamUpdateT "%.*s.STREAM.UPDATE.%s"

// jsApiStreamPurgeT is the endpoint to purge streams.
#define jsApiStreamPurgeT "%.*s.STREAM.PURGE.%s"

// jsApiStreamDeleteT is the endpoint to delete streams.
#define jsApiStreamDeleteT "%.*s.STREAM.DELETE.%s"

// jsApiStreamInfoT is the endpoint to get information on a stream.
#define jsApiStreamInfoT "%.*s.STREAM.INFO.%s"

// jsApiConsumerCreateT is used to create consumers.
#define jsApiConsumerCreateT "%.*s.CONSUMER.CREATE.%s"

// jsApiDurableCreateT is used to create durable consumers.
#define jsApiDurableCreateT "%.*s.CONSUMER.DURABLE.CREATE.%s.%s"

// jsApiConsumerCreateExT is used to create a named consumer.
#define jsApiConsumerCreateExT "%.*s.CONSUMER.CREATE.%s.%s"

// jsApiConsumerCreateExWithFilterT is used to create a named consumer with a filter subject.
#define jsApiConsumerCreateExWithFilterT "%.*s.CONSUMER.CREATE.%s.%s.%s"

// jsApiConsumerInfoT is used to get information about consumers.
#define jsApiConsumerInfoT "%.*s.CONSUMER.INFO.%s.%s"

// jsApiDeleteConsumerT is used to delete consumers.
#define jsApiConsumerDeleteT "%.*s.CONSUMER.DELETE.%s.%s"

// jsApiStreams can lookup a stream by subject.
#define jsApiStreams "%.*s.STREAM.NAMES"

// jsApiRequestNextT is the prefix for the request next message(s) for a consumer in worker/pull mode.
#define jsApiRequestNextT "%s.CONSUMER.MSG.NEXT.%s.%s"

// jsApiMsgDeleteT is the endpoint to remove a message.
#define jsApiMsgDeleteT "%.*s.STREAM.MSG.DELETE.%s"

// jsApiMsgGetT is the endpoint to get a message, either by sequence or last per subject.
#define jsApiMsgGetT "%.*s.STREAM.MSG.GET.%s"

// jsApiMsgGetT is the endpoint to get a message, either by sequence or last per subject.
#define jsApiDirectMsgGetT "%.*s.DIRECT.GET.%s"

// jsApiDirectMsgGetLastBySubjectT is the endpoint to perform a direct get of a message by subject.
#define jsApiDirectMsgGetLastBySubjectT "%.*s.DIRECT.GET.%s.%s"

// jsApiStreamListT is the endpoint to get the list of stream infos.
#define jsApiStreamListT "%.*s.STREAM.LIST"

// jsApiStreamNamesT is the endpoint to get the list of stream names.
#define jsApiStreamNamesT "%.*s.STREAM.NAMES"

// jsApiConsumerListT is the endpoint to get the list of consumers for a stream.
#define jsApiConsumerListT "%.*s.CONSUMER.LIST.%s"

// jsApiConsumerNamesT is the endpoint to get the list of consumer names for a stream.
#define jsApiConsumerNamesT "%.*s.CONSUMER.NAMES.%s"

// jsApiConsumerPauseT is the endpoint to pause a consumer.
#define jsApiConsumerPauseT "%.*s.CONSUMER.PAUSE.%s.%s"

// Creates a subject based on the option's prefix, the subject format and its values.
#define js_apiSubj(s, o, f, ...) (nats_asprintf((s), (f), (o)->Prefix, __VA_ARGS__) < 0 ? NATS_NO_MEMORY : NATS_OK)

// Execute the JS API request if status is OK on entry. If the result is NATS_NO_RESPONDERS,
// and `errCode` is not NULL, set it to JSNotEnabledErr.
#define IFOK_JSR(s, c)  if (s == NATS_OK) { s = (c); if ((s == NATS_NO_RESPONDERS) && (errCode != NULL)) { *errCode = JSNotEnabledErr; } }

// Returns true if the API response has a Code or ErrCode that is not 0.
#define js_apiResponseIsErr(ar)	(((ar)->Error.Code != 0) || ((ar)->Error.ErrCode != 0))

// jsApiError is included in all API responses if there was an error.
typedef struct __jsApiError
{
    int         Code;
    uint16_t    ErrCode;
    char        *Description;

} jsApiError;

// apiResponse is a standard response from the JetStream JSON API
typedef struct __jsApiResponse
{
    char        *Type;
    jsApiError 	Error;

} jsApiResponse;

#define JS_EMPTY_API_RESPONSE { NULL, { 0, 0, NULL } }

// Sets the options in `resOpts` based on the given `opts` and defaults to the context
// own options when some options are not specified.
// Returns also the NATS connection to be used to send the request.
// This function will get/release the context's lock.
natsStatus
js_setOpts(natsConnection **nc, bool *freePfx, jsCtx *js, jsOptions *opts, jsOptions *resOpts);

int
js_lenWithoutTrailingDot(const char *str);

natsStatus
js_unmarshalResponse(jsApiResponse *ar, nats_JSON **new_json, natsMsg *resp);

void
js_freeApiRespContent(jsApiResponse *ar);

natsStatus
js_unmarshalAccountInfo(nats_JSON *json, jsAccountInfo **new_ai);

natsStatus
js_marshalStreamConfig(natsBuffer **new_buf, jsStreamConfig *cfg);

natsStatus
js_unmarshalStreamConfig(nats_JSON *json, const char *fieldName, jsStreamConfig **new_cfg);

void
js_destroyStreamConfig(jsStreamConfig *cfg);

natsStatus
js_unmarshalStreamState(nats_JSON *pjson, const char *fieldName, jsStreamState *state);

natsStatus
js_unmarshalStreamInfo(nats_JSON *json, jsStreamInfo **new_si);

natsStatus
js_unmarshalConsumerInfo(nats_JSON *json, jsConsumerInfo **new_ci);

void
js_cleanStreamState(jsStreamState *state);

natsStatus
js_checkConsName(const char *cons, bool isDurable);

natsStatus
js_getMetaData(const char *reply,
    char **domain,
    char **stream,
    char **consumer,
    uint64_t *numDelivered,
    uint64_t *sseq,
    uint64_t *dseq,
    int64_t *tm,
    uint64_t *numPending,
    int asked);

void
js_retain(jsCtx *js);

void
js_release(jsCtx *js);

natsStatus
js_directGetMsgToJSMsg(const char *stream, natsMsg *msg);

natsStatus
js_cloneConsumerConfig(jsConsumerConfig *org, jsConsumerConfig **clone);

void
js_destroyConsumerConfig(jsConsumerConfig *cc);

natsStatus
js_checkFetchedMsg(natsSubscription *sub, natsMsg *msg, uint64_t fetchID, bool checkSts, bool *usrMsg);

natsStatus
js_maybeFetchMore(natsSubscription *sub, jsFetch *fetch);
