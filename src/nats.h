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

#ifndef NATS_H_
#define NATS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#include "status.h"
#include "version.h"

/** \def NATS_EXTERN
 *  \brief Needed for shared library.
 *
 *  Based on the platform this is compiled on, it will resolve to
 *  the appropriate instruction so that objects are properly exported
 *  when building the shared library.
 */
#if defined(_WIN32)
  #include <winsock2.h>
  #if defined(nats_EXPORTS)
    #define NATS_EXTERN __declspec(dllexport)
  #elif defined(nats_IMPORTS)
    #define NATS_EXTERN __declspec(dllimport)
  #else
    #define NATS_EXTERN
  #endif

  typedef SOCKET      natsSock;
#else
  #define NATS_EXTERN
  typedef int         natsSock;
#endif

/*! \mainpage %NATS C client.
 *
 * \section intro_sec Introduction
 *
 * The %NATS C Client is part of %NATS, an open-source cloud-native
 * messaging system, and is supported by [Synadia Communications Inc.](http://www.synadia.com).
 * This client, written in C, follows the go client closely, but
 * diverges in some places.
 *
 * \section install_sec Installation
 *
 * Instructions to build and install the %NATS C Client can be
 * found at the [NATS C Client GitHub page](https://github.com/nats-io/nats.c)
 *
 * \section faq_sec Frequently Asked Questions
 *
 * Some of the frequently asked questions can be found [here](https://github.com/nats-io/nats.c#faq)
 *
 * \section other_doc_section Other Documentation
 *
 * This documentation focuses on the %NATS C Client API; for additional
 * information, refer to the following:
 *
 * - [General Documentation for nats.io](http://nats.io/documentation)
 * - [NATS C Client found on GitHub](https://github.com/nats-io/nats.c)
 * - [The NATS Server (nats-server) found on GitHub](https://github.com/nats-io/nats-server)
 */

/** \brief The default `NATS Server` URL.
 *
 *  This is the default URL a `NATS Server`, running with default listen
 *  port, can be reached at.
 */
#define NATS_DEFAULT_URL "nats://localhost:4222"

/** \brief Message header for JetStream messages representing the message payload size
 *
 * When creating a JetStream consumer, if the `HeadersOnly` boolean is specified,
 * the subscription will receive messages with headers only (no message payload),
 * and a header of this name containing the size of the message payload that was
 * omitted.
 *
 * @see jsConsumerConfig
 */
 #define JSMsgSize    "Nats-Msg-Size"

/** \brief Message header for JetStream message for rollup
 *
 * If message is sent to a stream's subject with this header set, and the stream
 * is configured with `AllowRollup` option, then the server will insert this
 * message and delete all previous messages in the stream.
 *
 * If the header is set to #JSMsgRollupSubject, then only messages on the
 * specific subject this message is sent to are deleted.
 *
 * If the header is set to #JSMsgRollupAll, then all messages on all subjects
 * are deleted.
 */
 #define JSMsgRollup            "Nats-Rollup"

/** \brief Message header value causing rollup per subject
 *
 * This is a possible value for the #JSMsgRollup header indicating that only
 * messages for the subject the rollup message is sent will be removed.
 *
 * @see JSMsgRollup
 */
 #define JSMsgRollupSubject     "sub"

 /** \brief Message header value causing rollup for all subjects
 *
 * This is a possible value for the #JSMsgRollup header indicating that all
 * messages for all subjects will be removed.
 *
 * @see JSMsgRollup
 */
 #define JSMsgRollupAll         "all"

 // Headers for republished messages and direct get.
 #define JSStream       "Nats-Stream"
 #define JSSequence     "Nats-Sequence"
 #define JSLastSequence "Nats-Last-Sequence"
 #define JSTimeStamp    "Nats-Time-Stamp"
 #define JSSubject      "Nats-Subject"

//
// Types.
//
/** \defgroup typesGroup Types
 *
 *  NATS Types.
 *  @{
 */

/** \brief A connection to a `NATS Server`.
 *
 * A #natsConnection represents a bare connection to a `NATS Server`. It will
 * send and receive byte array payloads.
 */
typedef struct __natsConnection     natsConnection;

/** \brief Statistics of a #natsConnection
 *
 * Tracks various statistics received and sent on a connection,
 * including counts for messages and bytes.
 */
typedef struct __natsStatistics     natsStatistics;

/** \brief Interest on a given subject.
 *
 * A #natsSubscription represents interest in a given subject.
 */
typedef struct __natsSubscription   natsSubscription;

/** \brief A structure holding a subject, optional reply and payload.
 *
 * #natsMsg is a structure used by Subscribers and
 * #natsConnection_PublishMsg().
 */
typedef struct __natsMsg            natsMsg;

/** \brief Way to configure a #natsConnection.
 *
 * Options can be used to create a customized #natsConnection.
 */
typedef struct __natsOptions        natsOptions;

/** \brief Unique subject often used for point-to-point communication.
 *
 * This can be used as the reply for a request. Inboxes are meant to be
 * unique so that replies can be sent to a specific subscriber. That
 * being said, inboxes can be shared across multiple subscribers if
 * desired.
 */
typedef char                        natsInbox;

/** \brief A list of NATS messages.
 *
 * Used by some APIs which return a list of #natsMsg objects.
 *
 * Those APIs will not create the object, but instead initialize
 * the object to which a pointer to that object will be passed to it.
 * Typically, the user will define the object on the stack and
 * pass a pointer to this object to APIs that require a pointer
 * to a #natsMsgList object.
 *
 * Similarly, calling #natsMsgList_Destroy will call #natsMsg_Destroy
 * on any message still in the list, free the array containing pointers
 * to the messages, but not free the #natsMsgList object itself.
 *
 * \note If the user wants to keep some of the messages from the
 * list, the pointers of those messages in the `Msgs` array should
 * be set to `NULL`. The value `Count` MUST not be changed. The
 * function #natsMsgList_Destroy will iterate through all
 * pointers in the list and only destroy the ones that have not
 * been set to `NULL`.
 *
 * @see natsMsgList_Destroy
 */
typedef struct natsMsgList
{
        natsMsg         **Msgs;
        int             Count;

} natsMsgList;

/** \brief A type to represent user-provided metadata, a list of k=v pairs.
 *
 * Used in JetStream, microservice configuration.
 */

typedef struct natsMetadata
{
        // User-provided metadata for the stream, encoded as an array of {"key", "value",...}
        const char **List;
        // Number of key/value pairs in Metadata, 1/2 of the length of the array.
        int Count;
} natsMetadata;

/**
 * The JetStream context. Use for JetStream assets management and communication.
 *
 * \warning A context MUST not be destroyed concurrently with #jsCtx API calls
 * (for instance #js_Publish or #js_PublishAsync, etc...). However, it
 * is safe to destroy the context while a #jsPubAckErrHandler callback is
 * running or while inside #js_PublishAsyncComplete.
 */
typedef struct __jsCtx                  jsCtx;

/**
 * JetStream publish options.
 *
 * These are options that you can provide to JetStream publish APIs.
 *
 * The common usage will be to initialize a structure on the stack by
 * calling #jsPubOptions_Init. Note that strings are owned by
 * the application and need to be valid for the duration of the API
 * call this object is passed to.
 *
 * \note It is the user responsibility to free the strings if they
 * have been allocated.
 *
 * @see jsPubOptions_Init
 */
typedef struct jsPubOptions
{
        int64_t         MaxWait;                ///< Amount of time (in milliseconds) to wait for a publish response, default will the context's Wait value.
        const char      *MsgId;                 ///< Message ID used for de-duplication.
        const char      *ExpectStream;          ///< Expected stream to respond from the publish call.
        const char      *ExpectLastMsgId;       ///< Expected last message ID in the stream.
        uint64_t        ExpectLastSeq;          ///< Expected last message sequence in the stream.
        uint64_t        ExpectLastSubjectSeq;   ///< Expected last message sequence for the subject in the stream.
        bool            ExpectNoMessage;        ///< Expected no message (that is, sequence == 0) for the subject in the stream.

} jsPubOptions;

/**
 * Determines how messages in a set are retained.
 */
typedef enum
{
        js_LimitsPolicy = 0,    ///< Specifies that messages are retained until any given limit is reached, which could be one of MaxMsgs, MaxBytes, or MaxAge. This is the default.
        js_InterestPolicy,      ///< Specifies that when all known observables have acknowledged a message it can be removed.
        js_WorkQueuePolicy,     ///< Specifies that when the first worker or subscriber acknowledges the message it can be removed.

} jsRetentionPolicy;

/**
 * Determines how to proceed when limits of messages or bytes are reached.
 */
typedef enum
{
        js_DiscardOld = 0,  ///< Will remove older messages to return to the limits. This is the default.
        js_DiscardNew,      ///< Will fail to store new messages.

} jsDiscardPolicy;

/**
 * Determines how messages are stored for retention.
 */
typedef enum
{
        js_FileStorage = 0, ///< Specifies on disk storage. It's the default.
        js_MemoryStorage,   ///< Specifies in memory only.

} jsStorageType;

/**
 * Determines how messages are compressed when stored for retention.
 */
typedef enum
{
        js_StorageCompressionNone = 0, ///< Specifies no compression. It's the default.
        js_StorageCompressionS2,   ///< Specifies S2.
} jsStorageCompression;

/**
 * Determines how the consumer should select the first message to deliver.
 */
typedef enum
{
        js_DeliverAll = 0,          ///< Starts from the very beginning of a stream. This is the default.
        js_DeliverLast,             ///< Starts with the last sequence received.
        js_DeliverNew,              ///< Starts with messages sent after the consumer is created.
        js_DeliverByStartSequence,  ///< Starts from a given sequence.
        js_DeliverByStartTime,      ///< Starts from a given UTC time (number of nanoseconds since epoch)
        js_DeliverLastPerSubject,   ///< Starts with the last message for all subjects received.

} jsDeliverPolicy;

/**
 * Determines how the consumer should acknowledge delivered messages.
 */
typedef enum
{
        js_AckExplicit = 0,     ///< Requires ack or nack for all messages.
        js_AckNone,             ///< Requires no acks for delivered messages.
        js_AckAll,              ///< When acking a sequence number, this implicitly acks all sequences below this one as well.


} jsAckPolicy;

/**
 * Determines how the consumer should replay messages it already has queued in the stream.
 */
typedef enum
{
        js_ReplayInstant = 0,   ///< Replays messages as fast as possible.
        js_ReplayOriginal,      ///< Maintains the same timing as the messages were received.

} jsReplayPolicy;


/**
 * Used to guide placement of streams in clustered JetStream.
 *
 * Initialize the object with #jsPlacement_Init.
 *
 * \note The strings are applications owned and will not be freed by the library.
 *
 * See #jsStreamConfig for information on how to configure a stream.
 *
 * @see jsPlacement_Init
 */
typedef struct jsPlacement
{
        const char      *Cluster;
        const char      **Tags;
        int             TagsLen;

} jsPlacement;

/**
 * Allows you to qualify access to a stream source in another account.
 *
 * Initialize the object with #jsExternalStream_Init.
 *
 * \note The strings are applications owned and will not be freed by the library.
 *
 * See #jsStreamConfig for information on how to configure a stream.
 */
typedef struct jsExternalStream
{
        const char      *APIPrefix;
        const char      *DeliverPrefix;

} jsExternalStream;

/**
 * Dictates how streams can source from other streams.
 *
 * Initialize the object with #jsStreamSource_Init.
 *
 * \note The strings are applications owned and will not be freed by the library.
 *
 * \note The `OptStartTime` needs to be expressed as the number of nanoseconds
 * passed since 00:00:00 UTC Thursday, 1 January 1970.
 *
 * See #jsStreamConfig for information on how to configure a stream.
 */
typedef struct jsStreamSource
{
        const char              *Name;
        uint64_t                OptStartSeq;
        int64_t                 OptStartTime;   ///< UTC time expressed as number of nanoseconds since epoch.
        const char              *FilterSubject;
        jsExternalStream        *External;
        // Domain and External are mutually exclusive.
        // If Domain is set, an External value will be created with
        // the APIPrefix constructed based on the Domain value.
        const char              *Domain;

} jsStreamSource;

/**
 * Allows a source subject to be mapped to a destination subject for republishing.
 */
typedef struct jsRePublish
{
        const char              *Source;
        const char              *Destination;
        bool                    HeadersOnly;

} jsRePublish;

/**
 * SubjectTransformConfig is for applying a subject transform (to matching
 * messages) before doing anything else when a new message is received
 */
typedef struct jsSubjectTransformConfig
{
        const char *Source;
        const char *Destination;
} jsSubjectTransformConfig;

/**
 * SubjectTransformConfig is for applying a subject transform (to matching
 * messages) before doing anything else when a new message is received
 */
typedef struct jsStreamConsumerLimits
{
        int64_t InactiveThreshold;
        int MaxAckPending;
} jsStreamConsumerLimits;

/**
 * Configuration of a JetStream stream.
 *
 * There are sensible defaults for most. If no subjects are
 * given the name will be used as the only subject.
 *
 * In order to add/update a stream, a configuration needs to be set.
 * The typical usage would be to initialize all required objects on the stack
 * and configure them, then pass the pointer to the configuration to
 * #js_AddStream or #js_UpdateStream.
 *
 * \note The strings are applications owned and will not be freed by the library.
 *
 * \note NATS server 2.10 added user-provided Metadata, storage Compression
 * type, FirstSeq to specify the starting sequence number, and SubjectTransform.
 *
 * @see jsStreamConfig_Init
 *
 * \code{.unparsed}
 * jsStreamConfig   sc;
 * jsPlacement      p;
 * jsStreamSource   m;
 * jsExternalStream esm;
 * jsStreamSource   s1;
 * jsStreamSource   s2;
 * jsExternalStream esmS2;
 * const char       *subjects[]     = {"foo", "bar"};
 * const char       *tags[]         = {"tag1", "tag2"};
 * jsStreamSource   *sources[]      = {&s1, &s2};
 * jsRePublish      rp;
 *
 * jsStreamConfig_Init(&sc);
 *
 * jsPlacement_Init(&p);
 * p.Cluster = "MyCluster";
 * p.Tags = tags;
 * p.TagsLen = 2;
 *
 * jsStreamSource_Init(&m);
 * m.Name = "AStream";
 * m.OptStartSeq = 100;
 * m.FilterSubject = "foo";
 * jsExternalStream_Init(&esm);
 * esm.APIPrefix = "mirror.prefix.";
 * esm.DeliverPrefix = "deliver.prefix.";
 * m.External = &esm;
 *
 * jsStreamSource_Init(&s1);
 * s1.Name = "StreamOne";
 * s1.OptStartSeq = 10;
 * s1.FilterSubject = "stream.one";
 *
 * jsStreamSource_Init(&s2);
 * s2.Name = "StreamTwo";
 * s2.FilterSubject = "stream.two";
 * jsExternalStream_Init(&esmS2);
 * esmS2.APIPrefix = "mirror.prefix.";
 * esmS2.DeliverPrefix = "deliver.prefix.";
 * s2.External = &esmS2;
 *
 * sc.Name = "MyStream";
 * sc.Subjects = subjects;
 * sc.SubjectsLen = 2;
 * sc.Retention = js_InterestPolicy;
 * sc.Replicas = 3;
 * sc.Placement = &p;
 * sc.Mirror = &m;
 * sc.Sources = sources;
 * sc.SourcesLen = 2;
 *
 * // For RePublish subject:
 * jsRePublish_Init(&rp);
 * rp.Source = ">";
 * rp.Destination = "RP.>";
 * sc.RePublish = &rp;
 *
 * s = js_AddStream(&si, js, &sc, NULL, &jerr);
 * \endcode
 */
typedef struct jsStreamConfig {
        const char              *Name;
        const char              *Description;
        const char              **Subjects;
        int                     SubjectsLen;
        jsRetentionPolicy       Retention;
        int64_t                 MaxConsumers;
        int64_t                 MaxMsgs;
        int64_t                 MaxBytes;
        int64_t                 MaxAge;
        int64_t                 MaxMsgsPerSubject;
        int32_t                 MaxMsgSize;
        jsDiscardPolicy         Discard;
        jsStorageType           Storage;
        int64_t                 Replicas;
        bool                    NoAck;
        const char              *Template;
        int64_t                 Duplicates;
        jsPlacement             *Placement;
        jsStreamSource          *Mirror;
        jsStreamSource          **Sources;
        int                     SourcesLen;
        bool                    Sealed;         ///< Seal a stream so no messages can get our or in.
        bool                    DenyDelete;     ///< Restrict the ability to delete messages.
        bool                    DenyPurge;      ///< Restrict the ability to purge messages.
        /**
         * Allows messages to be placed into the system and purge
         * all older messages using a special message header.
         */
        bool                    AllowRollup;

        // Allow republish of the message after being sequenced and stored.
        jsRePublish             *RePublish;

        // Allow higher performance, direct access to get individual messages. E.g. KeyValue
        bool                    AllowDirect;
        // Allow higher performance and unified direct access for mirrors as well.
        bool                    MirrorDirect;

        // Allow KV like semantics to also discard new on a per subject basis
        bool                    DiscardNewPerSubject;

        /**
         * @brief Configuration options introduced in 2.10
         *
         * - Metadata is a user-provided array of key/value pairs, encoded as a
         *   string array [n1, v1, n2, v2, ...] representing key/value pairs
         *   {n1:v1, n2:v2, ...}.
         *
         * - Compression: js_StorageCompressionNone (default) or
         *   js_StorageCompressionS2
         *
         * - FirstSeq: the starting sequence number for the stream.
         *
         * - SubjectTransformConfig is for applying a subject transform (to
         *   matching messages) before doing anything else when a new message is
         *   received
         *
         * - ConsumerLimits is for setting the limits on certain options on all
         *   consumers of the stream.
         */

        natsMetadata Metadata;
        jsStorageCompression Compression;
        uint64_t FirstSeq;
        jsSubjectTransformConfig SubjectTransform;
        jsStreamConsumerLimits ConsumerLimits;
} jsStreamConfig;

/**
 * Information about messages that have been lost
 */
typedef struct jsLostStreamData
{
        uint64_t                *Msgs;
        int                     MsgsLen;
        uint64_t                Bytes;

} jsLostStreamData;

/**
 * This indicate that the given `Subject` in a stream contains `Msgs` messages.
 *
 * @see jsStreamStateSubjects
 */
typedef struct jsStreamStateSubject
{
        const char              *Subject;
        uint64_t                Msgs;

} jsStreamStateSubject;

/**
 * List of subjects optionally returned in the stream information request.
 *
 * This structure indicates the number of elements in the list, that is,
 * the list contains `Count` #jsStreamStateSubject elements.
 *
 * To get this list in #jsStreamState, you have to ask for it through #jsOptions.
 *
 * \code{.unparsed}
 * jsStreamInfo *si = NULL;
 * jsOptions    o;
 *
 * jsOptions_Init(&o);
 * o.Stream.Info.SubjectsFilter = "foo.>";
 * s = js_GetStreamInfo(&si, js, "MY_STREAM", &o, &jerr);
 *
 * // handle errors and assume si->State.Subjects is not NULL
 *
 * for (i=0; i<si->State.Subjects->Count; i++)
 * {
 *      jsStreamStateSubject *subj = &(si->State.Subjects->List[i]);
 *      printf("Subject=%s Messages count=%d\n", subj->Subject, (int) subj->Msgs);
 * }
 * \endcode
 *
 * @see jsStreamStateSubject
 * @see js_GetStreamInfo
 * @see jsOptions.Stream.Info.SubjectsFilter
 */
typedef struct jsStreamStateSubjects
{
        jsStreamStateSubject    *List;
        int                     Count;

} jsStreamStateSubjects;

/**
 * Information about the given stream
 *
 * \note `FirstTime` and `LastTime` are message timestamps expressed as the number
 * of nanoseconds passed since 00:00:00 UTC Thursday, 1 January 1970.
 */
typedef struct jsStreamState
{
        uint64_t                Msgs;
        uint64_t                Bytes;
        uint64_t                FirstSeq;
        int64_t                 FirstTime;      ///< UTC time expressed as number of nanoseconds since epoch.
        uint64_t                LastSeq;
        int64_t                 LastTime;       ///< UTC time expressed as number of nanoseconds since epoch.
        int64_t                 NumSubjects;
        jsStreamStateSubjects   *Subjects;
        uint64_t                NumDeleted;
        uint64_t                *Deleted;
        int                     DeletedLen;
        jsLostStreamData        *Lost;
        int64_t                 Consumers;

} jsStreamState;

/**
 * Information about all the peers in the cluster that
 * are supporting the stream or consumer.
 */
typedef struct jsPeerInfo
{
        char        *Name;
        bool        Current;
        bool        Offline;
        int64_t     Active;
        uint64_t    Lag;

} jsPeerInfo;

/**
 * Information about the underlying set of servers
 * that make up the stream or consumer.
 */
typedef struct jsClusterInfo
{
        char            *Name;
        char            *Leader;
        jsPeerInfo      **Replicas;
        int             ReplicasLen;

} jsClusterInfo;

/**
 * Information about an upstream stream source.
 */
typedef struct jsStreamSourceInfo
{
        char                    *Name;
        jsExternalStream        *External;
        uint64_t                Lag;
        int64_t                 Active;
        const char *            FilterSubject;
        jsSubjectTransformConfig *SubjectTransforms;
        int                     SubjectTransformsLen;

} jsStreamSourceInfo;

/**
 * Information about an alternate stream represented by a mirror.
 */
typedef struct jsStreamAlternate
{
        const char              *Name;
        const char              *Domain;
        const char              *Cluster;

} jsStreamAlternate;

/**
 * Configuration and current state for this stream.
 *
 * \note `Created` is the timestamp when the stream was created, expressed as
 * the number of nanoseconds passed since 00:00:00 UTC Thursday, 1 January 1970.
 */
typedef struct jsStreamInfo
{
        jsStreamConfig          *Config;
        int64_t                 Created;        ///< UTC time expressed as number of nanoseconds since epoch.
        jsStreamState           State;
        jsClusterInfo           *Cluster;
        jsStreamSourceInfo      *Mirror;
        jsStreamSourceInfo      **Sources;
        int                     SourcesLen;
        jsStreamAlternate       **Alternates;
        int                     AlternatesLen;

} jsStreamInfo;

/**
 * List of stream information objects returned by #js_Streams
 *
 * \note Once done, the list should be destroyed calling #jsStreamInfoList_Destroy
 *
 * @see jsStreamInfoList_Destroy
 */
typedef struct jsStreamInfoList
{
        jsStreamInfo    **List;
        int             Count;

} jsStreamInfoList;

/**
 * List of stream names returned by #js_StreamNames
 *
 * \note Once done, the list should be destroyed calling #jsStreamNamesList_Destroy
 *
 * @see jsStreamNamesList_Destroy
 */
typedef struct jsStreamNamesList
{
        char    **List;
        int     Count;

} jsStreamNamesList;

/**
 * Configuration of a JetStream consumer.
 *
 * In order to add a consumer, a configuration needs to be set.
 * The typical usage would be to initialize all required objects on the stack
 * and configure them, then pass the pointer to the configuration to
 * #js_AddConsumer.
 *
 * \note `OptStartTime` needs to be expressed as the number of nanoseconds
 * passed since 00:00:00 UTC Thursday, 1 January 1970.
 *
 * \note The strings are applications owned and will not be freed by the library.
 *
 * \note `SampleFrequency` is a sampling value, represented as a string such as "50"
 * for 50%, that causes the server to produce advisories for consumer ack metrics.
 *
 * \note `Durable` cannot contain the character ".".
 *
 * \note `HeadersOnly` means that the subscription will not receive any message payload,
 * instead, it will receive only messages headers (if present) with the addition of
 * the header #JSMsgSize ("Nats-Msg-Size"), whose value is the payload size.
 *
 * \note NATS server 2.10 added FilterSubjects, an array of multiple filter
 * subjects. It is mutually exclusive with the previously available single
 * FilterSubject.
 *
 * \note NATS server 2.10 added consumer Metadata which contains user-provided
 * string name/value pairs.
 *
 * @see jsConsumerConfig_Init
 *
 * \code{.unparsed}
 * jsConsumerInfo   *ci = NULL;
 * jsConsumerConfig cc;
 *
 * jsConsumerConfig_Init(&cc);
 * cc.Durable = "MY_DURABLE";
 * cc.DeliverSubject = "foo";
 * cc.DeliverPolicy = js_DeliverNew;
 *
 * s = js_AddConsumer(&ci, js, &cc, NULL, &jerr);
 * \endcode
 */
typedef struct jsConsumerConfig
{
        const char              *Name;
        const char              *Durable;
        const char              *Description;
        jsDeliverPolicy         DeliverPolicy;
        uint64_t                OptStartSeq;
        int64_t                 OptStartTime;           ///< UTC time expressed as number of nanoseconds since epoch.
        jsAckPolicy             AckPolicy;
        int64_t                 AckWait;
        int64_t                 MaxDeliver;
        int64_t                 *BackOff;               ///< Redelivery durations expressed in nanoseconds
        int                     BackOffLen;
        const char              *FilterSubject;
        jsReplayPolicy          ReplayPolicy;
        uint64_t                RateLimit;
        const char              *SampleFrequency;
        int64_t                 MaxWaiting;
        int64_t                 MaxAckPending;
        bool                    FlowControl;
        int64_t                 Heartbeat;              ///< Heartbeat interval expressed in number of nanoseconds.
        bool                    HeadersOnly;

        // Pull based options.
        int64_t                 MaxRequestBatch;        ///< Maximum Pull Consumer request batch size.
        int64_t                 MaxRequestExpires;      ///< Maximum Pull Consumer request expiration, expressed in number of nanoseconds.
        int64_t                 MaxRequestMaxBytes;     ///< Maximum Pull Consumer request maximum bytes.

        // Push based options.
        const char              *DeliverSubject;
        const char              *DeliverGroup;

        // Ephemeral inactivity threshold.
        int64_t                 InactiveThreshold;      ///< How long the server keeps an ephemeral after detecting loss of interest, expressed in number of nanoseconds.

        // Generally inherited by parent stream and other markers, now can be configured directly.
        int64_t                 Replicas;
        // Force memory storage.
        bool                    MemoryStorage;

        // Configuration options introduced in 2.10

        // Multiple filter subjects
        const char **FilterSubjects;
        int FilterSubjectsLen;

        // User-provided metadata for the consumer, encoded as an array of {"key", "value",...}
        natsMetadata Metadata;
} jsConsumerConfig;

/**
 * This represents a consumer sequence mismatch between the server and client
 * views.
 *
 * This can help applications find out if messages have been missed. Without
 * this and during a disconnect, it would be possible that a subscription
 * is not aware that it missed messages from the server. When acknowledgment
 * mode is other than #js_AckNone, messages would ultimately be redelivered,
 * but for #js_AckNone, they would not. But even with an acknowledgment mode
 * this may help finding sooner that something went wrong and let the application
 * decide if it wants to recreate the subscription starting at a given
 * sequence.
 *
 * The gap of missing messages could be calculated as `ConsumerServer-ConsumerClient`.
 *
 * @see natsSubscription_GetSequenceMismatch
 */
typedef struct jsConsumerSequenceMismatch
{
        uint64_t        Stream;         ///< This is the stream sequence that the application should resume from.
        uint64_t        ConsumerClient; ///< This is the consumer sequence that was last received by the library.
        uint64_t        ConsumerServer; ///< This is the consumer sequence last sent by the server.

} jsConsumerSequenceMismatch;

/**
 * JetStream subscribe options.
 *
 * These are options that you can provide to JetStream subscribe APIs.
 *
 * The common usage will be to initialize a structure on the stack by
 * calling #jsSubOptions_Init. Note that strings are owned by
 * the application and need to be valid for the duration of the API
 * call this object is passed to.
 *
 * \note It is the user responsibility to free the strings if they
 * have been allocated.
 *
 * @see jsSubOptions_Init
 */
typedef struct jsSubOptions
{
        /**
         * If specified, the library will only bind to this stream,
         * otherwise, the library communicates with the server to
         * get the stream name that has the matching subject given
         * to the #js_Subscribe family calls.
         */
        const char              *Stream;        ///< If specified, the consumer will be bound to this stream name.
        /**
         * If specified, the #js_Subscribe family calls will only
         * attempt to create a subscription for this matching consumer.
         *
         * That is, the consumer should exist prior to the call,
         * either created by the application calling #js_AddConsumer
         * or it should have been created with some other tools
         * such as the NATS cli.
         */
        const char              *Consumer;      ///< If specified, the subscription will be bound to an existing consumer from the `Stream` without attempting to create.
        /**
         * If specified, the low level NATS subscription will be a
         * queue subscription, which means that the load on the
         * delivery subject will be balanced across multiple members
         * of the same queue group.
         *
         * This makes sense only if the delivery subject in the
         * `Config` field of #jsSubOptions is the same for the
         * members of the same group.
         *
         * When no `Durable` name is specified in the `Config` block, then the
         * queue name will be used as the consumer's durable name. In this case,
         * the queue name cannot contain the character ".".
         */
        const char              *Queue;         ///< Queue name for queue subscriptions.
        /**
         * This has meaning only for asynchronous subscriptions,
         * and only if the consumer's acknowledgment mode is
         * other than #js_AckNone.
         *
         * For asynchronous subscriptions, the default behavior
         * is for the library to acknowledge the message once
         * the user callback returns.
         *
         * This option allows you to take control of when the
         * message should be acknowledged.
         */
        bool                    ManualAck;      ///< If true, the user will have to acknowledge the messages.
        /**
         * This allows the user to fully configure the JetStream
         * consumer.
         */
        jsConsumerConfig        Config;         ///< Consumer configuration.
        /**
         * This will create a fifo ephemeral consumer for in order delivery of
         * messages. There are no redeliveries and no acks.
         * Flow control and heartbeats are required and set by default, but
         * the heartbeats value can be overridden in the consumer configuration.
         */
        bool                    Ordered;        ///< If true, this will be an ordered consumer.

} jsSubOptions;

/**
 * Includes the consumer and stream sequence info from a JetStream consumer.
 */
typedef struct jsSequencePair
{
        uint64_t        Consumer;
        uint64_t        Stream;

} jsSequencePair;

/**
 * Has both the consumer and the stream sequence and last activity.
 */
typedef struct jsSequenceInfo
{
        uint64_t        Consumer;
        uint64_t        Stream;
        int64_t         Last;           ///< UTC time expressed as number of nanoseconds since epoch.

} jsSequenceInfo;

/**
 * Configuration and current state for this consumer.
 *
 * \note `Created` is the timestamp when the consumer was created, expressed as the number
 * of nanoseconds passed since 00:00:00 UTC Thursday, 1 January 1970.
 */
typedef struct jsConsumerInfo
{
        char                    *Stream;
        char                    *Name;
        int64_t                 Created;                ///< UTC time expressed as number of nanoseconds since epoch.
        jsConsumerConfig        *Config;
        jsSequenceInfo          Delivered;
        jsSequenceInfo          AckFloor;
        int64_t                 NumAckPending;
        int64_t                 NumRedelivered;
        int64_t                 NumWaiting;
        uint64_t                NumPending;
        jsClusterInfo           *Cluster;
        bool                    PushBound;

} jsConsumerInfo;

/**
 * List of consumers information objects returned by #js_Consumers
 *
 * \note Once done, the list should be destroyed calling #jsConsumerInfoList_Destroy
 *
 * @see jsStreamInfoList_Destroy
 */
typedef struct jsConsumerInfoList
{
        jsConsumerInfo  **List;
        int             Count;

} jsConsumerInfoList;

/**
 * List of consumer names returned by #js_ConsumerNames
 *
 * \note Once done, the list should be destroyed calling #jsConsumerNamesList_Destroy
 *
 * @see jsConsumerNamesList_Destroy
 */
typedef struct jsConsumerNamesList
{
        char    **List;
        int     Count;

} jsConsumerNamesList;

/**
 * Reports on API calls to JetStream for this account.
 */
typedef struct jsAPIStats
{
        uint64_t Total;
        uint64_t Errors;

} jsAPIStats;

/**
 * Includes the JetStream limits of the current account.
 */
typedef struct  jsAccountLimits
{
        int64_t MaxMemory;
        int64_t MaxStore;
        int64_t MaxStreams;
        int64_t MaxConsumers;
        int64_t MaxAckPending;
        int64_t MemoryMaxStreamBytes;
        int64_t StoreMaxStreamBytes;
        bool    MaxBytesRequired;

} jsAccountLimits;

typedef struct jsTier
{
        const char              *Name;
        uint64_t                Memory;
        uint64_t                Store;
        int64_t                 Streams;
        int64_t                 Consumers;
        jsAccountLimits         Limits;

} jsTier;

/**
 * Information about the JetStream usage from the current account.
 */
typedef struct jsAccountInfo
{
        uint64_t                Memory;
        uint64_t                Store;
        int64_t                 Streams;
        int64_t                 Consumers;
        char                    *Domain;
        jsAPIStats              API;
        jsAccountLimits         Limits;
        jsTier                  **Tiers;
        int                     TiersLen;

} jsAccountInfo;

/**
 * This represents the JetStream metadata associated with received messages.
 *
 * @see natsMsg_GetMetaData
 * @see jsMsgMetaData_Destroy
 *
 */
typedef struct jsMsgMetaData
{
        jsSequencePair  Sequence;
        uint64_t        NumDelivered;
        uint64_t        NumPending;
        int64_t         Timestamp;
        char            *Stream;
        char            *Consumer;
        char            *Domain;

} jsMsgMetaData;

/**
 * Ack received after successfully publishing a message.
 */
typedef struct jsPubAck
{
        char            *Stream;
        uint64_t        Sequence;
        char            *Domain;
        bool            Duplicate;

} jsPubAck;

/**
 * Publish acknowledgment failure that will be passed to the optional
 * #jsPubAckErrHandler callback.
 */
typedef struct jsPubAckErr
{
        natsMsg         *Msg;
        natsStatus      Err;
        jsErrCode       ErrCode;
        const char      *ErrText;

} jsPubAckErr;

#ifndef BUILD_IN_DOXYGEN
// Forward declarations
typedef void (*jsPubAckErrHandler)(jsCtx *js, jsPubAckErr *pae, void *closure);
typedef void (*jsPubAckHandler)(jsCtx *js, natsMsg *msg, jsPubAck *pa, jsPubAckErr *pae, void *closure);
#endif

/**
 * Options for the js_DirectGetMsg() call, which retrieves a message
 * from any server (not only the leader) as long as the stream has
 * been created with a AllowDirect option.
 *
 * Note that some options are mutually exclusive but are not checked
 * byt the library. The server will reject invalid requests and
 * the library will return the error returned from the server.
 */
typedef struct jsDirectGetMsgOptions
{
        uint64_t        Sequence;               ///< Get the message at this sequence
        const char      *NextBySubject;         ///< Get the next message (based on sequence) for that subject
        const char      *LastBySubject;         ///< Get the last message on that subject

} jsDirectGetMsgOptions;

/**
 * Options for the natsSubscription_FetchRequest() call, which is
 * similar to natsSubscription_Fetch() but gives more control in
 * the configuration of the fetch.
 */
typedef struct jsFetchRequest
{
        int64_t         Expires;        ///< Expiration of the request, expressed in nanoseconds
        int             Batch;          ///< Maximum number of messages to be received (see MaxBytes)
        int64_t         MaxBytes;       ///< Maximum bytes for the request (request complete based on whichever Batch or MaxBytes comes first)
        bool            NoWait;         ///< Will not wait if the request cannot be completed
        int64_t         Heartbeat;      ///< Have server sends heartbeats to help detect communication failures

} jsFetchRequest;

/**
 * JetStream context options.
 *
 * Initialize the object with #jsOptions_Init.
 */
typedef struct jsOptions
{
        const char              *Prefix;                        ///< JetStream prefix, default is "$JS.API"
        const char              *Domain;                        ///< Domain changes the domain part of JetSteam API prefix.
        int64_t                 Wait;                           ///< Amount of time (in milliseconds) to wait for various JetStream API requests, default is 5000 ms (5 seconds).

        /**
         * Publish Async options
         */
        struct jsOptionsPublishAsync
        {
                int64_t                 MaxPending;             ///< Maximum outstanding asynchronous publishes that can be inflight at one time.

                // If jsPubAckHandler is specified, the callback will be invoked
                // for every asynchronous published message, either as a positive
                // result, or with the error encountered when publishing that
                // message. If this callback is specified, ErrHandler (see below)
                // will be ignored.
                jsPubAckHandler         AckHandler;             ///< Callback invoked for each asynchronous published message.
                void                    *AckHandlerClosure;     ///< Closure (or user data) passed to #jsPubAckHandler callback.

                // This callback is invoked for messages published asynchronously
                // when an error is returned by the server or if the library has
                // timed-out waiting for an acknowledgment back from the server
                // (if publish uses the jsPubOptions.MaxWait).
                jsPubAckErrHandler      ErrHandler;             ///< Callback invoked when error encountered publishing a given message.
                void                    *ErrHandlerClosure;     ///< Closure (or user data) passed to #jsPubAckErrHandler callback.

                int64_t                 StallWait;              ///< Amount of time (in milliseconds) to wait in a PublishAsync call when there is MaxPending inflight messages, default is 200 ms.

        } PublishAsync;

        /**
         * Advanced stream options
         *
         * * `Purge` for advanced purge options.
         * * `Info` for advanced information retrieval options.
         */
        struct jsOptionsStream
        {
                /**
                 * Advanced stream purge options
                 *
                 * * `Subject` will filter the purge request to only messages that match the subject, which can have wildcards.<br>
                 * * `Sequence` will purge up to but not including this sequence and can be combined with subject filtering.<br>
                 * * `Keep` will specify how many messages to keep and can be combined with subject filtering.<br>
                 *
                 * \note `Sequence` and `Keep` are mutually exclusive, so both can not be set at the same time.
                 */
                struct jsOptionsStreamPurge
                {
                        const char      *Subject;       ///< This is the subject to match against messages for the purge command.
                        uint64_t        Sequence;       ///< Purge up to but not including sequence.
                        uint64_t        Keep;           ///< Number of messages to keep.

                } Purge;                                ///< Optional stream purge options.

                /**
                 * Advance stream information retrieval options
                 */
                struct jsOptionsStreamInfo
                {
                        bool            DeletedDetails;         ///< Get the list of deleted message sequences.
                        const char      *SubjectsFilter;        ///< Get the list of subjects in this stream.

                } Info;                                 ///< Optional stream information retrieval options.

        } Stream;                                       ///< Optional stream options.

} jsOptions;

/**
 * The KeyValue store object.
 */
typedef struct __kvStore                kvStore;

/**
 * The KeyValue entry object.
 */
typedef struct __kvEntry                kvEntry;

/**
 * The KeyValue status object.
 */
typedef struct __kvStatus               kvStatus;

/**
 * The KeyValue watcher object.
 */
typedef struct __kvWatcher              kvWatcher;

/**
 * Determines the type of operation of a #kvEntry
 */
typedef enum
{
        kvOp_Unknown = 0,
        kvOp_Put,
        kvOp_Delete,
        kvOp_Purge,

} kvOperation;

/**
 * KeyValue configuration object.
 *
 * Initialize the object with #kvConfig_Init.
 */
typedef struct kvConfig
{
        const char      *Bucket;
        const char      *Description;
        int32_t         MaxValueSize;
        uint8_t         History;
        int64_t         TTL;
        int64_t         MaxBytes;
        jsStorageType   StorageType;
        int             Replicas;
        jsRePublish     *RePublish;
        jsStreamSource  *Mirror;
        jsStreamSource  **Sources;
        int             SourcesLen;

} kvConfig;

/**
 * KeyValue watcher options object.
 *
 * Initialize the object with #kvWatchOptions_Init
 */
typedef struct kvWatchOptions
{
        bool            IgnoreDeletes;
        bool            IncludeHistory;
        bool            MetaOnly;
        int64_t         Timeout;        ///< How long to wait (in milliseconds) for some operations to complete.

} kvWatchOptions;

/**
 * KeyValue purge options object.
 *
 * Initialize the object with #kvPurgeOptions_Init
 */
typedef struct kvPurgeOptions
{
        // How long to wait (in milliseconds) for some operations to complete.
        int64_t         Timeout;

        // When calling kvStore_PurgeDeletes(), all keys that have a delete or
        // purge marker as the last entry are gathered and then those keys
        // are purged of their content, including the marker.
        // Starting with NATS C client v3.3.0, if this option is not specified,
        // only the markers older than 30 minutes will be deleted. Use this
        // option to set the limit or a negative value to force removal of
        // markers regardless of their age.
        // The value is expressed as a time in nanoseconds.
        int64_t         DeleteMarkersOlderThan;

} kvPurgeOptions;

/** \brief A list of KeyValue store entries.
 *
 * Used by some APIs which return a list of #kvEntry objects.
 *
 * Those APIs will not create the object, but instead initialize
 * the object.
 *
 * Typically, the user will define the object on the stack and
 * pass a pointer to this object to APIs that require a pointer
 * to a #kvEntryList object.
 *
 * Similarly, calling #kvEntryList_Destroy will call #kvEntry_Destroy
 * on entries in the list, free the array containing pointers
 * to the entries, but not free the #kvEntryList object itself.
 *
 * @see kvEntryList_Destroy
 */
typedef struct kvEntryList
{
        kvEntry         **Entries;
        int             Count;

} kvEntryList;

/** \brief A list of KeyValue store keys.
 *
 * Used by some APIs which return a list of key names.
 *
 * Those APIs will not create the object, but instead initialize
 * the object.
 *
 * Typically, the user will define the object on the stack and
 * pass a pointer to this object to APIs that require a pointer
 * to a #kvKeysList object.
 *
 * Similarly, calling #kvKeysList_Destroy will free key strings
 * in the list, free the array containing pointers to the keys,
 * but not free the #kvKeysList object itself.
 *
 * @see kvKeysList_Cleanup
 */
typedef struct kvKeysList
{
        char            **Keys;
        int             Count;

} kvKeysList;

#if defined(NATS_HAS_STREAMING)
/** \brief A connection to a `NATS Streaming Server`.
 *
 * A #stanConnection represents a connection to a `NATS Streaming Server`.
 */
typedef struct __stanConnection     stanConnection;

/** \brief Interest on a given channel.
 *
 * A #stanSubscription represents interest in a given channel.
 */
typedef struct __stanSubscription   stanSubscription;

/** \brief The Streaming message.
 *
 * #stanMsg is the object passed to the subscriptions' message callbacks.
 */
typedef struct __stanMsg            stanMsg;

/** \brief Way to configure a #stanConnection.
 *
 * Options can be used to create a customized #stanConnection.
 */
typedef struct __stanConnOptions    stanConnOptions;

/** \brief Way to configure a #stanSubscription.
 *
 * Options can be used to create a customized #stanSubscription.
 */
typedef struct __stanSubOptions     stanSubOptions;
#endif

/** @} */ // end of typesGroup

//
// Callbacks.
//

/** \defgroup callbacksGroup Callbacks
 *
 *  NATS Callbacks.
 *  @{
 */

/** \brief Callback used to deliver messages to the application.
 *
 * This is the callback that one provides when creating an asynchronous
 * subscription. The library will invoke this callback for each message
 * arriving through the subscription's connection.
 *
 * \warning If this callback is setup for a subject that is used as the reply
 * subject to #natsConnection_PublishRequest calls (and its variants), it
 * is possible to get an empty message with a header "Status" with value
 * "503" that is sent by the server when there were no responders on the
 * request's subject. Use #natsMsg_IsNoResponders to know if that is the case.
 *
 * @see natsConnection_Subscribe()
 * @see natsConnection_QueueSubscribe()
 * @see natsMsg_IsNoResponders()
 */
typedef void (*natsMsgHandler)(
        natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

/** \brief Callback used to notify the user of asynchronous connection events.
 *
 * This callback is used for asynchronous events such as disconnected
 * and closed connections.
 *
 * @see natsOptions_SetClosedCB()
 * @see natsOptions_SetDisconnectedCB()
 * @see natsOptions_SetReconnectedCB()
 *
 * \warning Such callback is invoked from a dedicated thread and the state
 *          of the connection that triggered the event may have changed since
 *          that event was generated.
 */
typedef void (*natsConnectionHandler)(
        natsConnection  *nc, void *closure);

/** \brief Callback used to notify the user of errors encountered while processing
 *         inbound messages.
 *
 * This callback is used to process asynchronous errors encountered while processing
 * inbound messages, such as #NATS_SLOW_CONSUMER.
 */
typedef void (*natsErrHandler)(
        natsConnection *nc, natsSubscription *subscription, natsStatus err,
        void *closure);

/** \brief Attach this connection to the external event loop.
 *
 * After a connection has (re)connected, this callback is invoked. It should
 * perform the necessary work to start polling the given socket for READ events.
 *
 * @param userData location where the adapter implementation will store the
 * object it created and that will later be passed to all other callbacks. If
 * `*userData` is not `NULL`, this means that this is a reconnect event.
 * @param loop the event loop (as a generic void*) this connection is being
 * attached to.
 * @param nc the connection being attached to the event loop.
 * @param socket the socket to poll for read/write events.
 */
typedef natsStatus (*natsEvLoop_Attach)(
        void            **userData,
        void            *loop,
        natsConnection  *nc,
        natsSock        socket);

/** \brief Read event needs to be added or removed.
 *
 * The `NATS` library will invoke this callback to indicate if the event
 * loop should start (`add is `true`) or stop (`add` is `false`) polling
 * for read events on the socket.
 *
 * @param userData the pointer to an user object created in #natsEvLoop_Attach.
 * @param add `true` if the event library should start polling, `false` otherwise.
 */
typedef natsStatus (*natsEvLoop_ReadAddRemove)(
        void            *userData,
        bool            add);

/** \brief Write event needs to be added or removed.
 *
 * The `NATS` library will invoke this callback to indicate if the event
 * loop should start (`add is `true`) or stop (`add` is `false`) polling
 * for write events on the socket.
 *
 * @param userData the pointer to an user object created in #natsEvLoop_Attach.
 * @param add `true` if the event library should start polling, `false` otherwise.
 */
typedef natsStatus (*natsEvLoop_WriteAddRemove)(
        void            *userData,
        bool            add);

/** \brief Detach from the event loop.
 *
 * The `NATS` library will invoke this callback to indicate that the connection
 * no longer needs to be attached to the event loop. User can cleanup some state.
 *
 * @param userData the pointer to an user object created in #natsEvLoop_Attach.
 */
typedef natsStatus (*natsEvLoop_Detach)(
        void            *userData);

/** \brief Callback used to fetch and return account signed user JWT.
 *
 * This handler is invoked when connecting and reconnecting. It should
 * return the user JWT that will be sent to the server.
 *
 * The user JWT is returned as a string that is allocated by the user and is
 * freed by the library after the handler is invoked.
 *
 * If the user is unable to return the JWT, a status other than `NATS_OK` should
 * be returned (we recommend `NATS_ERR`). A custom error message can be returned
 * through `customErrTxt`. The user must allocate the memory for this error
 * message and the library will free it after the invocation of the handler.
 *
 * \warning There may be repeated invocations of this handler for a given connection
 * so it is crucial to always return a copy of the user JWT maintained by the
 * application, since again, the library will free the memory pointed by `userJWT`
 * after each invocation of this handler.
 *
 * @see natsOptions_SetUserCredentialsCallbacks()
 * @see natsOptions_SetUserCredentialsFromFiles()
 */
typedef natsStatus (*natsUserJWTHandler)(
        char            **userJWT,
        char            **customErrTxt,
        void            *closure);


/** \brief Callback used to sign a nonce sent by the server.
 *
 * This handler is invoked when connecting and reconnecting. It should
 * sign the given `nonce` and return a raw signature through `signature` and
 * specify how many characters the signature has using `signatureLength`.
 *
 * The memory pointed by `signature` must be allocated by the user and
 * will be freed by the library after each invocation of this handler.
 *
 * If the user is unable to sign, a status other than `NATS_OK` (we recommend
 * `NATS_ERR`) should be returned. A custom error message can be returned
 * through `customErrTxt`. The user must allocate the memory for this error
 * message and the library will free it after the invocation of this handler.
 *
 * The library will base64 encode this raw signature and send that to the server.
 *
 * \warning There may be repeated invocations of this handler for a given connection
 * so it is crucial to always return a copy of the signature, since again,
 * the library will free the memory pointed by `signature` after each invocation
 * of this handler.
 *
 * @see natsOptions_SetUserCredentialsCallbacks()
 * @see natsOptions_SetUserCredentialsFromFiles()
 * @see natsOptions_SetNKey()
 */
typedef natsStatus (*natsSignatureHandler)(
        char            **customErrTxt,
        unsigned char   **signature,
        int             *signatureLength,
        const char      *nonce,
        void            *closure);

/** \brief Callback used to build a token on connections and reconnections.
 *
 * This is the function that one provides to build a different token at each reconnect.
 *
 * @see natsOptions_SetTokenHandler()
 *
 * \warning Such callback is invoked synchronously from the connection thread.
 */
typedef const char* (*natsTokenHandler)(void *closure);


/** \brief Callback used to notify that an object lifecycle is complete.
 *
 * Currently used for asynchronous #natsSubscription objects. When set, this callback will
 * be invoked after the subscription is closed and the message handler has returned.
 *
 * @see natsSubscription_SetOnCompleteCB()
 */
typedef void (*natsOnCompleteCB)(void *closure);

/** \brief Callback used to specify how long to wait between reconnects.
 *
 * This callback is used to get from the user the desired delay the library
 * should pause before attempting to reconnect again. Note that this is invoked
 * after the library tried the whole list of URLs and failed to reconnect.
 *
 * \note This callback is invoked from the connection reconnect thread and waits
 * for user input. It should not block and instead quickly return the desired
 * reconnect delay.
 * The state of the connection is disconnected when this callback is invoked.
 * Not much can be done with the passed connection, but user can call
 * #natsConnection_Close() if desired. This will abort the reconnect attempts
 * and close the connection.
 *
 * @param nc the pointer to the #natsConnection invoking this handler.
 * @param attempts the number of times the library tried the whole list of server URLs.
 * @param closure an optional pointer to a user defined object that was specified when
 * registering the callback.
 * @return the number of milliseconds to wait before trying to reconnect.
 */
typedef int64_t (*natsCustomReconnectDelayHandler)(natsConnection *nc, int attempts, void *closure);

#ifdef BUILD_IN_DOXYGEN
/** \brief Callback used to process asynchronous publish errors from JetStream.
 *
 * Callback used to process asynchronous publish errors from JetStream #js_PublishAsync
 * and #js_PublishMsgAsync calls. The provided #jsPubAckErr object gives the user
 * access to the encountered error along with the original message sent to the server
 * for possible restransmitting.
 *
 * \note If the message is resent, the library will not destroy the original
 * message and once again take ownership of it. To resend the message, do the
 * following so that the library knows not to destroy the message (since the
 * call will clear the `Msg` field from the #jsPubAckErr object).
 *
 * \code{.unparsed}
 * void myPAECallback(jsCtx *js, jsPubAckErr *pae, void* closure)
 * {
 *      ...
 *      // Resend the message
 *      js_PublishMsgAsync(js, &(pae->Msg), NULL);
 * }
 * \endcode
 *
 * \warning The #jsPubAckErr object and its content will be invalid as
 * soon as the callback returns.
 *
 * \warning Unlike a NATS message callback, the user does not have to destroy
 * the original NATS message (present in the #jsPubAckErr object), the
 * library will do it.
 *
 * @param js the pointer to the #jsCtx object.
 * @param pae the pointer to the #jsPubAckErr object.
 * @param closure an optional pointer to a user defined object that was specified when
 * registering the callback.
 */
typedef void (*jsPubAckErrHandler)(jsCtx *js, jsPubAckErr *pae, void *closure);

/** \brief Callback used to process asynchronous publish responses from JetStream.
 *
 * Callback used to process asynchronous publish responses (positive and negatives)
 * from JetStream #js_PublishAsync and #js_PublishMsgAsync calls. The provided
 * #jsPubAck or #jsPubAckErr objects give the user access to the successful
 * acknowledgment from the server or the encountered error along with the original
 * message sent to the server for possible restransmitting.
 *
 * \warning The user is responsible for destroying the message. If the message
 * is resent using the #js_PublishMsgAsync call, the library is taking ownership
 * of the message and calling #natsMsg_Destroy will have no effect because
 * the pointer will have been set to `NULL`. So it is recommended to always
 * call #natsMsg_Destroy at the end of the function.
 *
 * \code{.unparsed}
 * void myAckHandler(jsCtx *js, natsMsg *msg, jsPubAck *pa, jsPubAckErr *pae, void *closure)
 * {
 *      if (pa != NULL)
 *      {
 *              // Success case...
 *      }
 *      else if (pae != NULL)
 *      {
 *              // Error case...
 *              // If the application wants to resend the message:
 *              js_PublishMsgAsync(js, &msg, NULL);
 *      }
 *      natsMsg_Destroy(msg);
 * }
 * \endcode
 *
 * \warning The #jsPubAck and #jsPubAckErr objects and their content will be
 * invalid as soon as the callback returns.
 *
 * @param js the pointer to the #jsCtx object.
 * @param msg the pointer to the original published #natsMsg.
 * @param pa the pointer to the #jsPubAck object.
 * @param pae the pointer to the #jsPubAckErr object.
 * @param closure an optional pointer to a user defined object that was specified when
 * registering the callback.
 */
typedef void (*jsPubAckHandler)(jsCtx *js, natsMsg *msg, jsPubAck *pa, jsPubAckErr *pae, void *closure);
#endif

#if defined(NATS_HAS_STREAMING)
/** \brief Callback used to notify of an asynchronous publish result.
 *
 * This is used for asynchronous publishing to provide status of the acknowledgment.
 * The function will be passed the GUID and any error state. No error means the
 * message was successfully received by NATS Streaming.
 *
 * @see stanConnection_PublishAsync()
 */
typedef void (*stanPubAckHandler)(const char *guid, const char *error, void *closure);

/** \brief Callback used to deliver messages to the application.
 *
 * This is the callback that one provides when creating an asynchronous
 * subscription. The library will invoke this callback for each message
 * arriving through the subscription's connection.
 *
 * @see stanConnection_Subscribe()
 * @see stanConnection_QueueSubscribe()
 */
typedef void (*stanMsgHandler)(
        stanConnection *sc, stanSubscription *sub, const char *channel, stanMsg *msg, void *closure);

/** \brief Callback used to notify the user of the permanent loss of the connection.
 *
 * This callback is used to notify the user that the connection to the Streaming
 * server is permanently lost.
 *
 */
typedef void (*stanConnectionLostHandler)(
        stanConnection *sc, const char* errorTxt, void *closure);
#endif

/** @} */ // end of callbacksGroup

//
// Functions.
//
/** \defgroup funcGroup Functions
 *
 *  NATS Functions.
 *  @{
 */

/** \defgroup libraryGroup Library
 *
 *  Library and helper functions.
 * @{
 */

/** \brief Initializes the library.
 *
 * This initializes the library.
 *
 * It is invoked automatically when creating a connection, using a default
 * spin count. However, you can call this explicitly before creating the very
 * first connection in order for your chosen spin count to take effect.
 *
 * \warning You must not call #nats_Open and #nats_Close concurrently.
 *
 * @param lockSpinCount The number of times the library will spin trying to
 * lock a mutex object.
 */
NATS_EXTERN natsStatus
nats_Open(int64_t lockSpinCount);


/** \brief Returns the Library's version.
 *
 * Returns the version of the library your application is linked with.
 */
NATS_EXTERN const char*
nats_GetVersion(void);

/** \brief Returns the Library's version as a number.
 *
 * The version is returned as an hexadecimal number. For instance, if the
 * string version is "1.2.3", the value returned will be:
 *
 * > 0x010203
 */
NATS_EXTERN uint32_t
nats_GetVersionNumber(void);

#ifdef BUILD_IN_DOXYGEN
/** \brief Check that the header is compatible with the library.
 *
 * The version of the header you used to compile your application may be
 * incompatible with the library the application is linked with.
 *
 * This function will check that the two are compatibles. If they are not,
 * a message is printed and the application will exit.
 *
 * @return `true` if the header and library are compatibles, otherwise the
 * application exits.
 *
 * @see nats_GetVersion
 * @see nats_GetVersionNumber
 */
NATS_EXTERN bool nats_CheckCompatibility(void);
#else

#define nats_CheckCompatibility() nats_CheckCompatibilityImpl(NATS_VERSION_REQUIRED_NUMBER, \
                                                              NATS_VERSION_NUMBER, \
                                                              NATS_VERSION_STRING)

NATS_EXTERN bool
nats_CheckCompatibilityImpl(uint32_t reqVerNumber, uint32_t verNumber, const char *verString);

#endif

/** \brief Gives the current time in milliseconds.
 *
 * Gives the current time in milliseconds.
 */
NATS_EXTERN int64_t
nats_Now(void);

/** \brief Gives the current time in nanoseconds.
 *
 * Gives the current time in nanoseconds. When such granularity is not
 * available, the time returned is still expressed in nanoseconds.
 */
NATS_EXTERN int64_t
nats_NowInNanoSeconds(void);

/** \brief Sleeps for a given number of milliseconds.
 *
 * Causes the current thread to be suspended for at least the number of
 * milliseconds.
 *
 * @param sleepTime the number of milliseconds.
 */
NATS_EXTERN void
nats_Sleep(int64_t sleepTime);

/** \brief Returns the calling thread's last known error.
 *
 * Returns the calling thread's last known error. This can be useful when
 * #natsConnection_Connect fails. Since no connection object is returned,
 * you would not be able to call #natsConnection_GetLastError.
 *
 * @param status if not `NULL`, this function will store the last error status
 * in there.
 * @return the thread local error string.
 *
 * \warning Do not free the string returned by this function.
 */
NATS_EXTERN const char*
nats_GetLastError(natsStatus *status);

/** \brief Returns the calling thread's last known error stack.
 *
 * Copies the calling thread's last known error stack into the provided buffer.
 * If the buffer is not big enough, #NATS_INSUFFICIENT_BUFFER is returned.
 *
 * @param buffer the buffer into the stack is copied.
 * @param bufLen the size of the buffer
 */
NATS_EXTERN natsStatus
nats_GetLastErrorStack(char *buffer, size_t bufLen);

/** \brief Prints the calling thread's last known error stack into the file.
 *
 * This call prints the calling thread's last known error stack into the file `file`.
 * It first prints the error status and the error string, then the stack.
 *
 * Here is an example for a call:
 *
 * \code{.unparsed}
 * Error: 29 - SSL Error - (conn.c:565): SSL handshake error: sslv3 alert bad certificate
 * Stack: (library version: 1.2.3-beta)
 *   01 - _makeTLSConn
 *   02 - _checkForSecure
 *   03 - _processExpectedInfo
 *   04 - _processConnInit
 *   05 - _connect
 *   06 - natsConnection_Connect
 * \endcode
 *
 * @param file the file the stack is printed to.
 */
NATS_EXTERN void
nats_PrintLastErrorStack(FILE *file);

/** \brief Sets the maximum size of the global message delivery thread pool.
 *
 * Normally, each asynchronous subscriber that is created has its own
 * message delivery thread. The advantage is that it reduces lock
 * contentions, therefore improving performance.<br>
 * However, if an application creates many subscribers, this is not scaling
 * well since the process would use too many threads.
 *
 * The library has a thread pool that can perform message delivery. If
 * a connection is created with the proper option set
 * (#natsOptions_UseGlobalMessageDelivery), then this thread pool
 * will be responsible for delivering the messages. The thread pool is
 * lazily initialized, that is, no thread is used as long as no subscriber
 * (requiring global message delivery) is created.
 *
 * Each subscriber will be attached to a given worker on the pool so that
 * message delivery order is guaranteed.
 *
 * This call allows you to set the maximum size of the pool.
 *
 * \note At this time, a pool does not shrink, but the caller will not get
 * an error when calling this function with a size smaller than the current
 * size.
 *
 * @see natsOptions_UseGlobalMessageDelivery()
 * @see \ref envVariablesGroup
 *
 * @param max the maximum size of the pool.
 */
NATS_EXTERN natsStatus
nats_SetMessageDeliveryPoolSize(int max);

/** \brief Release thread-local memory possibly allocated by the library.
 *
 * This needs to be called on user-created threads where NATS calls are
 * performed. This does not need to be called in threads created by
 * the library. For instance, do not call this function in the
 * message handler that you specify when creating a subscription.
 *
 * Also, you do not need to call this in an user thread (or the main)
 * if you are calling nats_Close() there.
 */
NATS_EXTERN void
nats_ReleaseThreadMemory(void);

/** \brief Signs a given text using the provided private key.
 *
 * The key is the encoded string representation of the private key, or seed.
 * This is what you get when generating an NKey using NATS tooling.
 *
 * The input is a string, generally the nonce sent by the server when
 * accepting a connection.
 *
 * This function signs the input and returns the signature through the
 * output arguments. This call allocates memory necessary to hold
 * the signature. If this is used as part of the signature callback
 * passed to #natsOptions_SetNKey(), then the memory will be automatically
 * freed by the library after the signature has been inserted in the CONNECT
 * protocol.
 * If this function is used outside of this context, it is the user responsibility
 * to free the allocated memory when no longer needed.
 *
 * @see natsOptions_SetNKey()
 *
 * @param encodedSeed the string encoded private key, also known as seed.
 * @param input the input to be signed.
 * @param signature the memory location of allocated memory containing the signed input.
 * @param signatureLength the size of the allocated signature.
 */
NATS_EXTERN natsStatus
nats_Sign(const char    *encodedSeed,
          const char    *input,
          unsigned char **signature,
          int           *signatureLength);

/** \brief Tear down the library.
 *
 * Releases memory used by the library.
 *
 * For this to take effect, all NATS objects that you have created
 * must first be destroyed.
 *
 * This call does not block and it is possible that the library
 * is not unloaded right away if there are still internal threads referencing
 * it, so calling #nats_Open() right away may fail. If you want to ensure
 * that the library is fully unloaded, call #nats_CloseAndWait() instead.
 *
 * \note There are still a small number of thread local keys and a mutex
 * that are not freed until the application exit (in which case a final cleanup
 * is executed).
 *
 * \warning You must not call #nats_Open and #nats_Close concurrently.
 *
 * @see nats_CloseAndWait()
 */
NATS_EXTERN void
nats_Close(void);

/** \brief Tear down the library and wait for all resources to be released.
 *
 * Similar to #nats_Close() except that this call will make sure that all
 * references to the library are decremented before returning (up to the
 * given timeout). Internal threads (such as subscriptions dispatchers,
 * etc..) hold a reference to the library. Only when all references have
 * been released that this call will return. It means that you must call
 * all the "destroy" calls before calling this function, otherwise it will
 * block forever (or up to given timeout).
 *
 * For instance, this code would "deadlock":
 * \code{.unparsed}
 * natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
 * nats_CloseWait(0);
 * natsConnection_Destroy(nc);
 * \endcode
 * But this would work as expected:
 * \code{.unparsed}
 * natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
 * natsConnection_Destroy(nc);
 * nats_CloseWait(0);
 * \endcode
 * The library and other objects (such as connections, subscriptions, etc)
 * use internal threads. After the destroy call, it is possible or even likely
 * that some threads are still running, holding references to the library.
 * Unlike #nats_Close(), which will simply ensure that the library is ultimately
 * releasing memory, the #nats_CloseAndWait() API will ensure that all those internal
 * threads have unrolled and that the memory used by the library is released before
 * returning.
 *
 * \note If a timeout is specified, the call may return #NATS_TIMEOUT but the
 * library is still being tear down and memory will be released. The error is just
 * to notify you that the operation did not complete in the allotted time. Calling
 * #nats_Open() in this case (or any implicit opening of the library) may result
 * in an error since the library may still be in the process of being closed.
 *
 * \warning Due to the blocking nature it is illegal to call this from any
 * NATS thread (such as message or connection callbacks). If trying to do so,
 * a #NATS_ILLEGAL_STATE error will be returned.
 *
 * @see nats_Close()
 *
 * @param timeout the maximum time to wait for the library to be closed. If
 * negative or 0, waits for as long as needed.
 */
NATS_EXTERN natsStatus
nats_CloseAndWait(int64_t timeout);

/** @} */ // end of libraryGroup

/** \defgroup statusGroup Status
 *
 * Functions related to #natsStatus.
 * @{
 */

/** \brief Get the text corresponding to a #natsStatus.
 *
 * Returns the static string corresponding to the given status.
 *
 * \warning The returned string is a static string, do not attempt to free
 * it.
 *
 * @param s status to get the text representation from.
 */
NATS_EXTERN const char*
natsStatus_GetText(natsStatus s);

/** @} */ // end of statusGroup

/** \defgroup statsGroup Statistics
 *
 *  Statistics Functions.
 *  @{
 */

/** \brief Creates a #natsStatistics object.
 *
 * Creates a statistics object that can be passed to #natsConnection_GetStats().
 *
 * \note The object needs to be destroyed when no longer needed.
 *
 * @see #natsStatistics_Destroy()
 *
 * @param newStats the location where to store the pointer to the newly created
 * #natsStatistics object.
 */
NATS_EXTERN natsStatus
natsStatistics_Create(natsStatistics **newStats);

/** \brief Extracts the various statistics values.
 *
 * Gets the counts out of the statistics object.
 *
 * \note You can pass `NULL` to any of the count your are not interested in
 * getting.
 *
 * @see natsConnection_GetStats()
 *
 * @param stats the pointer to the #natsStatistics object to get the values from.
 * @param inMsgs total number of inbound messages.
 * @param inBytes total size (in bytes) of inbound messages.
 * @param outMsgs total number of outbound messages.
 * @param outBytes total size (in bytes) of outbound messages.
 * @param reconnects total number of times the client has reconnected.
 */
NATS_EXTERN natsStatus
natsStatistics_GetCounts(const natsStatistics *stats,
                         uint64_t *inMsgs, uint64_t *inBytes,
                         uint64_t *outMsgs, uint64_t *outBytes,
                         uint64_t *reconnects);

/** \brief Destroys the #natsStatistics object.
 *
 * Destroys the statistics object, freeing up memory.
 *
 * @param stats the pointer to the #natsStatistics object to destroy.
 */
NATS_EXTERN void
natsStatistics_Destroy(natsStatistics *stats);

/** @} */ // end of statsGroup

/** \defgroup optsGroup Options
 *
 *  NATS Options.
 *  @{
 */

/** \brief Creates a #natsOptions object.
 *
 * Creates a #natsOptions object. This object is used when one wants to set
 * specific options prior to connecting to the `NATS Server`.
 *
 * After making the appropriate natsOptions_Set calls, this object is passed
 * to the #natsConnection_Connect() call, which will clone this object. After
 * natsConnection_Connect() returns, modifications to the options object
 * will not affect the connection.
 *
 * \note The object needs to be destroyed when no longer needed.*
 *
 * @see natsConnection_Connect()
 * @see natsOptions_Destroy()
 *
 * @param newOpts the location where store the pointer to the newly created
 * #natsOptions object.
 */
NATS_EXTERN natsStatus
natsOptions_Create(natsOptions **newOpts);

/** \brief Sets the URL to connect to.
 *
 * Sets the URL of the `NATS Server` the client should try to connect to.
 * The URL can contain optional user name and password.
 *
 * Some valid URLS:
 *
 * - nats://localhost:4222
 * - nats://user\@localhost:4222
 * - nats://user:password\@localhost:4222
 *
 * @see natsOptions_SetServers
 * @see natsOptions_SetUserInfo
 * @see natsOptions_SetToken
 *
 * @param opts the pointer to the #natsOptions object.
 * @param url the string representing the URL the connection should use
 * to connect to the server.
 *
 */
/*
 * The above is for doxygen. The proper syntax for username/password
 * is without the '\' character:
 *
 * nats://localhost:4222
 * nats://user@localhost:4222
 * nats://user:password@localhost:4222
 */
NATS_EXTERN natsStatus
natsOptions_SetURL(natsOptions *opts, const char *url);

/** \brief Set the list of servers to try to (re)connect to.
 *
 * This specifies a list of servers to try to connect (or reconnect) to.
 * Note that if you call #natsOptions_SetURL() too, the actual list will contain
 * the one from #natsOptions_SetURL() and the ones specified in this call.
 *
 * @see natsOptions_SetURL
 * @see natsOptions_SetUserInfo
 * @see natsOptions_SetToken
 *
 * @param opts the pointer to the #natsOptions object.
 * @param servers the array of strings representing the server URLs.
 * @param serversCount the size of the array.
 */
NATS_EXTERN natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount);

/** \brief Sets the user name/password to use when not specified in the URL.
 *
 * Credentials are usually provided through the URL in the form:
 * <c>nats://foo:bar\@localhost:4222</c>.<br>
 * Until now, you could specify URLs in two ways, with #natsOptions_SetServers
 * or #natsConnection_ConnectTo. The client library would connect (or reconnect)
 * only to this given list of URLs, so if any of the server in the list required
 * authentication, you were responsible for providing the appropriate credentials
 * in the URLs.<br>
 * <br>
 * However, with cluster auto-discovery, the client library asynchronously receives
 * URLs of servers in the cluster. These URLs do not contain any embedded credentials.
 * <br>
 * You need to use this function (or #natsOptions_SetToken) to instruct the client
 * library to use those credentials when connecting to a server that requires
 * authentication and for which there is no embedded credentials in the URL.
 *
 * @see natsOptions_SetToken
 * @see natsOptions_SetURL
 * @see natsOptions_SetServers
 *
 * @param opts the pointer to the #natsOptions object.
 * @param user the user name to send to the server during connect.
 * @param password the password to send to the server during connect.
 */
NATS_EXTERN natsStatus
natsOptions_SetUserInfo(natsOptions *opts, const char *user, const char *password);

/** \brief Sets the token to use when not specified in the URL.
 *
 * Tokens are usually provided through the URL in the form:
 * <c>nats://mytoken\@localhost:4222</c>.<br>
 * Until now, you could specify URLs in two ways, with #natsOptions_SetServers
 * or #natsConnection_ConnectTo. The client library would connect (or reconnect)
 * only to this given list of URLs, so if any of the server in the list required
 * authentication, you were responsible for providing the appropriate token
 * in the URLs.<br>
 * <br>
 * However, with cluster auto-discovery, the client library asynchronously receives
 * URLs of servers in the cluster. These URLs do not contain any embedded tokens.
 * <br>
 * You need to use this function (or #natsOptions_SetUserInfo) to instruct the client
 * library to use this token when connecting to a server that requires
 * authentication and for which there is no embedded token in the URL.
 *
 * @see natsOptions_SetUserInfo
 * @see natsOptions_SetURL
 * @see natsOptions_SetServers
 *
 * @param opts the pointer to the #natsOptions object.
 * @param token the token to send to the server during connect.
 */
NATS_EXTERN natsStatus
natsOptions_SetToken(natsOptions *opts, const char *token);

/** \brief Sets the tokenCb to use whenever a token is needed.
 *
 * For use cases where setting a static token through the URL<br>
 * or through #natsOptions_SetToken is not desirable.<br>
 * <br>
 * This function can be used to generate a token whenever the client needs one.<br>
 * Some example of use cases: expiring token, credential rotation, ...
 *
 * @see natsOptions_SetToken
 *
 * @param opts the pointer to the #natsOptions object.
 * @param tokenCb the tokenCb to use to generate a token to the server during connect.
 * @param closure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 */
NATS_EXTERN natsStatus
natsOptions_SetTokenHandler(natsOptions *opts, natsTokenHandler tokenCb,
                            void *closure);

/** \brief Indicate if the servers list should be randomized.
 *
 * If 'noRandomize' is true, then the list of server URLs is used in the order
 * provided by #natsOptions_SetURL() + #natsOptions_SetServers(). Otherwise, the
 * list is formed in a random order.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param noRandomize if `true`, the list will be used as-is.
 */
NATS_EXTERN natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize);

/** \brief Sets the (re)connect process timeout.
 *
 * This timeout, expressed in milliseconds, is used to interrupt a (re)connect
 * attempt to a `NATS Server`. This timeout is used both for the low level TCP
 * connect call, and for timing out the response from the server to the client's
 * initial `PING` protocol.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param timeout the time, in milliseconds, allowed for an individual connect
 * (or reconnect) to complete.
 *
 */
NATS_EXTERN natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout);

/** \brief Sets the name.
 *
 * This name is sent as part of the `CONNECT` protocol. There is no default name.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param name the name to set.
 */
NATS_EXTERN natsStatus
natsOptions_SetName(natsOptions *opts, const char *name);

/** \brief Sets the secure mode.
 *
 * Indicates to the server if the client wants a secure (SSL/TLS) connection.
 *
 * The default is `false`.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param secure `true` for a secure connection, `false` otherwise.
 */
NATS_EXTERN natsStatus
natsOptions_SetSecure(natsOptions *opts, bool secure);

/** \brief Loads the trusted CA certificates from a file.
 *
 * Loads the trusted CA certificates from a file.
 *
 * Note that the certificates are added to a SSL context for this #natsOptions
 * object at the time of this call, so possible errors while loading the
 * certificates will be reported now instead of when a connection is created.
 * You can get extra information by calling #nats_GetLastError.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param fileName the file containing the CA certificates.
 *
 */
NATS_EXTERN natsStatus
natsOptions_LoadCATrustedCertificates(natsOptions *opts, const char *fileName);

/** \brief Sets the trusted CA certificates from memory.
 *
 * Similar to #natsOptions_LoadCATrustedCertificates expect that instead
 * of loading from file, this loads from the given memory location.
 *
 * If more than one certificate need to be provided, they need to be
 * concatenated. For instance:
 *
 * \code{.unparsed}
 * const char *certs =
 *    "-----BEGIN CERTIFICATE-----\n"
 *    "MIIGjzCCBHegAwIBAgIJAKT2W9SKY7o4MA0GCSqGSIb3DQEBCwUAMIGLMQswCQYD\n"
 *    (...)
 *    "-----END CERTIFICATE-----\n"
 *    "-----BEGIN CERTIFICATE-----\n"
 *    "MIIXyz...\n"
 *    (...)
 *    "-----END CERTIFICATE-----\n"
 * \endcode
 *
 * @see natsOptions_LoadCATrustedCertificates
 *
 * @param opts the pointer to the #natsOptions object.
 * @param certificates the string containing the concatenated CA certificates.
 */
NATS_EXTERN natsStatus
natsOptions_SetCATrustedCertificates(natsOptions *opts, const char *certificates);

/** \brief Loads the certificate chain from a file, using the given key.
 *
 * The certificates must be in PEM format and must be sorted starting with
 * the subject's certificate, followed by intermediate CA certificates if
 * applicable, and ending at the highest level (root) CA.
 *
 * The private key file format supported is also PEM.
 *
 * See #natsOptions_LoadCATrustedCertificates regarding error reports.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param certsFileName the file containing the client certificates.
 * @param keyFileName the file containing the client private key.
 */
NATS_EXTERN natsStatus
natsOptions_LoadCertificatesChain(natsOptions *opts,
                                  const char *certsFileName,
                                  const char *keyFileName);

/** \brief Sets the client certificate and key.
 *
 * Similar to #natsOptions_LoadCertificatesChain expect that instead
 * of loading from file, this loads from the given memory locations.
 *
 * @see natsOptions_LoadCertificatesChain()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param cert the memory location containing the client certificates.
 * @param key the memory location containing the client private key.
 */
NATS_EXTERN natsStatus
natsOptions_SetCertificatesChain(natsOptions *opts,
                                 const char *cert,
                                 const char *key);

/** \brief Sets the list of available ciphers.
 *
 * \note This function does not impact TLSv1.3 ciphersuites.
 *
 * Sets the list of available ciphers.
 * Check https://www.openssl.org/docs/man1.1.1/man1/ciphers.html for the
 * proper syntax. Here is an example:
 *
 * > "-ALL:HIGH"
 *
 * See #natsOptions_LoadCATrustedCertificates regarding error reports.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param ciphers the ciphers suite.
 */
NATS_EXTERN natsStatus
natsOptions_SetCiphers(natsOptions *opts, const char *ciphers);

/** \brief Sets the list of available ciphers for TLSv1.3.
 *
 * Sets the list of available ciphers.
 * Check https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_ciphersuites.html for the
 * proper syntax. Here is an example:
 *
 * > "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
 *
 * See #natsOptions_LoadCATrustedCertificates regarding error reports.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param ciphers the ciphers suite.
 */
NATS_EXTERN natsStatus
natsOptions_SetCipherSuites(natsOptions *opts, const char *ciphers);

/** \brief Sets the server certificate's expected hostname.
 *
 * If set, the library will check that the hostname in the server
 * certificate matches the given `hostname`. This will occur when a connection
 * is created, not at the time of this call.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param hostname the expected server certificate hostname.
 */
NATS_EXTERN natsStatus
natsOptions_SetExpectedHostname(natsOptions *opts, const char *hostname);

/** \brief Switch server certificate verification.
 *
 * By default, the server certificate is verified. You can disable the verification
 * by passing <c>true</c> to this function.
 *
 * \warning This is fine for tests but use with caution since this is not secure.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param skip set it to <c>true</c> to disable - or skip - server certificate verification.
 */
NATS_EXTERN natsStatus
natsOptions_SkipServerVerification(natsOptions *opts, bool skip);

/** \brief Sets the verbose mode.
 *
 * Sets the verbose mode. If `true`, sends are echoed by the server with
 * an `OK` protocol message.
 *
 * The default is `false`.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param verbose `true` for a verbose protocol, `false` otherwise.
 */
NATS_EXTERN natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose);

/** \brief Sets the pedantic mode.
 *
 * Sets the pedantic mode. If `true` some extra checks will be performed
 * by the server.
 *
 * The default is `false`
 *
 * @param opts the pointer to the #natsOptions object.
 * @param pedantic `true` for a pedantic protocol, `false` otherwise.
 */
NATS_EXTERN natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic);

/** \brief Sets the ping interval.
 *
 * Interval, expressed in milliseconds, in which the client sends `PING`
 * protocols to the `NATS Server`.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param interval the interval, in milliseconds, at which the connection
 * will send `PING` protocols to the server.
 */
NATS_EXTERN natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval);

/** \brief Sets the limit of outstanding `PING`s without corresponding `PONG`s.
 *
 * Specifies the maximum number of `PING`s without corresponding `PONG`s (which
 * should be received from the server) before closing the connection with
 * the #NATS_STALE_CONNECTION status. If reconnection is allowed, the client
 * library will try to reconnect.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param maxPingsOut the maximum number of `PING`s without `PONG`s
 * (positive number).
 */
NATS_EXTERN natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPingsOut);

/** \brief Sets the size of the internal read/write buffers.
 *
 * Sets the size, in bytes, of the internal read/write buffers used for
 * reading/writing data from a socket.
 * If not specified, or the value is 0, the library will use a default value,
 * currently set to 32KB.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param ioBufSize the size, in bytes, of the internal buffer for read/write
 * operations.
 */
NATS_EXTERN natsStatus
natsOptions_SetIOBufSize(natsOptions *opts, int ioBufSize);

/** \brief Indicates if the connection will be allowed to reconnect.
 *
 * Specifies whether or not the client library should try to reconnect when
 * losing the connection to the `NATS Server`.
 *
 * The default is `true`.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param allow `true` if the connection is allowed to reconnect, `false`
 * otherwise.
 */
NATS_EXTERN natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow);

/** \brief Sets the maximum number of reconnect attempts.
 *
 * Specifies the maximum number of reconnect attempts.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param maxReconnect the maximum number of reconnects (positive number).
 */
NATS_EXTERN natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect);

/** \brief Sets the time between reconnect attempts.
 *
 * Specifies how long to wait between two reconnect attempts from the same
 * server. This means that if you have a list with S1,S2 and are currently
 * connected to S1, and get disconnected, the library will immediately
 * attempt to connect to S2. If this fails, it will go back to S1, and this
 * time will wait for `reconnectWait` milliseconds since the last attempt
 * to connect to S1.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param reconnectWait the time, in milliseconds, to wait between attempts
 * to reconnect to the same server.
 */
NATS_EXTERN natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait);

/** \brief Set the upper bound of a random delay added to reconnect wait.
 *
 * After a disconnect, the library will try to reconnect to any server URLs
 * in its list (the URLs are either provided by the user or discovered through
 * gossip protocol).
 *
 * After the library failed to reconnect to every server in the list, it
 * will wait for `reconnectWait` as specified with #natsOptions_SetReconnectWait().
 * This option adds some random jitter to the reconnect wait delay.
 *
 * This will help minimize the thundering herd phenomenon. For instance, suppose
 * a server has 1000 connections, all were created at different times, but
 * have the same reconnect wait option. If this server suddenly stops, then all
 * connections will detect the failure and initiate a reconnect at the same time.
 * The issue is even greater when those connections are TLS because of the added
 * cost of the TLS handshake.
 *
 * @see natsOptions_SetReconnectWait()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param jitter the jitter in milliseconds for non TLS connections.
 * @param jitterTLS the jitter in milliseconds for TLS connections.
 */
NATS_EXTERN natsStatus
natsOptions_SetReconnectJitter(natsOptions *opts, int64_t jitter, int64_t jitterTLS);

/** \brief Sets the handler to invoke when the library needs to wait before the next reconnect attempts.
 *
 * This callback is invoked after the library tried every URL in the server list
 * and failed to reconnect. It passes to the user the current number of attempts.
 * This function shall return the amount of time the library will sleep before attempting
 * to reconnect again.
 *
 * It is strongly recommended that this value contains some jitter to prevent all
 * connections to attempt reconnecting at the same time.
 *
 * \note When using this approach, the reconnect wait as specified by #natsOptions_SetReconnectWait()
 * is ignored.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param cb the custom reconnect delay handler to invoke.
 * @param closure a pointer to an user defined object (can be `NULL`). See
 * the #natsCustomReconnectDelayHandler prototype.
 */
NATS_EXTERN natsStatus
natsOptions_SetCustomReconnectDelay(natsOptions *opts,
                                    natsCustomReconnectDelayHandler cb,
                                    void *closure);

/** \brief Sets the size of the backing buffer used during reconnect.
 *
 * Sets the size, in bytes, of the backing buffer holding published data
 * while the library is reconnecting. Once this buffer has been exhausted,
 * publish operations will return the #NATS_INSUFFICIENT_BUFFER error.
 * If not specified, or the value is 0, the library will use a default value,
 * currently set to 8MB.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param reconnectBufSize the size, in bytes, of the backing buffer for
 * write operations during a reconnect.
 */
NATS_EXTERN natsStatus
natsOptions_SetReconnectBufSize(natsOptions *opts, int reconnectBufSize);

/** \brief Sets the maximum number of pending messages per subscription.
 *
 * Specifies the maximum number of inbound messages that can be buffered in the
 * library, for each subscription, before inbound messages are dropped and
 * #NATS_SLOW_CONSUMER status is reported to the #natsErrHandler callback (if
 * one has been set).
 *
 * @see natsOptions_SetErrorHandler()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param maxPending the number of messages allowed to be buffered by the
 * library before triggering a slow consumer scenario.
 */
NATS_EXTERN natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending);

/** \brief Sets the error handler for asynchronous events.
 *
 * Specifies the callback to invoke when an asynchronous error
 * occurs. This is used by applications having only asynchronous
 * subscriptions that would not know otherwise that a problem with the
 * connection occurred.
 *
 * @see natsErrHandler
 *
 * @param opts the pointer to the #natsOptions object.
 * @param errHandler the error handler callback.
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure);

/** \brief Sets the callback to be invoked when a connection to a server
 *         is permanently lost.
 *
 * Specifies the callback to invoke when a connection is terminally closed,
 * that is, after all reconnect attempts have failed (when reconnection is
 * allowed).
 *
 * @param opts the pointer to the #natsOptions object.
 * @param closedCb the callback to be invoked when the connection is closed.
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure);

/** \brief Sets the callback to be invoked when the connection to a server is
 *         lost.
 *
 * Specifies the callback to invoke when a connection to the `NATS Server`
 * is lost.
 *
 * \warning Invocation of this callback is asynchronous, which means that
 * the state of the connection may have changed when this callback is
 * invoked.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param disconnectedCb the callback to be invoked when a connection to
 * a server is lost
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure);

/** \brief Sets the callback to be invoked when the connection has reconnected.
 *
 * Specifies the callback to invoke when the client library has successfully
 * reconnected to a `NATS Server`.
 *
 * \warning Invocation of this callback is asynchronous, which means that
 * the state of the connection may have changed when this callback is
 * invoked.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param reconnectedCb the callback to be invoked when the connection to
 * a server has been re-established.
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure);

/** \brief Sets the callback to be invoked when new servers are discovered.
 *
 * Specifies the callback to invoke when the client library has been notified
 * of one or more new `NATS Servers`.
 *
 * \warning Invocation of this callback is asynchronous, which means that
 * the state may have changed when this callback is invoked.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param discoveredServersCb the callback to be invoked when new servers
 * have been discovered.
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetDiscoveredServersCB(natsOptions *opts,
                                   natsConnectionHandler discoveredServersCb,
                                   void *closure);

/** \brief Sets if the library should ignore or not discovered servers.
 *
 * By default, when a server joins a cluster, a client is notified
 * of the new URL and added to the list so it can be used in case
 * of a reconnect.
 *
 * The servers can be configured to disable this gossip, however, if
 * not done at the servers level, this option allows the discovered
 * servers to be ignored.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param ignore if discovered server should be ignored or not.
 */
NATS_EXTERN natsStatus
natsOptions_SetIgnoreDiscoveredServers(natsOptions *opts, bool ignore);

/** \brief Sets the callback to be invoked when server enters lame duck mode.
 *
 * Specifies the callback to invoke when the server notifies
 * the connection that it entered lame duck mode, that is, going to
 * gradually disconnect all its connections before shuting down. This is
 * often used in deployments when upgrading NATS Servers.
 *
 * \warning Invocation of this callback is asynchronous, which means that
 * the state may have changed when this callback is invoked.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param lameDuckCb the callback to be invoked when server enters
 * lame duck mode.
 * @param closure a pointer to an user object that will be passed to
 * the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetLameDuckModeCB(natsOptions *opts,
                              natsConnectionHandler lameDuckCb,
                              void *closure);

/** \brief Sets the external event loop and associated callbacks.
 *
 * If you want to use an external event loop, the `NATS` library will not
 * create a thread to read data from the socket, and will not directly write
 * data to the socket. Instead, the library will invoke those callbacks
 * for various events.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param loop the `void*` pointer to the external event loop.
 * @param attachCb the callback invoked after the connection is connected,
 * or reconnected.
 * @param readCb the callback invoked when the event library should start or
 * stop polling for read events.
 * @param writeCb the callback invoked when the event library should start or
 * stop polling for write events.
 * @param detachCb the callback invoked when a connection is closed.
 */
NATS_EXTERN natsStatus
natsOptions_SetEventLoop(natsOptions *opts,
                         void *loop,
                         natsEvLoop_Attach          attachCb,
                         natsEvLoop_ReadAddRemove   readCb,
                         natsEvLoop_WriteAddRemove  writeCb,
                         natsEvLoop_Detach          detachCb);

/** \brief Switch on/off the use of a central message delivery thread pool.
 *
 * Normally, each asynchronous subscriber that is created has its own
 * message delivery thread. The advantage is that it reduces lock
 * contentions, therefore improving performance.<br>
 * However, if an application creates many subscribers, this is not scaling
 * well since the process would use too many threads.
 *
 * When a connection is created from a `nats_Options` that has enabled
 * global message delivery, asynchronous subscribers from this connection
 * will use a shared thread pool responsible for message delivery.
 *
 * \note The message order per subscription is still guaranteed.
 *
 * @see nats_SetMessageDeliveryPoolSize()
 * @see \ref envVariablesGroup
 *
 * @param opts the pointer to the #natsOptions object.
 * @param global if `true`, uses the global message delivery thread pool,
 * otherwise, each asynchronous subscriber will create their own message
 * delivery thread.
 */
NATS_EXTERN natsStatus
natsOptions_UseGlobalMessageDelivery(natsOptions *opts, bool global);

/** \brief Dictates the order in which host name are resolved during connect.
 *
 * The library would previously favor IPv6 addresses during the connect process.
 * <br>
 * You can now change the order, or even exclude a family of addresses, using
 * this option. Here is the list of possible values:
 * <br>
 * Value | Meaning
 * ------|--------
 * 46 | try IPv4 first, if it fails try IPv6
 * 64 | try IPv6 first, if it fails try IPv4
 * 4 | use only IPv4
 * 6 | use only IPv6
 * 0 | any family, no specific order
 *
 * \note If this option is not set, or you specify `0` for the order, the
 * library will use the first IP (based on the DNS configuration) for which
 * a successful connection can be made.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param order a string representing the order for the IP resolution.
 */
NATS_EXTERN natsStatus
natsOptions_IPResolutionOrder(natsOptions *opts, int order);

/** \brief Sets if Publish calls should send data right away.
 *
 * For throughput performance, the client library tries by default to buffer
 * as much data as possible before sending it over TCP.
 *
 * Setting this option to `true` will make Publish calls send the
 * data right away, reducing latency, but also throughput.
 *
 * A good use-case would be a connection used to solely send replies.
 * Imagine, a requestor sending a request, waiting for the reply before
 * sending the next request.<br>
 * The replier application will send only one reply at a time (since
 * it will not receive the next request until the requestor receives
 * the reply).<br>
 * In such case, it makes sense for the reply to be sent right away.
 *
 * The alternative would be to call #natsConnection_Flush(),
 * but this call requires a round-trip with the server, which is less
 * efficient than using this option.
 *
 * Note that the Request() call already automatically sends the request
 * as fast as possible, there is no need to set an option for that.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param sendAsap a boolean indicating if published data should be
 * sent right away or be buffered.
 */
NATS_EXTERN natsStatus
natsOptions_SetSendAsap(natsOptions *opts, bool sendAsap);

/** \brief Switches the use of old style requests.
 *
 * Setting `useOldStyle` to `true` forces the request calls to use the original
 * behavior, which is to create a new inbox, a new subscription on that inbox
 * and set auto-unsubscribe to 1.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param useOldStyle a boolean indicating if old request style should be used.
 */
NATS_EXTERN natsStatus
natsOptions_UseOldRequestStyle(natsOptions *opts, bool useOldStyle);

/** \brief Fails pending requests on disconnect event.
 *
 * If this option is enabled, all pending #natsConnection_Request() family
 * calls will fail with the #NATS_CONNECTION_DISCONNECTED status.
 *
 * \note This does not apply to requests from connections that use the
 * old style requests.
 *
 * @see natsOptions_UseOldRequestStyle
 *
 * @param opts the pointer to the #natsOptions object.
 * @param failRequests a boolean indicating if pending requests should fail
 * when a disconnect event occurs.
 */
NATS_EXTERN natsStatus
natsOptions_SetFailRequestsOnDisconnect(natsOptions *opts, bool failRequests);

/** \brief Sets if connection receives its own messages.
 *
 * This configures whether the server will echo back messages
 * that are sent on this connection if there is also matching subscriptions.
 *
 * Set this to `true` to prevent the server from sending back messages
 * produced by this connection. The default is false, that is, messages
 * originating from this connection will be sent by the server if the
 * connection has matching subscriptions.
 *
 * \note This is supported on servers >= version 1.2.0. Calling
 * #natsConnection_Connect() with the option set to `true` to server below
 * this version will return the `NATS_NO_SERVER_SUPPORT` error.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param noEcho a boolean indicating if sent messages can be delivered back
 * to this connection or not.
 */
NATS_EXTERN natsStatus
natsOptions_SetNoEcho(natsOptions *opts, bool noEcho);

/** \brief Indicates if initial connect failure should be retried or not.
 *
 * By default, #natsConnection_Connect() attempts to connect to a server
 * specified in provided list of servers. If it cannot connect and the list has been
 * fully tried, the function returns an error.
 *
 * This option is used to changed this default behavior.
 *
 * If `retry` is set to `true` and connection cannot be established right away, the
 * library will attempt to connect based on the reconnect attempts
 * and delay settings.
 *
 * \note The connect retry logic uses reconnect settings even if #natsOptions_SetAllowReconnect()
 * has been set to false. In other words, a failed connect may be retried even though
 * a reconnect will not be allowed should the connection to the server be lost
 * after initial connect.
 *
 * The behavior will then depend on the value of the `connectedCb` parameter:
 *
 * * If `NULL`, then the call blocks until it can connect
 * or exhausts the reconnect attempts.
 *
 * * If not `NULL`, and no connection can be immediately
 * established, the #natsConnection_Connect() calls returns #NATS_NOT_YET_CONNECTED
 * to indicate that no connection is currently established, but will
 * try asynchronously to connect using the reconnect attempts/delay settings. If
 * the connection is later established, the specified callback will be
 * invoked. If no connection can be made and the retry attempts are exhausted,
 * the callback registered with #natsOptions_SetClosedCB(), if any, will be
 * invoked.
 *
 * \note If #natsConnection_Connect() returns `NATS_OK` (that is, a connection to
 * a `NATS Server` was established in that call), then the `connectedCb` callback
 * will not be invoked.
 *
 * If `retry` is set to false, #natsConnection_Connect() behaves as originally
 * designed, that is, returns an error and no connection object if failing to connect
 * to any server in the list.
 *
 * \note The `connectedCb` parameter is ignored and set to `NULL` in the options object
 * when `retry` is set to `false`.
 *
 * @see natsOptions_SetMaxReconnect()
 * @see natsOptions_SetReconnectWait()
 * @see natsOptions_SetClosedCB()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param retry a boolean indicating if a failed connect should be retried.
 * @param connectedCb if `retry` is true and this is not `NULL`, then the
 * connect may be asynchronous and this callback will be invoked if the connect
 * succeeds.
 * @param closure a pointer to an user object that will be passed to the callback. `closure` can be `NULL`.
 */
NATS_EXTERN natsStatus
natsOptions_SetRetryOnFailedConnect(natsOptions *opts, bool retry,
        natsConnectionHandler connectedCb, void* closure);

/** \brief Sets the callbacks to fetch user JWT and sign server's nonce.
 *
 * Any time the library creates a TCP connection to the server, the server
 * in response sends an `INFO` protocol. That `INFO` protocol, for NATS Server
 * at v2.0.0+, may include a `nonce` for the client to sign.
 *
 * If this option is set, the library will invoke the two handlers to fetch
 * the user JWT and sign the server's nonce.
 *
 * This is an option that will be used only by users that are able to
 * sign using Ed25519 (public-key signature system). Most users will probably
 * prefer the user of #natsOptions_SetUserCredentialsFromFiles().
 *
 * \note natsOptions_SetUserCredentialsCallbacks() and natsOptions_SetNKey()
 * are mutually exclusive. Calling this function will remove the NKey and
 * replace the signature handler, that was set with natsOptions_SetNKey(),
 * with this one.
 *
 * @see natsUserJWTHandler
 * @see natsSignatureHandler
 * @see natsOptions_SetUserCredentialsFromFiles()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param ujwtCB the callback to invoke to fetch the user JWT.
 * @param ujwtClosure the closure that will be passed to the `ujwtCB` callback.
 * @param sigCB the callback to invoke to sign the server nonce.
 * @param sigClosure the closure that will be passed to the `sigCB` callback.
 */
NATS_EXTERN natsStatus
natsOptions_SetUserCredentialsCallbacks(natsOptions *opts,
                                        natsUserJWTHandler      ujwtCB,
                                        void                    *ujwtClosure,
                                        natsSignatureHandler    sigCB,
                                        void                    *sigClosure);

/** \brief Sets the file(s) to use to fetch user JWT and seed required to sign nonce.
 *
 * This is a convenient option that specifies the files(s) to use to fetch
 * the user JWT and the user seed to be used to sign the server's nonce.
 *
 * The `userOrChainedFile` contains the user JWT token and possibly the user
 * NKey seed. Note the format of this file:
 *
 * \code{.unparsed}
 * -----BEGIN NATS USER JWT-----
 * ...an user JWT token...
 * ------END NATS USER JWT------
 *
 * ************************* IMPORTANT *************************
 * NKEY Seed printed below can be used to sign and prove identity.
 * NKEYs are sensitive and should be treated as secrets.
 *
 * -----BEGIN USER NKEY SEED-----
 * SU...
 * ------END USER NKEY SEED------
 * \endcode
 *
 * The `---BEGIN NATS USER JWT---` header is used to detect where the user
 * JWT is in this file.
 *
 * If the file does not contain the user NKey seed, then the `seedFile` file
 * name must be specified and must contain the user NKey seed.
 *
 * \note natsOptions_SetUserCredentialsFromFiles() and natsOptions_SetNKey()
 * are mutually exclusive. Calling this function will remove the NKey and
 * replace the signature handler, that was set with natsOptions_SetNKey(),
 * with an internal one that will handle the signature.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param userOrChainedFile the name of the file containing the user JWT and
 * possibly the user NKey seed.
 * @param seedFile the name of the file containing the user NKey seed.
 */
NATS_EXTERN natsStatus
natsOptions_SetUserCredentialsFromFiles(natsOptions *opts,
                                        const char *userOrChainedFile,
                                        const char *seedFile);

/** \brief Sets JWT handler and handler to sign nonce that uses seed.
 *
 * This function acts similarly to natsOptions_SetUserCredentialsFromFiles() but reads from memory instead
 * from a file. Also it assumes that `jwtAndSeedContent` contains both the JWT and NKey seed.
 *
 * As for the format, see natsOptions_SetUserCredentialsFromFiles() documentation.
 *
 * @see natsOptions_SetUserCredentialsFromFiles()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param jwtAndSeedContent string containing user JWT and user NKey seed.
 */
NATS_EXTERN natsStatus
natsOptions_SetUserCredentialsFromMemory(natsOptions *opts,
                                         const char *jwtAndSeedContent);

/** \brief Sets the NKey public key and signature callback.
 *
 * Any time the library creates a TCP connection to the server, the server
 * in response sends an `INFO` protocol. That `INFO` protocol, for NATS Server
 * at v2.0.0+, may include a `nonce` for the client to sign.
 *
 * If this option is set, the library will add the NKey public key `pubKey`
 * to the `CONNECT` protocol along with the server's nonce signature resulting
 * from the invocation of the signature handler `sigCB`.
 *
 * \note natsOptions_SetNKey() and natsOptions_SetUserCredentialsCallbacks()
 * or natsOptions_SetUserCredentialsFromFiles() are mutually exclusive.
 * Calling this function will remove the user JWT callback and replace the
 * signature handler, that was set with one of the user credentials options,
 * with this one.
 *
 * @see natsSignatureHandler
 *
 * @param opts the pointer to the #natsOptions object.
 * @param pubKey the user NKey public key.
 * @param sigCB the callback to invoke to sign the server nonce.
 * @param sigClosure the closure that will be passed to the `sigCB` callback.
 */
NATS_EXTERN natsStatus
natsOptions_SetNKey(natsOptions             *opts,
                    const char              *pubKey,
                    natsSignatureHandler    sigCB,
                    void                    *sigClosure);

/** \brief Sets the NKey public key and its seed file.
 *
 * Any time the library creates a TCP connection to the server, the server
 * in response sends an `INFO` protocol. That `INFO` protocol, for NATS Server
 * at v2.0.0+, may include a `nonce` for the client to sign.
 *
 * If this option is set, the library will add the NKey public key `pubKey`
 * to the `CONNECT` protocol along with the server's nonce signature signed
 * using the private key from the provided seed file. The library takes care
 * of clearing the memory holding the private key read from the file as soon
 * as it is no longer needed.
 *
 * \note natsOptions_SetNKeyFromSeed() and natsOptions_SetUserCredentialsCallbacks()
 * or natsOptions_SetUserCredentialsFromFiles() are mutually exclusive.
 * Calling this function will remove the user JWT callback and replace the
 * signature handler, that was set with one of the user credentials options,
 * with this one.
 *
 * @see natsSignatureHandler
 *
 * @param opts the pointer to the #natsOptions object.
 * @param pubKey the user NKey public key.
 * @param seedFile the name of the file containing the user NKey seed.
 */
NATS_EXTERN natsStatus
natsOptions_SetNKeyFromSeed(natsOptions *opts,
                            const char  *pubKey,
                            const char  *seedFile);

/** \brief Sets the write deadline.
 *
 * If this is set, the socket is set to non-blocking mode and
 * write will have a deadline set. If the deadline is reached,
 * the write call will return an error which will translate
 * to publish calls, or any library call trying to send data
 * to the server, to possibly fail.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param deadline the write deadline expressed in milliseconds.
 * If set to 0, it means that there is no deadline and socket
 * is in blocking mode.
 */
NATS_EXTERN natsStatus
natsOptions_SetWriteDeadline(natsOptions *opts, int64_t deadline);

/** \brief Enable/Disable the "no responders" feature.
 *
 * By default, when a connection to a NATS Server v2.2.0+ is made,
 * the library signals to the server that it supports the "no responders"
 * feature, which means that if a request is made, and there are
 * no subscriptions on the request subject (no responders), then
 * the server sends back an empty message with the header "Status"
 * and value "503". The request APIs capture this message and
 * instead return a #NATS_NO_RESPONDERS status, instead of waiting
 * for the timeout to occur and return #NATS_TIMEOUT.
 *
 * In case where users set up their own asynchronous subscription
 * on the reply subject and publish the request with #natsConnection_PublishRequest
 * and the like, then the message callback may be invoked with this
 * "no responders" message, which can be checked with #natsMsg_IsNoResponders.
 *
 * However, if users don't want to have to deal with that, it is
 * possible to instruct the server to disable this feature for
 * a given connection. If that is the case, requests will behave
 * as with pre-v2.2.0 servers, in that the request will timeout
 * when there are no responders.
 *
 * \note This function is to disable the feature that is normally
 * enabled by default. Passing `false` means that it would enable
 * it again if you had previously disable the option. However, the
 * feature may still be disabled when connecting to a server that
 * does not support it.
 *
 * @see natsMsg_IsNoResponders()
 *
 * @param opts the pointer to the #natsOptions object.
 * @param disabled the boolean to indicate if the feature should be
 * disabled or not.
 */
NATS_EXTERN natsStatus
natsOptions_DisableNoResponders(natsOptions *opts, bool disabled);

/** \brief Sets a custom inbox prefix
 *
 * The default inbox prefix is "_INBOX", but you can change it
 * using this option. This can be useful when setting permissions
 * and/or with import/exports across different accounts.
 *
 * The prefix must be a valid subject and not contain any of the
 * wildcards tokens `*` nor `>`.
 *
 * To clear the custom inbox prefix, call this function with `NULL`
 * or the empty string.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param inboxPrefix the desired inbox prefix.
 */
NATS_EXTERN natsStatus
natsOptions_SetCustomInboxPrefix(natsOptions *opts, const char *inboxPrefix);

/** \brief Sets a custom padding when allocating buffer for incoming messages
 *
 * By default library allocates natsMsg with payload buffer size
 * equal to payload size. Sometimes it can be useful to add some
 * padding to the end of the buffer which can be tweaked using
 * this option.
 *
 * To clear the custom message buffer padding, call this function with 0.
 * Changing this option has no effect on existing NATS connections.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param paddingSize the desired inbox prefix.
 */
NATS_EXTERN natsStatus
natsOptions_SetMessageBufferPadding(natsOptions *opts, int paddingSize);

/** \brief Destroys a #natsOptions object.
 *
 * Destroys the natsOptions object, freeing used memory. See the note in
 * the natsOptions_Create() call.
 *
 * @param opts the pointer to the #natsOptions object to destroy.
 */
NATS_EXTERN void
natsOptions_Destroy(natsOptions *opts);

/** @} */ // end of optsGroup

#if defined(NATS_HAS_STREAMING)
/** \defgroup stanConnOptsGroup Streaming Connection Options
 *
 *   NATS Streaming Connection Options.
 *   @{
 */

/** \brief Creates a #stanConnOptions object.
 *
 * Creates a #stanConnOptions object. This object is used when one wants to set
 * specific options prior to connecting to the `NATS Streaning Server`.
 *
 * After making the appropriate `stanConnOptions_SetXXX()` calls, this object is passed
 * to the #stanConnection_Connect() call, which will clone this object. After
 * #stanConnection_Connect() returns, modifications to the options object
 * will not affect the connection.
 *
 * The default options set in this call are:
 * url: `nats://localhost:4222`
 * connection wait: 2 seconds
 * publish ack wait: 30 seconds
 * discovery prefix: `_STAN.discovery`
 * maximum publish acks inflight and percentage: 16384, 50%
 * ping interval: 5 seconds
 * max ping out without response: 3
 *
 * \note The object needs to be destroyed when no longer needed.
 *
 * @see stanConnection_Connect()
 * @see stanConnOptions_Destroy()
 *
 * @param newOpts the location where store the pointer to the newly created
 * #stanConnOptions object.
 */
NATS_EXTERN natsStatus
stanConnOptions_Create(stanConnOptions **newOpts);

/** \brief Sets the URL to connect to.
 *
 * Sets the URL of the `NATS Streaming Server` the client should try to connect to.
 * The URL can contain optional user name and password. You can provide a comma
 * separated list of URLs too.
 *
 * Some valid URLS:
 *
 * - nats://localhost:4222
 * - nats://user\@localhost:4222
 * - nats://user:password\@localhost:4222
 * - nats://host1:4222,nats://host2:4222,nats://host3:4222
 *
 * \note This option takes precedence over #natsOptions_SetURL when NATS options
 * are passed with #stanConnOptions_SetNATSOptions.
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param url the string representing the URL the connection should use
 * to connect to the server.
 *
 */
NATS_EXTERN natsStatus
stanConnOptions_SetURL(stanConnOptions *opts, const char *url);

/** \brief Sets the NATS Options to use to create the connection.
 *
 * The Streaming client connects to the NATS Streaming Server through
 * a regular NATS Connection (#natsConnection). To configure this connection
 * create a #natsOptions and configure it as needed, then call this function.
 *
 * This function clones the passed options, so after this call, any
 * changes to the given #natsOptions will not affect the #stanConnOptions.
 *
 * \note If both #natsOptions_SetURL and #stanConnOptions_SetURL are used
 * the URL(s) set in the later take precedence.
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param nOpts the pointer to the #natsOptions object to use to create
 * the connection to the server.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetNATSOptions(stanConnOptions *opts, natsOptions *nOpts);

/** \brief Sets the timeout for establishing a connection.
 *
 * Value expressed in milliseconds.
 *
 * Default is 2000 milliseconds (2 seconds).
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param wait how long to wait for a response from the streaming server.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetConnectionWait(stanConnOptions *opts, int64_t wait);

/** \brief Sets the timeout for waiting for an ACK for a published message.
 *
 * Value expressed in milliseconds.
 *
 * Default is 30000 milliseconds (30 seconds).
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param wait how long to wait for a response from the streaming server.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetPubAckWait(stanConnOptions *opts, int64_t wait);

/** \brief Sets the subject prefix the library sends the connect request to.
 *
 * Default is `_STAN.discovery`
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param prefix the subject prefix the library sends the connect request to.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetDiscoveryPrefix(stanConnOptions *opts, const char *prefix);


/** \brief Sets the maximum number of published messages without outstanding ACKs from the server.
 *
 * A connection will block #stanConnection_Publish() or #stanConnection_PublishAsync calls
 * if the number of outstanding published messages has reached this number.
 *
 * When the connection receives ACKs, the number of outstanding messages will decrease.
 * If the number falls between `maxPubAcksInflight * percentage`, then the blocked publish
 * calls will be released.
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param maxPubAcksInflight the maximum number of published messages without ACKs from the server.
 * @param percentage the percentage (expressed as a float between ]0.0 and 1.0]) of the maxPubAcksInflight
 * number below which a blocked publish call is released.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetMaxPubAcksInflight(stanConnOptions *opts, int maxPubAcksInflight, float percentage);

/** \brief Sets the ping interval and max out values.
 *
 * Value expressed in number of seconds.
 *
 * Default is 5 seconds and 3 missed PONGs.
 *
 * The interval needs to be at least 1 and represents the number of seconds.
 * The maxOut needs to be at least 2, since the count of sent PINGs increase
 * whenever a PING is sent and reset to 0 when a response is received.
 * Setting to 1 would cause the library to close the connection right away.
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param interval the number of seconds between each PING.
 * @param maxOut the maximum number of PINGs without receiving a PONG back.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetPings(stanConnOptions *opts, int interval, int maxOut);

/** \brief Sets the connection lost handler.
 *
 * This callback will be invoked should the client permanently lose
 * contact with the server (or another client replaces it while being
 * disconnected). The callback will not be invoked on normal #stanConnection_Close().
 *
 * @param opts the pointer to the #stanConnOptions object.
 * @param handler the handler to be invoked when the connection to the streaming server is lost.
 * @param closure the closure the library will pass to the callback.
 */
NATS_EXTERN natsStatus
stanConnOptions_SetConnectionLostHandler(stanConnOptions *opts, stanConnectionLostHandler handler, void *closure);

/** \brief Destroys a #stanConnOptions object.
 *
 * Destroys the #stanConnOptions object, freeing used memory. See the note in
 * the #stanConnOptions_Create() call.
 *
 * @param opts the pointer to the #stanConnOptions object to destroy.
 */
NATS_EXTERN void
stanConnOptions_Destroy(stanConnOptions *opts);

/** @} */ // end of stanConnOptsGroup

/** \defgroup stanSubOptsGroup Streaming Subscription Options
 *
 *   NATS Streaming Subscription Options.
 *   @{
 */

/** \brief Creates a #stanSubOptions object.
 *
 * Creates a #stanSubOptions object. This object is used when one wants to set
 * specific options prior to create a subscription.
 *
 * After making the appropriate `stanSubOptions_SetXXX()` calls, this object is passed
 * to the #stanConnection_Subscribe() or #stanConnection_QueueSubscribe() call, which
 * will clone this object. It means that modifications to the options object will not
 * affect the created subscription.
 *
 * The default options set in this call are:
 * ackWait: 30000 milliseconds (30 seconds)
 * maxIinflight: 1024
 * start position: new only
 *
 * \note The object needs to be destroyed when no longer needed.
 *
 * @see stanConnection_Subscribe()
 * @see stanConnection_QueueSubscribe()
 * @see stanSubOptions_Destroy()
 *
 * @param newOpts the location where store the pointer to the newly created
 * #stanSubOptions object.
 */
NATS_EXTERN natsStatus
stanSubOptions_Create(stanSubOptions **newOpts);

/** \brief Sets the Durable Name for this subscription.
 *
 * If a durable name is set, this subscription will be durable. It means that
 * if the application stops and re-create the same subscription with the
 * same connection client ID and durable name (or simply durable name for
 * queue subscriptions), then the server will resume (re)delivery of messages
 * from the last known position in the steam for that durable.
 *
 * It means that the start position, if provided, is used only when the durable
 * subscription is first created, then is ignored by the server for the rest
 * of the durable subscription lifetime.
 *
 * \note Durable names should be alphanumeric and not contain the character `:`.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param durableName the string representing the name of the durable subscription.
 *
 */
NATS_EXTERN natsStatus
stanSubOptions_SetDurableName(stanSubOptions *opts, const char *durableName);

/** \brief Sets the timeout for waiting for an ACK from the cluster's point of view for delivered messages.
 *
 * Value expressed in milliseconds.
 *
 * Default is 30000 milliseconds (30 seconds).
 *
 * If the server does not receive an acknowledgment from the subscription for
 * a delivered message after this amount of time, the server will re-deliver
 * the unacknowledged message.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param wait how long the server will wait for an acknowledgment from the subscription.
 */
NATS_EXTERN natsStatus
stanSubOptions_SetAckWait(stanSubOptions *opts, int64_t wait);

/** \brief Sets the the maximum number of messages the cluster will send without an ACK.
 *
 * Default is 1024.
 *
 * If a subscription receives messages but does not acknowledge them, the server will
 * stop sending new messages when it reaches this number. Unacknowledged messages are
 * re-delivered regardless of that setting.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param maxInflight the maximum number of messages the subscription will receive without sending back ACKs.
 */
NATS_EXTERN natsStatus
stanSubOptions_SetMaxInflight(stanSubOptions *opts, int maxInflight);

/** \brief Sets the desired start position based on the given sequence number.
 *
 * This allows the subscription to start at a specific sequence number in
 * the channel's message log.
 *
 * If the sequence is smaller than the first available message in the
 * message log (old messages dropped due to channel limits),
 * the subscription will be created with the first available sequence.
 *
 * Conversely, if the given sequence is higher than the currently
 * last sequence number, the subscription will receive only new published messages.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param seq the starting sequence.
 */
NATS_EXTERN natsStatus
stanSubOptions_StartAtSequence(stanSubOptions *opts, uint64_t seq);

/** \brief Sets the desired start position based on the given time.
 *
 * When the subscription is created, the server will send messages starting
 * with the message which timestamp is at least the given time. The time
 * is expressed in number of milliseconds since the EPOCH, as given by
 * nats_Now() for instance.
 *
 * If the time is in the past and the most recent message's timestamp is older
 * than the given time, or if the time is in the future, the subscription
 * will receive only new messages.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param time the start time, expressed in milliseconds since the EPOCH.
 */
NATS_EXTERN natsStatus
stanSubOptions_StartAtTime(stanSubOptions *opts, int64_t time);

/** \brief Sets the desired start position based on the given delta.
 *
 * When the subscription is created, the server will send messages starting
 * with the message which timestamp is at least now minus `delta`. In other words,
 * this means start receiving messages that were sent n milliseconds ago.
 *
 * The delta is expressed in milliseconds.
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param delta he historical time delta (from now) from which to start receiving messages.
 */
NATS_EXTERN natsStatus
stanSubOptions_StartAtTimeDelta(stanSubOptions *opts, int64_t delta);

/** \brief The subscription should start with the last message in the channel.
 *
 * When the subscription is created, the server will start sending messages
 * starting with the last message currently in the channel message's log.
 * The subscription will then receive any new published message.
 *
 * @param opts the pointer to the #stanSubOptions object.
 */
NATS_EXTERN natsStatus
stanSubOptions_StartWithLastReceived(stanSubOptions *opts);

/** \brief The subscription should start with the first message in the channel.
 *
 * When the subscription is created, the server will start sending messages
 * starting with the first message currently in the channel message's log.
 *
 * @param opts the pointer to the #stanSubOptions object.
 */
NATS_EXTERN natsStatus
stanSubOptions_DeliverAllAvailable(stanSubOptions *opts);

/** \brief Sets the subscription's acknowledgment mode.
 *
 * By default, a subscription will automatically send a message acknowledgment
 * after the #stanMsgHandler callback returns.
 *
 * In order to control when the acknowledgment is sent, set the acknowledgment
 * mode to `manual` and call #stanSubscription_AckMsg().
 *
 * @see #stanSubscription_AckMsg()
 *
 * @param opts the pointer to the #stanSubOptions object.
 * @param manual a boolean indicating if the subscription should auto-acknowledge
 * or if the user will.
 */
NATS_EXTERN natsStatus
stanSubOptions_SetManualAckMode(stanSubOptions *opts, bool manual);

/** \brief Destroys a #stanSubOptions object.
 *
 * Destroys the #stanSubOptions object, freeing used memory. See the note in
 * the #stanSubOptions_Create() call.
 *
 * @param opts the pointer to the #stanSubOptions object to destroy.
 */
NATS_EXTERN void
stanSubOptions_Destroy(stanSubOptions *opts);

/** @} */ // end of stanSubOptsGroup
#endif

/** \defgroup inboxGroup Inboxes
 *
 *   NATS Inboxes.
 *   @{
 */

/** \brief Creates an inbox.
 *
 * Returns an inbox string which can be used for directed replies from
 * subscribers. These are guaranteed to be unique, but can be shared
 * and subscribed to by others.
 *
 * \note The inbox needs to be destroyed when no longer needed.
 *
 * @see #natsInbox_Destroy()
 *
 * @param newInbox the location where to store a pointer to the newly
 * created #natsInbox.
 */
NATS_EXTERN natsStatus
natsInbox_Create(natsInbox **newInbox);

/** \brief Destroys the inbox.
 *
 * Destroys the inbox.
 *
 * @param inbox the pointer to the #natsInbox object to destroy.
 */
NATS_EXTERN void
natsInbox_Destroy(natsInbox *inbox);

/** @} */ // end of inboxGroup

/** \defgroup msgGroup Message
 *
 *  NATS Message.
 *  @{
 */

/** \brief  Destroys this list of messages.
 *
 * This function iterates through the list of all messages and call #natsMsg_Destroy
 * for each valid (not set to `NULL`) message. It then frees the array that was
 * allocated to hold pointers to those messages.
 *
 * \note The #natsMsgList object itself is not freed since it is expected that
 * users will pass a pointer to a stack object. Should the user create its own
 * object, it will be the user responsibility to free this object.
 *
 * @param list the #natsMsgList list of #natsMsg objects to destroy.
 */
NATS_EXTERN void
natsMsgList_Destroy(natsMsgList *list);

/** \brief Creates a #natsMsg object.
 *
 * Creates a #natsMsg object. This is used by the subscription related calls
 * and by #natsConnection_PublishMsg().
 *
 * \note Messages need to be destroyed with #natsMsg_Destroy() when no
 * longer needed.
 *
 * @see natsMsg_Destroy()
 *
 * @param newMsg the location where to store the pointer to the newly created
 * #natsMsg object.
 * @param subj the subject this message will be sent to. Cannot be `NULL`.
 * @param reply the optional reply for this message.
 * @param data the optional message payload.
 * @param dataLen the size of the payload.
 */
NATS_EXTERN natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen);

/** \brief Returns the subject set in this message.
 *
 * Returns the subject set on that message.
 *
 * \warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN const char*
natsMsg_GetSubject(const natsMsg *msg);

/** \brief Returns the reply set in this message.
 *
 * Returns the reply, possibly `NULL`.
 *
 * \warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN const char*
natsMsg_GetReply(const natsMsg *msg);

/** \brief Returns the message payload.
 *
 * Returns the message payload, possibly `NULL`.
 *
 * Note that although the data sent and received from the server is not `NULL`
 * terminated, the NATS C Client does add a `NULL` byte to the received payload.
 * If you expect the received data to be a "string", then this conveniently
 * allows you to call #natsMsg_GetData() without having to copy the returned
 * data to a buffer to add the `NULL` byte at the end.
 *
 * \warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN const char*
natsMsg_GetData(const natsMsg *msg);

/** \brief Returns the message length.
 *
 * Returns the message's payload length, possibly 0.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN int
natsMsg_GetDataLength(const natsMsg *msg);

/** \brief Set the header entries associated with `key` to the single element `value`.
 *
 * It will replace any existing value associated with `key`.
 *
 * \warning Prior to v3.0.0, the `key` was stored in its canonical form, this is no
 * longer the case. Header keys are now case sensitive.
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param key the key under which the `value` will be stored. It can't ne `NULL` or empty.
 * @param value the string to store under the given `key`. The value can be `NULL` or empty string.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Set(natsMsg *msg, const char *key, const char *value);

/** \brief Add `value` to the header associated with `key`.
 *
 * It will append to any existing values associated with `key`.
 *
 * \warning Prior to v3.0.0, the `key` was stored in its canonical form, this is no
 * longer the case. Header keys are now case sensitive.
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param key the key under which the `value` will be stored. It can't ne `NULL` or empty.
 * @param value the string to add to the values associated with the given `key`. The value can be `NULL` or empty string.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Add(natsMsg *msg, const char *key, const char *value);

/** \brief Get the header entry associated with `key`.
 *
 * If more than one entry for the `key` is available, the first is returned.
 * The returned value is owned by the library and MUST not be freed or altered.
 *
 * \warning Prior to v3.0.0, the `key` was stored in its canonical form, this is no
 * longer the case. Header keys are now case sensitive.
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param key the key for which the value is requested.
 * @param value the memory location where the library will store the pointer to the first
 * value (if more than one is found) associated with the `key`.
 * @return NATS_NOT_FOUND if `key` is not present in the headers.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Get(natsMsg *msg, const char *key, const char **value);

/** \brief Get all header values associated with `key`.
 *
 * The returned strings are own by the library and MUST not be freed or altered.
 * However, the returned array `values` MUST be freed by the user.
 *
 * \code{.c}
 * const char* *values = NULL;
 * int         count   = 0;
 *
 * s = natsMsgHeader_Values(msg, "My-Key", &values, &count);
 * if (s == NATS_OK)
 * {
 *      // do something with the values
 *
 *      // then free the array of pointers.
 *      free((void*) values);
 * }
 * \endcode
 *
 * \warning Prior to v3.0.0, the `key` was stored in its canonical form, this is no
 * longer the case. Header keys are now case sensitive.
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param key the key for which the values are requested.
 * @param values the memory location where the library will store the pointer to the array
 * of values.
 * @param count the memory location where the library will store the number of values returned.
 * @return NATS_NOT_FOUND if `key` is not present in the headers.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Values(natsMsg *msg, const char *key, const char* **values, int *count);

/** \brief Get all header keys.
 *
 * The returned strings are own by the library and MUST not be freed or altered.
 * However, the returned array `keys` MUST be freed by the user.
 *
 * \code{.c}
 * const char* *keys = NULL;
 * int         count   = 0;
 *
 * s = natsMsgHeader_Keys(msg, &keys, &count);
 * if (s == NATS_OK)
 * {
 *      // do something with the keys
 *
 *      // then free the array of pointers.
 *      free((void*) keys);
 * }
 * \endcode
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param keys the memory location where the library will store the pointer to the array
 * of keys.
 * @param count the memory location where the library will store the number of keys returned.
 * @return NATS_NOT_FOUND if no key is present.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Keys(natsMsg *msg, const char* **keys, int *count);

/** \brief Delete the value(s) associated with `key`.
 *
 * \warning Prior to v3.0.0, the `key` was stored in its canonical form, this is no
 * longer the case. Header keys are now case sensitive.
 *
 * \warning Headers are not thread-safe, that is, you must not set/add/get values or
 * delete keys for the same message from different threads. The internal structure
 * of `natsMsg` may possible be altered during this call.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param key the key to delete from the headers map.
 * @return NATS_NOT_FOUND if `key` is not present in the headers.
 */
NATS_EXTERN natsStatus
natsMsgHeader_Delete(natsMsg *msg, const char *key);

/** \brief Indicates if this message is a "no responders" message from the server.
 *
 * Starting with the NATS Server v2.2.0+ and the C client v2.2.0+ releases, which
 * introduced support for message headers and the "no responders" feature, if a
 * request is received by the server and there are no subscriptions on the
 * request's subject, the server sends a message with no payload but with a header
 * "Status" with value "503".
 *
 * The call #natsConnection_Request() and its variants intercept this special
 * message and instead of returning it to the user, they return #NATS_NO_RESPONDERS.
 *
 * If a synchronous subscription is created on a subject used as a reply subject
 * to a #natsConnection_PublishRequest call (and its variants), #natsSubscription_NextMsg
 * also detects this message and returns #NATS_NO_RESPONDERS (but it was not from
 * release v2.2.0 to v2.4.1).
 *
 * For asynchronous subscriptions, the user may want to know that the request
 * failed because there are no responders. For that reason, the message is passed
 * to the message callback, and this function can be used to detect that this
 * is a "no responders" message from the server and act accordingly.
 *
 * @param msg the pointer to the #natsMsg object.
 * @return `true` if this message is a "no responders" message from the server,
 * that is, has no payload and the "Status" header with "503" as the value.
 */
NATS_EXTERN bool
natsMsg_IsNoResponders(natsMsg *msg);

/** \brief Destroys the message object.
 *
 * Destroys the message, freeing memory.
 *
 * @param msg the pointer to the #natsMsg object to destroy.
 */
NATS_EXTERN void
natsMsg_Destroy(natsMsg *msg);

/** @} */ // end of msgGroup

#if defined(NATS_HAS_STREAMING)
/** \defgroup stanMsgGroup Streaming Message
 *
 *  NATS Streaming Message.
 *  @{
 */

/** \brief Returns the message's sequence number.
 *
 * Returns the message's sequence number (as assigned by the cluster).
 *
 * @param msg the pointer to the #stanMsg object.
 */
NATS_EXTERN uint64_t
stanMsg_GetSequence(const stanMsg *msg);

/** \brief Returns the message's timestamp.
 *
 * Returns the message's timestamp (as assigned by the cluster).
 *
 * @param msg the pointer to the #stanMsg object.
 */
NATS_EXTERN int64_t
stanMsg_GetTimestamp(const stanMsg *msg);

/** \brief Returns the message's redelivered flag.
 *
 * Returns the message's redelivered flag. This can help detect if this
 * message is a possible duplicate (due to redelivery and at-least-once
 * semantic).
 *
 * @param msg the pointer to the #stanMsg object.
 */
NATS_EXTERN bool
stanMsg_IsRedelivered(const stanMsg *msg);

/** \brief Returns the message payload.
 *
 * Returns the message payload, possibly `NULL`.
 *
 * Note that although the data sent and received from the server is not `NULL`
 * terminated, the NATS C Client does add a `NULL` byte to the received payload.
 * If you expect the received data to be a "string", then this conveniently
 * allows you to call #stanMsg_GetData() without having to copy the returned
 * data to a buffer to add the `NULL` byte at the end.
 *
 * \warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 *
 * @param msg the pointer to the #stanMsg object.
 */
NATS_EXTERN const char*
stanMsg_GetData(const stanMsg *msg);

/** \brief Returns the message length.
 *
 * Returns the message's payload length, possibly 0.
 *
 * @param msg the pointer to the #stanMsg object.
 */
NATS_EXTERN int
stanMsg_GetDataLength(const stanMsg *msg);

/** \brief Destroys the message object.
 *
 * Destroys the message, freeing memory.
 *
 * @param msg the pointer to the #stanMsg object to destroy.
 */
NATS_EXTERN void
stanMsg_Destroy(stanMsg *msg);

/** @} */ // end of stanMsgGroup
#endif

/** \defgroup connGroup Connection
 *
 * NATS Connection
 * @{
 */

/** \defgroup connMgtGroup Management
 *
 *  Functions related to connection management.
 *  @{
 */

/** \brief Connects to a `NATS Server` using the provided options.
 *
 * Attempts to connect to a `NATS Server` with multiple options.
 *
 * This call is cloning the #natsOptions object. Once this call returns,
 * changes made to the `options` will not have an effect to this
 * connection. The `options` can however be changed prior to be
 * passed to another #natsConnection_Connect() call if desired.
 *
 * @see #natsOptions
 * @see #natsConnection_Destroy()
 *
 * @param nc the location where to store the pointer to the newly created
 * #natsConnection object.
 * @param options the options to use for this connection. If `NULL`
 * this call is equivalent to #natsConnection_ConnectTo() with #NATS_DEFAULT_URL.
 *
 */
NATS_EXTERN natsStatus
natsConnection_Connect(natsConnection **nc, natsOptions *options);

/** \brief Process a read event when using external event loop.
 *
 * When using an external event loop, and the callback indicating that
 * the socket is ready for reading, this call will read data from the
 * socket and process it.
 *
 * @param nc the pointer to the #natsConnection object.
 *
 * \warning This API is reserved for external event loop adapters.
 */
NATS_EXTERN void
natsConnection_ProcessReadEvent(natsConnection *nc);

/** \brief Process a write event when using external event loop.
 *
 * When using an external event loop, and the callback indicating that
 * the socket is ready for writing, this call will write data to the
 * socket.
 *
 * @param nc the pointer to the #natsConnection object.
 *
 * \warning This API is reserved for external event loop adapters.
 */
NATS_EXTERN void
natsConnection_ProcessWriteEvent(natsConnection *nc);

/** \brief Connects to a `NATS Server` using any of the URL from the given list.
 *
 * Attempts to connect to a `NATS Server`.
 *
 * This call supports multiple comma separated URLs. If more than one is
 * specified, it behaves as if you were using a #natsOptions object and
 * called #natsOptions_SetServers() with the equivalent array of URLs.
 * The list is randomized before the connect sequence starts.
 *
 * @see #natsConnection_Destroy()
 * @see #natsOptions_SetServers()
 *
 * @param nc the location where to store the pointer to the newly created
 * #natsConnection object.
 * @param urls the URL to connect to, or the list of URLs to chose from.
 * If `NULL` this call is equivalent to #natsConnection_ConnectTo()
 * with #NATS_DEFAULT_URL
 */
NATS_EXTERN natsStatus
natsConnection_ConnectTo(natsConnection **nc, const char *urls);

/** \brief Test if connection has been closed.
 *
 * Tests if connection has been closed.
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN bool
natsConnection_IsClosed(natsConnection *nc);

/** \brief Test if connection is reconnecting.
 *
 * Tests if connection is reconnecting.
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN bool
natsConnection_IsReconnecting(natsConnection *nc);

/** \brief Test if connection is draining.
 *
 * Tests if connection is draining.
 *
 * @param nc the pointer to the #natsConnection object.
 */
bool
natsConnection_IsDraining(natsConnection *nc);

/** \brief Returns the current state of the connection.
 *
 * Returns the current state of the connection.
 *
 * @see #natsConnStatus
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN natsConnStatus
natsConnection_Status(natsConnection *nc);

/** \brief Returns the number of bytes to be sent to the server.
 *
 * When calling any of the publish functions, data is not necessarily
 * immediately sent to the server. Some buffering occurs, allowing
 * for better performance. This function indicates if there is any
 * data not yet transmitted to the server.
 *
 * @param nc the pointer to the #natsConnection object.
 * @return the number of bytes to be sent to the server, or -1 if the
 * connection is closed.
 */
NATS_EXTERN int
natsConnection_Buffered(natsConnection *nc);

/** \brief Flushes the connection.
 *
 * Performs a round trip to the server and return when it receives the
 * internal reply.
 *
 * Note that if this call occurs when the connection to the server is
 * lost, the `PING` will not be echoed even if the library can connect
 * to a new (or the same) server. Therefore, in such situation, this
 * call will fail with the status #NATS_CONNECTION_DISCONNECTED.
 *
 * If the connection is closed while this call is in progress, then the
 * status #NATS_CONN_STATUS_CLOSED would be returned instead.
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN natsStatus
natsConnection_Flush(natsConnection *nc);

/** \brief Flushes the connection with a given timeout.
 *
 * Performs a round trip to the server and return when it receives the
 * internal reply, or if the call times-out (timeout is expressed in
 * milliseconds).
 *
 * See possible failure case described in #natsConnection_Flush().
 *
 * @param nc the pointer to the #natsConnection object.
 * @param timeout in milliseconds, is the time allowed for the flush
 * to complete before #NATS_TIMEOUT error is returned.
 */
NATS_EXTERN natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout);

/** \brief Returns the maximum message payload.
 *
 * Returns the maximum message payload accepted by the server. The
 * information is gathered from the `NATS Server` when the connection is
 * first established.
 *
 * @param nc the pointer to the #natsConnection object.
 * @return the maximum message payload.
 */
NATS_EXTERN int64_t
natsConnection_GetMaxPayload(natsConnection *nc);

/** \brief Gets the connection statistics.
 *
 * Copies in the provided statistics structure, a snapshot of the statistics for
 * this connection.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param stats the pointer to a #natsStatistics object in which statistics
 * will be copied.
 */
NATS_EXTERN natsStatus
natsConnection_GetStats(natsConnection *nc, natsStatistics *stats);

/** \brief Gets the URL of the currently connected server.
 *
 * Copies in the given buffer, the connected server's Url. If the buffer is
 * too small, an error is returned.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param buffer the buffer in which the URL is copied.
 * @param bufferSize the size of the buffer.
 */
NATS_EXTERN natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize);

/** \brief Gets the server Id.
 *
 * Copies in the given buffer, the connected server's Id. If the buffer is
 * too small, an error is returned.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param buffer the buffer in which the server id is copied.
 * @param bufferSize the size of the buffer.
 */
NATS_EXTERN natsStatus
natsConnection_GetConnectedServerId(natsConnection *nc, char *buffer, size_t bufferSize);

/** \brief Returns the list of server URLs known to this connection.
 *
 * Returns the list of known servers, including additional servers
 * discovered after a connection has been established (with servers
 * version 0.9.2 and above).
 *
 * No credential information is included in any of the server URLs
 * returned by this call.<br>
 * If you want to use any of these URLs to connect to a server that
 * requires authentication, you will need to use #natsOptions_SetUserInfo
 * or #natsOptions_SetToken.
 *
 * \note The user is responsible for freeing the memory of the returned array.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param servers the location where to store the pointer to the array
 * of server URLs.
 * @param count the location where to store the number of elements of the
 * returned array.
 */
NATS_EXTERN natsStatus
natsConnection_GetServers(natsConnection *nc, char ***servers, int *count);

/** \brief Returns the list of discovered server URLs.
 *
 * Unlike #natsConnection_GetServers, this function only returns
 * the list of servers that have been discovered after the a connection
 * has been established (with servers version 0.9.2 and above).
 *
 * No credential information is included in any of the server URLs
 * returned by this call.<br>
 * If you want to use any of these URLs to connect to a server that
 * requires authentication, you will need to use #natsOptions_SetUserInfo
 * or #natsOptions_SetToken.
 *
 * \note The user is responsible for freeing the memory of the returned array.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param servers the location where to store the pointer to the array
 * of server URLs.
 * @param count the location where to store the number of elements of the
 * returned array.
 */
NATS_EXTERN natsStatus
natsConnection_GetDiscoveredServers(natsConnection *nc, char ***servers, int *count);

/** \brief Gets the last connection error.
 *
 * Returns the last known error as a 'natsStatus' and the location to the
 * null-terminated error string.
 *
 * \warning The returned string is owned by the connection object and
 * must not be freed.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param lastError the location where the pointer to the connection's last
 * error string is copied.
 */
NATS_EXTERN natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError);

/** \brief Gets the current client ID assigned by the server.
 *
 * Returns the client ID assigned by the server to which the client is
 * currently connected to.
 *
 * \note The value may change if the client reconnects.
 *
 * This function returns #NATS_NO_SERVER_SUPPORT if the server's version
 * is less than 1.2.0.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param cid the location where to store the client ID.
 */
NATS_EXTERN natsStatus
natsConnection_GetClientID(natsConnection *nc, uint64_t *cid);

/** \brief Drains the connection with default timeout.
 *
 * Drain will put a connection into a drain state. All subscriptions will
 * immediately be put into a drain state. Upon completion, the publishers
 * will be drained and can not publish any additional messages. Upon draining
 * of the publishers, the connection will be closed. Use the #natsOptions_SetClosedCB()
 * option to know when the connection has moved from draining to closed.
 *
 * This call uses a default drain timeout of 30 seconds.
 *
 * \warning This function does not block waiting for the draining operation
 * to complete.
 *
 * @see natsOptions_SetClosedCB
 * @see natsConnection_DrainTimeout
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN natsStatus
natsConnection_Drain(natsConnection *nc);

/** \brief Drains the connection with given timeout.
 *
 * Identical to #natsConnection_Drain but the timeout can be specified here.
 *
 * The value is expressed in milliseconds. Zero or negative value means
 * that the operation will not timeout.
 *
 * \warning This function does not block waiting for the draining operation
 * to complete.
 *
 * @see natsOptions_SetClosedCB
 * @see natsConnection_Drain
 *
 * @param nc the pointer to the #natsConnection object.
 * @param timeout the allowed time for a drain operation to complete, expressed
 * in milliseconds.
 */
NATS_EXTERN natsStatus
natsConnection_DrainTimeout(natsConnection *nc, int64_t timeout);

/** \brief Signs any 'message' using the connection's user credentials.
 *
 * The connection must have been created with the #natsOptions_SetUserCredentialsFromFiles.
 *
 * This call will sign the given message with the private key extracted through
 * the user credentials file(s).
 *
 * @param nc the pointer to the #natsConnection object.
 * @param message the byte array to sign.
 * @param messageLen the length of the given byte array.
 * @param sig an array of 64 bytes in which the signature will be copied.
 */
NATS_EXTERN natsStatus
natsConnection_Sign(natsConnection *nc,
                    const unsigned char *message, int messageLen,
                    unsigned char sig[64]);

/** \brief Returns the client's IP address as reported by the server.
 *
 * When a connection is created to the server, the server identifies
 * the connection's remote IP address and return it back to the client.
 *
 * \note The user is responsible to free memory allocated to store
 * the client IP address.
 *
 * \note This is supported on servers >= version 2.1.6. Calling
 * #natsConnection_GetClientIP() with server below this version will
 * return the #NATS_NO_SERVER_SUPPORT error.
 *
 * @see natsConnection_GetLocalIPAndPort to get the local IP and port instead.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param ip the memory location where to store the client's IP string.
 * The user is responsible from freeing this memory.
 */
NATS_EXTERN natsStatus
natsConnection_GetClientIP(natsConnection *nc, char **ip);

/** \brief Returns the round trip time between this client and the server.
 *
 * The value returned is in nanoseconds.
 *
 * \note If the library is currently trying to reconnect, this call will
 * return #NATS_CONNECTION_DISCONNECTED.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param rtt the memory location where to store the rtt.
 */
NATS_EXTERN natsStatus
natsConnection_GetRTT(natsConnection *nc, int64_t *rtt);

/** \brief Returns if the connection to current server supports headers.
 *
 * Returns NATS_OK if the server this client is currently connected to
 * supports headers, NATS_NO_SERVER_SUPPORT otherwise.
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN natsStatus
natsConnection_HasHeaderSupport(natsConnection *nc);

/** \brief Returns the connection local IP and port.
 *
 * Unlike #natsConnection_GetClientIP, this function returns the
 * connection's local IP and port.
 *
 * \note The user is responsible for freeing the memory allocated
 * for the returned IP string.
 *
 * @param nc the pointer of the #natsConnection object.
 * @param ip the memory location where to store the local IP string.
 * The user is responsible from freeing this memory.
 * @param port the memory location where to store the local port.
 *
 * @return #NATS_OK on success.
 * @return #NATS_CONNECTION_DISCONNECTED if disconnected.
 * @return #NATS_CONNECTION_CLOSED is connection is closed.
 * @return #NATS_SYS_ERROR if a system error getting the IP and port occurred.
 * @return #NATS_NO_MEMORY if the library was unable to allocate memory for the returned IP string.
 */
natsStatus
natsConnection_GetLocalIPAndPort(natsConnection *nc, char **ip, int *port);

/** \brief Closes the connection.
 *
 * Closes the connection to the server. This call will release all blocking
 * calls, such as #natsConnection_Flush() and #natsSubscription_NextMsg().
 * The connection object is still usable until the call to
 * #natsConnection_Destroy().
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN void
natsConnection_Close(natsConnection *nc);

/** \brief Destroys the connection object.
 *
 * Destroys the connection object, freeing up memory.
 * If not already done, this call first closes the connection to the server.
 *
 * @param nc the pointer to the #natsConnection object.
 */
NATS_EXTERN void
natsConnection_Destroy(natsConnection *nc);

/** @} */ // end of connMgtGroup

/** \defgroup connPubGroup Publishing
 *
 *  Publishing functions
 *  @{
 */

/** \brief Publishes data on a subject.
 *
 * Publishes the data argument to the given subject. The data argument is left
 * untouched and needs to be correctly interpreted on the receiver.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the data is sent to.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 */
NATS_EXTERN natsStatus
natsConnection_Publish(natsConnection *nc, const char *subj,
                       const void *data, int dataLen);

/** \brief Publishes a string on a subject.
 *
 * Convenient function to publish a string. This call is equivalent to:
 *
 * \code{.c}
 * const char* myString = "hello";
 *
 * natsConnection_Publish(nc, subj, (const void*) myString, (int) strlen(myString));
 * \endcode
 *
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the data is sent to.
 * @param str the string to be sent.
 */
NATS_EXTERN natsStatus
natsConnection_PublishString(natsConnection *nc, const char *subj,
                             const char *str);

/** \brief Publishes a message on a subject.
 *
 * Publishes the #natsMsg, which includes the subject, an optional reply and
 * optional data.
 *
 * @see #natsMsg_Create()
 *
 * @param nc the pointer to the #natsConnection object.
 * @param msg the pointer to the #natsMsg object to send.
 */
NATS_EXTERN natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg);

/** \brief Publishes data on a subject expecting replies on the given reply.
 *
 * Publishes the data argument to the given subject expecting a response on
 * the reply subject. Use #natsConnection_Request() for automatically waiting
 * for a response inline.
 *
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the request is sent to.
 * @param reply the reply on which resonses are expected.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 */
NATS_EXTERN natsStatus
natsConnection_PublishRequest(natsConnection *nc, const char *subj,
                              const char *reply, const void *data, int dataLen);

/** \brief Publishes a string on a subject expecting replies on the given reply.
 *
 * Convenient function to publish a request as a string. This call is
 * equivalent to:
 *
 * \code{.c}
 * const char* myString = "hello";
 *
 * natsPublishRequest(nc, subj, reply, (const void*) myString, (int) strlen(myString));
 * \endcode
 *
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the request is sent to.
 * @param reply the reply on which resonses are expected.
 * @param str the string to send.
 */
NATS_EXTERN natsStatus
natsConnection_PublishRequestString(natsConnection *nc, const char *subj,
                                    const char *reply, const char *str);

/** \brief Sends a request and waits for a reply.
 *
 * Sends a request payload and delivers the first response message,
 * or an error, including a timeout if no message was received properly.
 *
 * \warning If connected to a NATS Server v2.2.0+ with no responder running
 * when the request is received, this call will return a #NATS_NO_RESPONDERS error.
 *
 * @param replyMsg the location where to store the pointer to the received
 * #natsMsg reply.
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the request is sent to.
 * @param data the data of the request, can be `NULL`.
 * @param dataLen the length of the data to send.
 * @param timeout in milliseconds, before this call returns #NATS_TIMEOUT
 * if no response is received in this alloted time.
 */
NATS_EXTERN natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout);

/** \brief Sends a request (as a string) and waits for a reply.
 *
 * Convenient function to send a request as a string. This call is
 * equivalent to:
 *
 * \code{.c}
 * const char* myString = "hello";
 *
 * natsConnection_Request(replyMsg, nc, subj, (const void*) myString, (int) strlen(myString));
 * \endcode
 *
 * \warning See warning about no responders in #natsConnection_Request().
 *
 * @param replyMsg the location where to store the pointer to the received
 * #natsMsg reply.
 * @param nc the pointer to the #natsConnection object.
 * @param subj the subject the request is sent to.
 * @param str the string to send.
 * @param timeout in milliseconds, before this call returns #NATS_TIMEOUT
 * if no response is received in this alloted time.
 */
NATS_EXTERN natsStatus
natsConnection_RequestString(natsMsg **replyMsg, natsConnection *nc,
                             const char *subj, const char *str,
                             int64_t timeout);

/** \brief Sends a request based on the given `requestMsg` and waits for a reply.
 *
 * Similar to #natsConnection_Request but uses `requestMsg` to extract subject,
 * and payload to send.
 *
 * \warning See warning about no responders in #natsConnection_Request().
 *
 * @param replyMsg the location where to store the pointer to the received
 * #natsMsg reply.
 * @param nc the pointer to the #natsConnection object.
 * @param requestMsg the message used for the request.
 * @param timeout in milliseconds, before this call returns #NATS_TIMEOUT
 * if no response is received in this alloted time.
 */
NATS_EXTERN natsStatus
natsConnection_RequestMsg(natsMsg **replyMsg, natsConnection *nc,
                          natsMsg *requestMsg, int64_t timeout);

/** @} */ // end of connPubGroup

/** \defgroup connSubGroup Subscribing
 *
 *  Subscribing functions.
 *  @{
 */

/** \brief Creates an asynchronous subscription.
 *
 * Expresses interest in the given subject. The subject can have wildcards
 * (see \ref wildcardsGroup). Messages will be delivered to the associated
 * #natsMsgHandler.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 */
NATS_EXTERN natsStatus
natsConnection_Subscribe(natsSubscription **sub, natsConnection *nc,
                         const char *subject, natsMsgHandler cb,
                         void *cbClosure);

/** \brief Creates an asynchronous subscription with a timeout.
 *
 * Expresses interest in the given subject. The subject can have wildcards
 * (see \ref wildcardsGroup). Messages will be delivered to the associated
 * #natsMsgHandler.
 *
 * If no message is received by the given timeout (in milliseconds), the
 * message handler is invoked with a `NULL` message.<br>
 * You can then destroy the subscription in the callback, or simply
 * return, in which case, the message handler will fire again when a
 * message is received or the subscription times-out again.
 *
 * \note Receiving a message reset the timeout. Until all pending messages
 * are processed, no timeout will occur. The timeout starts when the
 * message handler for the last pending message returns.
 *
 * \warning If you re-use message handler code between subscriptions with
 * and without timeouts, keep in mind that the message passed in the
 * message handler may be `NULL`.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 * @param timeout the interval (in milliseconds) after which, if no message
 * is received, the message handler is invoked with a `NULL` message.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 */
NATS_EXTERN natsStatus
natsConnection_SubscribeTimeout(natsSubscription **sub, natsConnection *nc,
                                const char *subject, int64_t timeout,
                                natsMsgHandler cb, void *cbClosure);

/** \brief Creates a synchronous subcription.
 *
 * Similar to #natsConnection_Subscribe, but creates a synchronous subscription
 * that can be polled via #natsSubscription_NextMsg().
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 */
NATS_EXTERN natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc,
                             const char *subject);

/** \brief Creates an asynchronous queue subscriber.
 *
 * Creates an asynchronous queue subscriber on the given subject.
 * All subscribers with the same queue name will form the queue group and
 * only one member of the group will be selected to receive any given
 * message asynchronously.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 * @param queueGroup the name of the group.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 *
 */
NATS_EXTERN natsStatus
natsConnection_QueueSubscribe(natsSubscription **sub, natsConnection *nc,
                              const char *subject, const char *queueGroup,
                              natsMsgHandler cb, void *cbClosure);

/** \brief Creates an asynchronous queue subscriber with a timeout.
 *
 * Creates an asynchronous queue subscriber on the given subject.
 * All subscribers with the same queue name will form the queue group and
 * only one member of the group will be selected to receive any given
 * message asynchronously.
 *
 * If no message is received by the given timeout (in milliseconds), the
 * message handler is invoked with a `NULL` message.<br>
 * You can then destroy the subscription in the callback, or simply
 * return, in which case, the message handler will fire again when a
 * message is received or the subscription times-out again.
 *
 * \note Receiving a message reset the timeout. Until all pending messages
 * are processed, no timeout will occur. The timeout starts when the
 * message handler for the last pending message returns.
 *
 * \warning If you re-use message handler code between subscriptions with
 * and without timeouts, keep in mind that the message passed in the
 * message handler may be `NULL`.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 * @param queueGroup the name of the group.
 * @param timeout the interval (in milliseconds) after which, if no message
 * is received, the message handler is invoked with a `NULL` message.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 */
NATS_EXTERN natsStatus
natsConnection_QueueSubscribeTimeout(natsSubscription **sub, natsConnection *nc,
                   const char *subject, const char *queueGroup,
                   int64_t timeout, natsMsgHandler cb, void *cbClosure);

/** \brief Creates a synchronous queue subscriber.
 *
 * Similar to #natsConnection_QueueSubscribe, but creates a synchronous
 * subscription that can be polled via #natsSubscription_NextMsg().
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param nc the pointer to the #natsConnection object.
 * @param subject the subject this subscription is created for.
 * @param queueGroup the name of the group.
 */
NATS_EXTERN natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                                  const char *subject, const char *queueGroup);

/** @} */ // end of connSubGroup

/** @} */ // end of connGroup

/** \defgroup subGroup Subscription
 *
 *  NATS Subscriptions.
 *  @{
 */

/** \brief Enables the No Delivery Delay mode.
 *
 * By default, messages that arrive are not immediately delivered. This
 * generally improves performance. However, in case of request-reply,
 * this delay has a negative impact. In such case, call this function
 * to have the subscriber be notified immediately each time a message
 * arrives.
 *
 * @param sub the pointer to the #natsSubscription object.
 *
 * \deprecated No longer needed. Will be removed in the future.
 */
NATS_EXTERN natsStatus
natsSubscription_NoDeliveryDelay(natsSubscription *sub);

/** \brief Returns the next available message.
 *
 * Return the next message available to a synchronous subscriber or block until
 * one is available.
 * A timeout (expressed in milliseconds) can be used to return when no message
 * has been delivered. If the value is zero, then this call will not wait and
 * return the next message that was pending in the client, and #NATS_TIMEOUT
 * otherwise.
 *
 * \note If you create a subscription for a subject used as the reply subject
 * of a #natsConnection_PublishRequest call (or any of its variant), and there
 * are no responders for the request subject, NATS Servers v2.2.0+ will return
 * an empty message with a header "Status" and value "503". The library v2.2.0
 * until v2.4.1 would return this message to the user, which was wrong.<br>
 * The library now returns no message and #NATS_NO_RESPONDERS status.
 *
 * @param nextMsg the location where to store the pointer to the next available
 * message.
 * @param sub the pointer to the #natsSubscription object.
 * @param timeout time, in milliseconds, after which this call will return
 * #NATS_TIMEOUT if no message is available.
 */
NATS_EXTERN natsStatus
natsSubscription_NextMsg(natsMsg **nextMsg, natsSubscription *sub,
                         int64_t timeout);

/** \brief Unsubscribes.
 *
 * Removes interest on the subject. Asynchronous subscription may still have
 * a callback in progress, in that case, the subscription will still be valid
 * until the callback returns.
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub);

/** \brief Auto-Unsubscribes.
 *
 * This call issues an automatic #natsSubscription_Unsubscribe that is
 * processed by the server when 'max' messages have been received.
 * This can be useful when sending a request to an unknown number
 * of subscribers.
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param max the maximum number of message you want this subscription
 * to receive.
 */
NATS_EXTERN natsStatus
natsSubscription_AutoUnsubscribe(natsSubscription *sub, int max);

/** \brief Gets the number of pending messages.
 *
 * Returns the number of queued messages in the client for this subscription.
 *
 * \deprecated Use #natsSubscription_GetPending instead.
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param queuedMsgs the location where to store the number of queued messages.
 */
NATS_EXTERN natsStatus
natsSubscription_QueuedMsgs(natsSubscription *sub, uint64_t *queuedMsgs);

/** \brief Gets the subscription id.
 *
 * Returns the id of the given subscription.
 *
 * \note Invalid or closed subscriptions will cause a value of 0 to be returned.
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN int64_t
natsSubscription_GetID(natsSubscription* sub);

/** \brief Gets the subject name.
 *
 * Returns the subject of the given subscription.
 *
 * \note Invalid or closed subscriptions will cause a value of NULL to be returned.
 *
 * \warning The string belongs to the subscription and must not be freed. Copy it if needed.
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN const char*
natsSubscription_GetSubject(natsSubscription* sub);

/** \brief Sets the limit for pending messages and bytes.
 *
 * Specifies the maximum number and size of incoming messages that can be
 * buffered in the library for this subscription, before new incoming messages are
 * dropped and #NATS_SLOW_CONSUMER status is reported to the #natsErrHandler
 * callback (if one has been set).
 *
 * If no limit is set at the subscription level, the limit set by #natsOptions_SetMaxPendingMsgs
 * before creating the connection will be used.
 *
 * \note If no option is set, there is still a default of `65536` messages and
 * `65536 * 1024` bytes.
 *
 * @see natsOptions_SetMaxPendingMsgs
 * @see natsSubscription_GetPendingLimits
 *
 * @param sub he pointer to the #natsSubscription object.
 * @param msgLimit the limit in number of messages that the subscription can hold.
 * @param bytesLimit the limit in bytes that the subscription can hold.
 */
NATS_EXTERN natsStatus
natsSubscription_SetPendingLimits(natsSubscription *sub, int msgLimit, int bytesLimit);

/** \brief Returns the current limit for pending messages and bytes.
 *
 * Regardless if limits have been explicitly set with #natsSubscription_SetPendingLimits,
 * this call will store in the provided memory locations, the limits set for
 * this subscription.
 *
 * \note It is possible for `msgLimit` and/or `bytesLimits` to be `NULL`, in which
 * case the corresponding value is obviously not stored, but the function will
 * not return an error.
 *
 * @see natsOptions_SetMaxPendingMsgs
 * @see natsSubscription_SetPendingLimits
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param msgLimit if not `NULL`, the memory location where to store the maximum
 * number of pending messages for this subscription.
 * @param bytesLimit if not `NULL`, the memory location where to store the maximum
 * size of pending messages for this subscription.
 */
NATS_EXTERN natsStatus
natsSubscription_GetPendingLimits(natsSubscription *sub, int *msgLimit, int *bytesLimit);

/** \brief Returns the number of pending messages and bytes.
 *
 * Returns the total number and size of pending messages on this subscription.
 *
 * \note It is possible for `msgs` and `bytes` to be NULL, in which case the
 * corresponding values are obviously not stored, but the function will not return
 * an error.
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param msgs if not `NULL`, the memory location where to store the number of
 * pending messages.
 * @param bytes if not `NULL`, the memory location where to store the total size of
 * pending messages.
 */
NATS_EXTERN natsStatus
natsSubscription_GetPending(natsSubscription *sub, int *msgs, int *bytes);

/** \brief Returns the number of delivered messages.
 *
 * Returns the number of delivered messages for this subscription.
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param msgs the memory location where to store the number of
 * delivered messages.
 */
NATS_EXTERN natsStatus
natsSubscription_GetDelivered(natsSubscription *sub, int64_t *msgs);

/** \brief Returns the number of dropped messages.
 *
 * Returns the number of known dropped messages for this subscription. This happens
 * when a consumer is not keeping up and the library starts to drop messages
 * when the maximum number (and/or size) of pending messages has been reached.
 *
 * \note If the server declares the connection a slow consumer, this number may
 * not be valid.
 *
 * @see natsOptions_SetMaxPendingMsgs
 * @see natsSubscription_SetPendingLimits
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param msgs the memory location where to store the number of dropped messages.
 */
NATS_EXTERN natsStatus
natsSubscription_GetDropped(natsSubscription *sub, int64_t *msgs);

/** \brief Returns the maximum number of pending messages and bytes.
 *
 * Returns the maximum of pending messages and bytes seen so far.
 *
 * \note `msgs` and/or `bytes` can be NULL.
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param msgs if not `NULL`, the memory location where to store the maximum
 * number of pending messages seen so far.
 * @param bytes if not `NULL`, the memory location where to store the maximum
 * number of bytes pending seen so far.
 */
NATS_EXTERN natsStatus
natsSubscription_GetMaxPending(natsSubscription *sub, int *msgs, int *bytes);

/** \brief Clears the statistics regarding the maximum pending values.
 *
 * Clears the statistics regarding the maximum pending values.
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN natsStatus
natsSubscription_ClearMaxPending(natsSubscription *sub);

/** \brief Get various statistics from this subscription.
 *
 * This is a convenient function to get several subscription's statistics
 * in one call.
 *
 * \note Any or all of the statistics pointers can be `NULL`.
 *
 * @see natsSubscription_GetPending
 * @see natsSubscription_GetMaxPending
 * @see natsSubscription_GetDelivered
 * @see natsSubscription_GetDropped
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param pendingMsgs if not `NULL`, memory location where to store the
 * number of pending messages.
 * @param pendingBytes if not `NULL`, memory location where to store the
 * total size of pending messages.
 * @param maxPendingMsgs if not `NULL`, memory location where to store the
 * maximum number of pending messages seen so far.
 * @param maxPendingBytes if not `NULL`, memory location where to store the
 * maximum total size of pending messages seen so far.
 * @param deliveredMsgs if not `NULL`, memory location where to store the
 * number of delivered messages.
 * @param droppedMsgs if not `NULL`, memory location where to store the
 * number of dropped messages.
 */
NATS_EXTERN natsStatus
natsSubscription_GetStats(natsSubscription *sub,
                          int     *pendingMsgs,
                          int     *pendingBytes,
                          int     *maxPendingMsgs,
                          int     *maxPendingBytes,
                          int64_t *deliveredMsgs,
                          int64_t *droppedMsgs);

/** \brief Checks the validity of the subscription.
 *
 * Returns a boolean indicating whether the subscription is still active.
 * This will return false if the subscription has already been closed,
 * or auto unsubscribed.
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN bool
natsSubscription_IsValid(natsSubscription *sub);

/** \brief Drains the subscription with a default timeout.
 *
 * Drain will remove interest but continue to invoke callbacks until all messages
 * have been processed.
 *
 * This call uses a default drain timeout of 30 seconds. See #natsSubscription_DrainTimeout
 * for details on behavior when timeout elapses.
 *
 * \warning This function does not block waiting for the operation
 * to complete. To synchronously wait, see #natsSubscription_WaitForDrainCompletion
 *
 * @see natsSubscription_DrainTimeout
 * @see natsSubscription_WaitForDrainCompletion
 * @see natsSubscription_DrainCompletionStatus
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN natsStatus
natsSubscription_Drain(natsSubscription *sub);

/** \brief Drains the subscription with the specified timeout.
 *
 * Drain will remove interest but continue to invoke callbacks until all messages
 * have been processed, or the specified timeout has elapsed. In that case, the
 * subscription will be forcibly closed and remaining pending messages (if any)
 * will not be processed.
 *
 * The timeout is expressed in milliseconds. Zero or negative value means that
 * the call will not timeout, but see below for more details.
 *
 * When this call returns, the UNSUBSCRIBE protocol for this subscription has
 * been enqueued to the outgoing connection buffer, but not sent to the server,
 * ensuring that this call does not block.
 *
 * The library then asynchronously ensures that this protocol is sent and waits
 * for a confirmation from the server. After that, it is guaranteed that no
 * new message for this subscription will be received and the library can proceed
 * with the rest of the draining.
 *
 * However, should the "flush" of the protocol fail, the library will ensure that
 * no new message is added to the subscription (in the event the server did not
 * receive the UNSUBSCRIBE protocol and still attempts to deliver messages), and
 * will proceed with the draining of the pending messages. Users can check the
 * status of the draining after it has completed by calling #natsSubscription_DrainCompletionStatus.
 *
 * If no timeout is specified (that is, value is zero or negative), a timeout
 * will be used for the "flush" of the protocol. Again, even in case of failure,
 * the draining will proceed.
 *
 * If a timeout is specified, the complete process: "flush" of the protocol
 * and draining of messages, must happen before the timeout elapses otherwise the
 * subscription will be forcibly closed, and not all message callbacks may be invoked.
 *
 * Regardless of the presence of a timeout or not, should the subscription or connection
 * be closed while draining occurs, the draining process will stop. The
 * #natsSubscription_WaitForDrainCompletion call will not report an error. To
 * know if an error occurred, the user can call #natsSubscription_DrainCompletionStatus
 * after ensuring that the drain has completed.
 *
 * \warning This function does not block waiting for the operation
 * to complete. To synchronously wait, see #natsSubscription_WaitForDrainCompletion
 *
 * @see natsSubscription_Drain
 * @see natsSubscription_WaitForDrainCompletion
 * @see natsSubscription_DrainCompletionStatus
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param timeout how long to wait for the operation to complete, expressed in
 * milliseconds. If the timeout elapses the subscription will be closed.
 */
NATS_EXTERN natsStatus
natsSubscription_DrainTimeout(natsSubscription *sub, int64_t timeout);

/** \brief Blocks until the drain operation completes.
 *
 * This function blocks until the subscription is fully drained.
 * Returns no error if the subscription is drained or closed, otherwise
 * returns the error if the subscription was not in drained mode (#NATS_ILLEGAL_STATE)
 * or if this subscription was not drained or closed prior to the specified
 * timeout (#NATS_TIMEOUT).
 *
 * The timeout is expressed in milliseconds. Zero or negative value
 * means that the call will not timeout.
 *
 * Note that if this call times-out, it does not mean that the drain stops.
 * The drain will continue until its own timeout elapses.
 *
 * @see natsSubscription_Drain
 * @see natsSubscription_DrainTimeout
 * @see natsSubscription_DrainCompletionStatus
 *
 * @param sub the pointer to the #natsSubscription object.
 * @param timeout how long to wait for the operation to complete, expressed in
 * milliseconds.
 */
NATS_EXTERN natsStatus
natsSubscription_WaitForDrainCompletion(natsSubscription *sub, int64_t timeout);

/** \brief Returns the status of the drain after completion.
 *
 * Once the drain has completed, users can use this function to know if the
 * drain completed successfully or not.
 *
 * Possible return values (the list is not exhaustive):
 *
 * <c>NATS_OK</c> the library sent the UNSUBSCRIBE protocol and finished processing
 * all messages that were pending.<br>
 * <c>NATS_ILLEGAL_STATE</c> this call was made for a subscription that had not
 * started draining or the draining is still in progress.<br>
 * <c>NATS_INVALID_SUBSCRIPTION</c> the subscription was closed while draining,
 * which means that some messages may not have been processed.<br>
 * <c>NATS_CONNECTION_CLOSED</c> the connection was closed while draining, which
 * means that some messages may not have been processed.
 *
 * \note This call does not wait for the drain completion (see #natsSubscription_WaitForDrainCompletion
 * for that).
 *
 * @see natsSubscription_Drain
 * @see natsSubscription_DrainTimeout
 * @see natsSubscription_WaitForDrainCompletion
 *
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN natsStatus
natsSubscription_DrainCompletionStatus(natsSubscription *sub);

/** \brief Sets a completion callback.
 *
 * In order to make sure that an asynchronous subscription's message handler is no longer invoked once the
 * subscription is closed (#natsSubscription_Unsubscribe), the subscription should be closed from the
 * message handler itslef.
 *
 * If the application closes the subscription from a different thread and immediately frees resources
 * needed in the message handler, there is a risk of a crash since the subscription's message handler
 * may still be invoked one last time or already in the process of executing.
 *
 * To address this, the user can set a callback that will be invoked after the subscription is closed and the
 * message handler has returned. This applies to asynchronous subscriptions using their own dispatcher or using
 * the library's delivery thread pool.
 *
 * \note You don't need to call this function if you are not freeing resources needed in the message handler or if
 * you always close the subscription from the message handler itself.
 *
 * \note If you plan on calling this function, you should do so before calling #natsSubscription_AutoUnsubscribe, since
 * there is a risk that the subscription be removed as soon as #natsSubscription_AutoUnsubscribe returns.
 *
 * Calling this function on a synchronous or closed subscription will return #NATS_INVALID_SUBSCRIPTION.
 *
 * @see natsOnCompleteCB
 *
 * @param sub the pointer to the #natsSubscription object
 * @param cb the callback to invoke when the last message of a closed subscription has been dispatched
 * @param closure the pointer to a user defined object (possibly `NULL`) that will be passed to the callback
 */
NATS_EXTERN natsStatus
natsSubscription_SetOnCompleteCB(natsSubscription *sub, natsOnCompleteCB cb, void *closure);

/** \brief Destroys the subscription.
 *
 * Destroys the subscription object, freeing up memory.
 * If not already done, this call will removes interest on the subject.
 *
 * @param sub the pointer to the #natsSubscription object to destroy.
 */
NATS_EXTERN void
natsSubscription_Destroy(natsSubscription *sub);

/** @} */ // end of subGroup

#if defined(NATS_HAS_STREAMING)
/** \defgroup stanConnGroup Streaming Connection
 *
 * NATS Streaming Connection
 * @{
 */

/** \defgroup stanConnMgtGroup Management
 *
 *  Functions related to connection management.
 *  @{
 */

/** \brief Connects to a `NATS Streaming Server` using the provided options.
 *
 * Attempts to connect to a `NATS Streaming Server`.
 *
 * This call is cloning the #stanConnOptions object, if given. Once this call returns,
 * changes made to the `options` will not have an effect to this connection.
 * The `options` can however be changed prior to be passed to another
 * #stanConnection_Connect() call if desired.
 *
 * \note The Streaming connection does not honor the NATS Connection option
 * #natsOptions_SetRetryOnFailedConnect(). If you pass NATS Options with
 * this option enabled, no error is returned, but if the connection cannot
 * be established "right away", the connect call will return an error.
 *
 * \warning If connecting to a NATS Server v2.2.0+ and there is no Streaming server
 * listening on the connect request subject, this call will return #NATS_NO_RESPONDERS,
 * not #NATS_TIMEOUT.
 *
 * @see #stanConnOptions
 * @see #stanConnection_Destroy()
 *
 * @param sc the location where to store the pointer to the newly created
 * #natsConnection object.
 * @param clusterID the name of the cluster this connection is for.
 * @param clientID the client ID for this connection. Only one connection
 * with this ID will be accepted by the server. Use only a-zA-Z0-9_- characters.
 * @param options the options to use for this connection (can be `NULL`).
 */
NATS_EXTERN natsStatus
stanConnection_Connect(stanConnection **sc, const char *clusterID, const char *clientID,
                       stanConnOptions *options);

/** \brief Returns the underlying NATS Connection.
 *
 * This can be used if the application needs to do non streaming messaging
 * but does not want to create a separate NATS Connection.
 *
 * Obtain a NATS connection from a NATS streaming connection. The NATS
 * connection can be used to perform regular NATS operations, but it is owned
 * and managed by the NATS streaming connection. It cannot be closed,
 * which will happen when the NATS streaming connection is closed.
 *
 * \note For each call to this function, the user must call
 * #stanConnection_ReleaseNATSConnection() when access to the NATS Connection
 * is no longer needed.
 *
 * \warning The returned connection cannot be closed, drained nor destroyed.
 * Calling corresponding functions will have no effect or return #NATS_ILLEGAL_STATE.
 *
 * @see stanConnection_ReleaseNATSConnection()
 *
 * @param sc the pointer to the #stanConnection object.
 * @param nc the location where to store the pointer of the #natsConnection object.
 */
NATS_EXTERN natsStatus
stanConnection_GetNATSConnection(stanConnection *sc, natsConnection **nc);

/** \brief Releases the NATS Connection.
 *
 * This should be paired with the #stanConnection_GetNATSConnection() call.
 * That is, after getting a reference to the underlying NATS Connection and
 * once that connection is no longer needed, calling this function will
 * allow resources to be properly released when the streaming connection is destroyed.
 *
 * You would normally call #stanConnection_GetNATSConnection() and this function
 * only once.
 *
 * After the last #stanConnection_ReleaseNATSConnection() call is made, you
 * must no longer use the NATS Connection because if #stanConnection_Destroy()
 * is called, that could make the pointer to the NATS Connection invalid.
 *
 * \note If the streaming connection is closed/destroyed before the last
 * call to #stanConnection_ReleaseNATSConnection, the pointer to the NATS
 * connection will still be valid, although all calls will fail since the
 * connection is now closed. Calling this function will release the streaming
 * object allowing memory to be freed.
 *
 * @see stanConnection_GetNATSConnection
 *
 * @param sc the pointer to the #stanConnection object.
 */
NATS_EXTERN void
stanConnection_ReleaseNATSConnection(stanConnection *sc);

/** \brief Closes the connection.
 *
 * Closes the connection to the server. This call will release all blocking
 * calls. The connection object is still usable until the call to
 * #stanConnection_Destroy().
 *
 * \warning See warning about connecting to a NATS Server v2.2.0+ in #stanConnection_Connect().
 *
 * @param sc the pointer to the #stanConnection object.
 */
NATS_EXTERN natsStatus
stanConnection_Close(stanConnection *sc);

/** \brief Destroys the connection object.
 *
 * Destroys the connection object, freeing up memory.
 * If not already done, this call first closes the connection to the server.
 *
 * @param sc the pointer to the #stanConnection object.
 */
NATS_EXTERN natsStatus
stanConnection_Destroy(stanConnection *sc);

/** @} */ // end of stanConnMgtGroup

/** \defgroup stanConnPubGroup Publishing
 *
 *  Publishing functions
 *  @{
 */

/** \brief Publishes data on a channel.
 *
 * Publishes the data argument to the given channel. The data argument is left
 * untouched and needs to be correctly interpreted on the receiver.
 *
 * @param sc the pointer to the #stanConnection object.
 * @param channel the channel name the data is sent to.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 */
NATS_EXTERN natsStatus
stanConnection_Publish(stanConnection *sc, const char *channel,
                       const void *data, int dataLen);

/** \brief Asynchronously publishes data on a channel.
 *
 * Publishes the data argument to the given channel. The data argument is left
 * untouched and needs to be correctly interpreted on the receiver.
 *
 * This function does not wait for an acknowledgment back from the server.
 * Instead, the library will invoke the provided callback when that acknowledgment
 * comes.
 *
 * In order to correlate the acknowledgment with the published message, you
 * can use the `ahClosure` since this will be passed to the #stanPubAckHandler
 * on every invocation. In other words, you should use a unique closure for
 * each published message.
 *
 * @param sc the pointer to the #natsConnection object.
 * @param channel the channel name the data is sent to.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 * @param ah the publish acknowledgment callback. If `NULL` the user will not
 * be notified of the publish result.
 * @param ahClosure the closure the library will pass to the `ah` callback if
 * one has been set.
 */
NATS_EXTERN natsStatus
stanConnection_PublishAsync(stanConnection *sc, const char *channel,
                            const void *data, int dataLen,
                            stanPubAckHandler ah, void *ahClosure);

/** @} */ // end of stanConnPubGroup

/** \defgroup stanConnSubGroup Subscribing
 *
 *  Subscribing functions.
 *  @{
 */

/** \brief Creates a subscription.
 *
 * Expresses interest in the given subject. The subject can NOT have wildcards.
 * Messages will be delivered to the associated #stanMsgHandler.
 *
 * \warning See warning about connecting to a NATS Server v2.2.0+ in #stanConnection_Connect().
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param sc the pointer to the #natsConnection object.
 * @param channel the channel this subscription is created for.
 * @param cb the #stanMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #stanMsgHandler prototype.
 * @param options the optional to further configure the subscription.
 */
NATS_EXTERN natsStatus
stanConnection_Subscribe(stanSubscription **sub, stanConnection *sc,
                         const char *channel, stanMsgHandler cb,
                         void *cbClosure, stanSubOptions *options);

/** \brief Creates a queue subscription.
 *
 * Creates a queue subscriber on the given channel.
 * All subscribers with the same queue name will form the queue group and
 * only one member of the group will be selected to receive any given
 * message asynchronously.
 *
 * \warning See warning about connecting to a NATS Server v2.2.0+ in #stanConnection_Connect().
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param sc the pointer to the #natsConnection object.
 * @param channel the channel name this subscription is created for.
 * @param queueGroup the name of the group.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 * @param options the optional options to further configure this queue subscription.
 */
NATS_EXTERN natsStatus
stanConnection_QueueSubscribe(stanSubscription **sub, stanConnection *sc,
                              const char *channel, const char *queueGroup,
                              stanMsgHandler cb, void *cbClosure, stanSubOptions *options);

/** @} */ // end of stanConnSubGroup

/** @} */ // end of stanConnGroup

/** \defgroup stanSubGroup Streaming Subscription
 *
 *  NATS Streaming Subscriptions.
 *  @{
 */

/** \brief Sets a completion callback.
 *
 * In order to make sure that an asynchronous subscription's message handler is no longer invoked once the
 * subscription is closed (or unsubscribed) (#stanSubscription_Close, #stanSubscription_Unsubscribe), the
 * subscription should be closed from the message handler itslef.
 *
 * If the application closes the subscription from a different thread and immediately frees resources
 * needed in the message handler, there is a risk of a crash since the subscription's message handler
 * may still be invoked one last time or already in the process of executing.
 *
 * To address this, the user can set a callback that will be invoked after the subscription is closed and the
 * message handler has returned.
 *
 * \note You don't need to call this function if you are not freeing resources needed in the message handler or if
 * you always close the subscription from the message handler itself.
 *
 * @see natsOnCompleteCB
 *
 * @param sub the pointer to the #stanSubscription object
 * @param cb the callback to invoke when the last message of a closed subscription has been dispatched
 * @param closure the pointer to a user defined object (possibly `NULL`) that will be passed to the callback
 */
NATS_EXTERN natsStatus
stanSubscription_SetOnCompleteCB(stanSubscription *sub, natsOnCompleteCB cb, void *closure);

/** \brief Acknowledge a message.
 *
 * If the subscription is created with manual acknowledgment mode (see #stanSubOptions_SetManualAckMode)
 * then it is the user responsibility to acknowledge received messages when
 * appropriate.
 *
 * @param sub the pointer to the #stanSubscription object.
 * @param msg the message to acknowledge.
 */
NATS_EXTERN natsStatus
stanSubscription_AckMsg(stanSubscription *sub, stanMsg *msg);

/** \brief Permanently remove a subscription.
 *
 * Removes interest on the channel. The subscription may still have
 * a callback in progress, in that case, the subscription will still be valid
 * until the callback returns.
 *
 * For non-durable subscriptions, #stanSubscription_Unsubscribe and #stanSubscription_Close
 * have the same effect.
 *
 * For durable subscriptions, calling this function causes the server
 * to remove the durable subscription (instead of simply suspending it).
 * It means that once this call is made, calling #stanConnection_Subscribe() with
 * the same durable name creates a brand new durable subscription, instead
 * of simply resuming delivery.
 *
 * \warning See warning about connecting to a NATS Server v2.2.0+ in #stanConnection_Connect().
 *
 * @param sub the pointer to the #stanSubscription object.
 */
NATS_EXTERN natsStatus
stanSubscription_Unsubscribe(stanSubscription *sub);

/** \brief Closes the subscription.
 *
 * Similar to #stanSubscription_Unsubscribe() except that durable interest
 * is not removed in the server. The durable subscription can therefore be
 * resumed.
 *
 * \warning See warning about connecting to a NATS Server v2.2.0+ in #stanConnection_Connect().
 *
 * @param sub the pointer to the #stanSubscription object.
 */
NATS_EXTERN natsStatus
stanSubscription_Close(stanSubscription *sub);

/** \brief Destroys the subscription.
 *
 * Destroys the subscription object, freeing up memory.
 * If not already done, this call will removes interest on the subject.
 *
 * @param sub the pointer to the #stanSubscription object to destroy.
 */
NATS_EXTERN void
stanSubscription_Destroy(stanSubscription *sub);

/** @} */ // end of stanSubGroup
#endif

/** \defgroup jsGroup JetStream
 *
 *  JetStream.
 *  @{
 */

/** \brief Initializes a streaming context options structure.
 *
 * Use this before setting specific stream context options and passing it
 * to JetStream APIs.
 *
 * @param opts the pointer to the #jsOptions to initialize.
 */
NATS_EXTERN natsStatus
jsOptions_Init(jsOptions *opts);

/** \brief Returns a new JetStream context.
 *
 * A JetStream context is used for messaging and assets management.
 *
 * Since the underlying NATS connection is used for communication, the NATS connection
 * should stay valid while using the JetStream context. That is, do not close/destroy
 * the NATS connection before destroying the JetStream context.
 *
 * \note When done, the context should be destroyed to release memory.
 *
 * @param js the location where to store the pointer to the newly created #jsCtx object.
 * @param nc the pointer to the #natsConnection object from which to get the JetStream context.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsConnection_JetStream(jsCtx **js, natsConnection *nc, jsOptions *opts);

/** \brief Destroys the JetStream context.
 *
 * Releases memory used by the context object.
 *
 * @param js the pointer to the #jsCtx object to destroy.
 */
NATS_EXTERN void
jsCtx_Destroy(jsCtx *js);

/** \defgroup jsAssetsGroup JetStream Assets Management
 *
 *  JetStream Assets Management
 *  @{
 */

/** \brief Initializes a streaming configuration structure.
 *
 * Use this before setting specific stream configuration options and passing this
 * configuration to some of the stream management APIs.
 *
 * @param cfg the pointer to the #jsStreamConfig to initialize.
 */
NATS_EXTERN natsStatus
jsStreamConfig_Init(jsStreamConfig *cfg);

/** \brief Initializes a placement configuration structure.
 *
 * Use this before setting specific stream placement options.
 *
 * @param placement the pointer to the #jsPlacement to initialize.
 */
NATS_EXTERN natsStatus
jsPlacement_Init(jsPlacement *placement);

/** \brief Initializes a stream source configuration structure.
 *
 * Use this before setting specific stream source options.
 *
 * @param source the pointer to the #jsStreamSource to initialize.
 */
NATS_EXTERN natsStatus
jsStreamSource_Init(jsStreamSource *source);

/** \brief Initializes an external stream configuration structure.
 *
 * Use this before setting specific external stream options.
 *
 * @param external the pointer to the #jsExternalStream to initialize.
 */
NATS_EXTERN natsStatus
jsExternalStream_Init(jsExternalStream *external);

/** \brief Initializes a republish structure.
 *
 * Use this to set the source, destination and/or headers only for a stream re-publish.
 *
 * @param rp the pointer to the #jsRePublish to initialize.
 */
NATS_EXTERN natsStatus
jsRePublish_Init(jsRePublish *rp);

/** \brief Creates a stream.
 *
 * Creates a stream based on the provided configuration (that cannot be `NULL`).
 * The name is mandatory and cannot contain <c>.</c> characters.
 *
 * \note If you do not need a #jsStreamInfo to be returned, you can pass `NULL`,
 * otherwise, on success you are responsible for freeing this object.
 *
 * @see jsStreamConfig_Init
 * @see jsStreamInfo_Destroy
 *
 * @param si the location where to store the pointer to the new #jsStreamInfo object in
 * response to the creation request, or `NULL` if the stream information is not needed.
 * @param js the pointer to the #jsCtx context.
 * @param cfg the pointer to the #jsStreamConfig.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_AddStream(jsStreamInfo **si, jsCtx *js, jsStreamConfig *cfg, jsOptions *opts, jsErrCode *errCode);

/** \brief Updates a stream.
 *
 * Updates a stream based on the provided configuration (that cannot be `NULL`).
 * The name is mandatory and cannot contain <c>.</c> characters.
 *
 * \note If you do not need a #jsStreamInfo to be returned, you can pass `NULL`,
 * otherwise, on success you are responsible for freeing this object.
 *
 * @see jsStreamConfig_Init
 * @see jsStreamInfo_Destroy
 *
 * @param si the location where to store the pointer to the new #jsStreamInfo object in
 * response to the creation request, or `NULL` if the stream information is not needed.
 * @param js the pointer to the #jsCtx context.
 * @param cfg the pointer to the #jsStreamConfig.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_UpdateStream(jsStreamInfo **si, jsCtx *js, jsStreamConfig *cfg, jsOptions *opts, jsErrCode *errCode);

/** \brief Purges a stream.
 *
 * Purges the stream named <c>stream</c>.
 *
 * For more advanced purge options, you can specify them through #jsOptions.
 *
 * \code{.unparsed}
 * jsOptions        o;
 *
 * jsOptions_Init(&o);
 * o.Stream.Purge.Subject  = "foo";
 * o.Stream.Purge.Sequence = 4;
 *
 * js_PurgeStream(js, "MY_STREAM", &o, &jerr);
 * \endcode
 *
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream to purge.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_PurgeStream(jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode);

/** \brief Deletes a stream.
 *
 * Deletes the stream named <c>stream</c>.
 *
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream to delete.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_DeleteStream(jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode);

/** \brief Retrieves a JetStream message from the stream by sequence.
 *
 * Retrieves a raw stream message stored in JetStream by sequence number.
 *
 * \note The message needs to be destroyed by calling #natsMsg_Destroy.
 *
 * @see js_GetLastMsg
 * @see natsMsg_Destroy
 *
 * @param msg the memory location where the library will store the pointer to the #natsMsg.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param seq the sequence in the stream of the message being retrieved.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_GetMsg(natsMsg **msg, jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode);

/** \brief Retrieves the last JetStream message from the stream for a given subject.
 *
 * Retrieves the last JetStream message from the stream for a given subject.
 *
 * \note The message needs to be destroyed by calling #natsMsg_Destroy.
 *
 * @see js_GetMsg
 * @see natsMsg_Destroy
 *
 * @param msg the memory location where the library will store the pointer to the #natsMsg.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param subject the subject for which the last message is being retrieved.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_GetLastMsg(natsMsg **msg, jsCtx *js, const char *stream, const char *subject, jsOptions *opts, jsErrCode *errCode);

/** \brief Initializes a direct get message options structure.
 *
 * Use this before setting specific direct get message options and passing it
 * to #js_DirectGetMsg API.
 *
 * @param opts the pointer to the #jsDirectGetMsgOptions object.
 */
NATS_EXTERN natsStatus
jsDirectGetMsgOptions_Init(jsDirectGetMsgOptions *opts);

/** \brief Retrieves directly a JetStream message based on provided options.
 *
 * If a stream is created with `AllowDirect`, it is possible to retrieve a message
 * without going through the leader.
 *
 * To specify the options, call #jsDirectGetMsgOptions_Init first and the set
 * the appropriate options, then invoke this function.
 *
 * \note Some options are mutually exclusive but the library is not doing the
 * check and leave it to the server to do it and return the error returned by
 * the server.
 *
 * \note This API can only be used against servers that support the direct
 * get feature, which is `v2.9.0+`. If running against an older server the
 * call will likely timeout.
 *
 * @param msg the location where to store the pointer to the retrieved message.
 * @param js  the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param dgOpts the pointer to the #jsDirectGetMsgOptions object, cannot be `NULL`.
 */
NATS_EXTERN natsStatus
js_DirectGetMsg(natsMsg **msg, jsCtx *js, const char *stream, jsOptions *opts, jsDirectGetMsgOptions *dgOpts);

/** \brief Deletes a message from the stream.
 *
 * Deletes the message at sequence <c>seq</c> in the stream named <c>stream</c>.
 *
 * \note To completely erase the content of the deleted message when stored on disk,
 * use #js_EraseMsg instead.
 *
 * @see js_EraseMsg
 *
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param seq the sequence in the stream of the message to delete.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_DeleteMsg(jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode);

/** \brief Erases a message from the stream.
 *
 * Similar to #js_DeleteMsg except that the content of the deleted message is
 * erased from stable storage.
 *
 * @see js_DeleteMsg
 *
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param seq the sequence in the stream of the message to erase.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_EraseMsg(jsCtx *js, const char *stream, uint64_t seq, jsOptions *opts, jsErrCode *errCode);

/** \brief Retreives information from a stream.
 *
 * Returns information about the stream named <c>stream</c>.
 *
 * \note You need to free the returned object.
 *
 * To get some detailed information about deleted messages, set this option:
 *
 * \code{.unparsed}
 * jsOptions                o;
 *
 * jsOptions_Init(&o);
 * o.Stream.Info.DeletedDetails = true;
 * js_GetStreamInfo(&si, js, "MY_STREAM", &o, &jerr);
 * \endcode
 *
 * @see jsStreamInfo_Destroy
 *
 * @param si the location where to store the pointer to the new #jsStreamInfo object in
 * response to the creation request.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream which information is retrieved.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_GetStreamInfo(jsStreamInfo **si, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the stream information object.
 *
 * Releases memory allocated for this stream information object.
 *
 * @param si the pointer to the #jsStreamInfo object.
 */
NATS_EXTERN void
jsStreamInfo_Destroy(jsStreamInfo *si);

/** \brief Retrieves the list of all available streams.
 *
 * Retrieves the list of all #jsStreamInfo. It is possible to filter
 * which streams are to be retrieved based on a subject filter.
 *
 * \warning The list should be destroyed when no longer used by
 * calling #jsStreamInfoList_Destroy.
 *
 * @param list the location where to store the pointer to the new #jsStreamInfoList object.
 * @param js the pointer to the #jsCtx context.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_Streams(jsStreamInfoList **list, jsCtx *js, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the stream information list object.
 *
 * Releases memory allocated for this stream information list.
 *
 * \warning All #jsStreamInfo pointers contained in the list will
 * be destroyed by this call.
 *
 * @param list the pointer to the #jsStreamInfoList object.
 */
NATS_EXTERN void
jsStreamInfoList_Destroy(jsStreamInfoList *list);

/** \brief Retrieves the list of all available stream names.
 *
 * Retrieves the list of all stream names. It is possible to filter
 * which streams are to be retrieved based on a subject filter.
 *
 * \warning The list should be destroyed when no longer used by
 * calling #jsStreamNamesList_Destroy.
 *
 * @param list the location where to store the pointer to the new #jsStreamNamesList object.
 * @param js the pointer to the #jsCtx context.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_StreamNames(jsStreamNamesList **list, jsCtx *js, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the stream names list object.
 *
 * Releases memory allocated for this list of stream names.
 *
 * \warning All string pointers contained in the list will
 * be destroyed by this call.
 *
 * @param list the pointer to the #jsStreamNamesList object.
 */
NATS_EXTERN void
jsStreamNamesList_Destroy(jsStreamNamesList *list);

/** \brief Initializes a consumer configuration structure.
 *
 * Use this before adding a consumer.
 *
 * @see #jsConsumerConfig
 *
 * @param cc the pointer to the #jsConsumerConfig to initialize.
 */
NATS_EXTERN natsStatus
jsConsumerConfig_Init(jsConsumerConfig *cc);

/** \brief Adds a JetStream consumer.
 *
 * Adds a consumer based on the provided configuration (that cannot be `NULL`).
 *
 * \note If you do not need a #jsConsumerInfo to be returned, you can pass `NULL`,
 * otherwise, on success you are responsible for freeing this object.
 *
 * @see jsConsumerConfig_Init
 * @see jsConsumerInfo_Destroy
 *
 * @param ci the location where to store the pointer to the new #jsConsumerInfo object in
 * response to the creation request, or `NULL` if the consumer information is not needed.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param cfg the pointer to the #jsConsumerConfig.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_AddConsumer(jsConsumerInfo **ci, jsCtx *js,
                   const char *stream, jsConsumerConfig *cfg,
                   jsOptions *opts, jsErrCode *errCode);

/** \brief Updates a JetStream consumer.
 *
 * Updates a consumer based on the provided configuration (that cannot be `NULL`).
 *
 * \note If you do not need a #jsConsumerInfo to be returned, you can pass `NULL`,
 * otherwise, on success you are responsible for freeing this object.
 *
 * @see jsConsumerConfig_Init
 * @see jsConsumerInfo_Destroy
 *
 * @param ci the location where to store the pointer to the new #jsConsumerInfo object in
 * response to the creation request, or `NULL` if the consumer information is not needed.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param cfg the pointer to the #jsConsumerConfig.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_UpdateConsumer(jsConsumerInfo **ci, jsCtx *js,
                  const char *stream, jsConsumerConfig *cfg,
                  jsOptions *opts, jsErrCode *errCode);

/** \brief Retrieves information about a consumer.
 *
 * \note The returned object should be destroyed using #jsConsumerInfo_Destroy in order
 * to free allocated memory.
 *
 * @param ci the location where to store the pointer to the new #jsConsumerInfo object.
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param consumer the name of the consumer.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_GetConsumerInfo(jsConsumerInfo **ci, jsCtx *js,
                       const char *stream, const char *consumer,
                       jsOptions *opts, jsErrCode *errCode);

/** \brief Deletes a consumer.
 *
 * Deletes the consumer named <c>consumer</c> from stream named <c>stream</c>.
 *
 * @param js the pointer to the #jsCtx context.
 * @param stream the name of the stream.
 * @param consumer the name of the consumer.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_DeleteConsumer(jsCtx *js, const char *stream, const char *consumer,
                      jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the consumer information object.
 *
 * Releases memory allocated for this consumer information object.
 *
 * @param ci the pointer to the #jsConsumerInfo object.
 */
NATS_EXTERN void
jsConsumerInfo_Destroy(jsConsumerInfo *ci);

/** \brief Retrieves the list of all available consumers for a stream.
 *
 * Retrieves the list of all #jsConsumerInfo for a given stream.
 *
 * \warning The list should be destroyed when no longer used by
 * calling #jsConsumerInfoList_Destroy.
 *
 * @param list the location where to store the pointer to the new #jsConsumerInfoList object.
 * @param js the pointer to the #jsCtx context.
 * @param stream the stream name whose consumer list is requested.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_Consumers(jsConsumerInfoList **list, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the consumer information list object.
 *
 * Releases memory allocated for this consumer information list.
 *
 * \warning All #jsConsumerInfo pointers contained in the list will
 * be destroyed by this call.
 *
 * @param list the pointer to the #jsConsumerInfoList object.
 */
NATS_EXTERN void
jsConsumerInfoList_Destroy(jsConsumerInfoList *list);

/** \brief Retrieves the list of all available consumer names for a stream.
 *
 * Retrieves the list of all consumer names for a given stream.
 *
 * \warning The list should be destroyed when no longer used by
 * calling #jsConsumerNamesList_Destroy.
 *
 * @param list the location where to store the pointer to the new #jsConsumerNamesList object.
 * @param js the pointer to the #jsCtx context.
 * @param stream the stream name whose consumer list is requested.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_ConsumerNames(jsConsumerNamesList **list, jsCtx *js, const char *stream, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the consumer names list object.
 *
 * Releases memory allocated for this list of consumer names.
 *
 * \warning All string pointers contained in the list will
 * be destroyed by this call.
 *
 * @param list the pointer to the #jsConsumerNamesList object.
 */
NATS_EXTERN void
jsConsumerNamesList_Destroy(jsConsumerNamesList *list);

/** \brief Retrieves information about the JetStream usage from an account.
 *
 * Retrieves information about the JetStream usage from an account.
 *
 * \note The returned object should be destroyed using #jsAccountInfo_Destroy in order
 * to free allocated memory.
 *
 * @param ai the location where to store the pointer to the new #jsAccountInfo object in
 * response to the account information request.
 * @param js the pointer to the #jsCtx context.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_GetAccountInfo(jsAccountInfo **ai, jsCtx *js, jsOptions *opts, jsErrCode *errCode);

/** \brief Destroys the account information object.
 *
 * Releases memory allocated for this account information object.
 *
 * @param ai the pointer to the #jsAccountInfo object.
 */
NATS_EXTERN void
jsAccountInfo_Destroy(jsAccountInfo *ai);

/** @} */ // end of jsAssetsGroup

/** \defgroup jsPubGroup Publishing
 *
 *  Publishing functions
 *  @{
 */

/** \brief Initializes a publish options structure.
 *
 * Use this before setting specific publish options and passing this
 * configuration to the JetStream publish APIs.
 *
 * @param opts the pointer to the #jsPubOptions to initialize.
 */
NATS_EXTERN natsStatus
jsPubOptions_Init(jsPubOptions *opts);

/** \brief Publishes data on a subject to JetStream.
 *
 * Publishes the data to the given subject to JetStream.
 *
 * See #js_PublishMsg for details.
 *
 * @param pubAck the location where to store the pub acknowledgment, or `NULL` if not needed.
 * @param js the pointer to the #jsCtx object.
 * @param subj the subject the data is sent to.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 * @param opts the publish options, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_Publish(jsPubAck **pubAck, jsCtx *js, const char *subj, const void *data, int dataLen,
           jsPubOptions *opts, jsErrCode *errCode);

/** \brief Publishes a message to JetStream.
 *
 * Publishes the given message to JetStream.
 *
 * \note If you are not interested in inspecting the publish acknowledgment, you can
 * pass `NULL`, but keep in mind that the publish acknowledgment is still sent by
 * the server.
 * \note The returned #jsPubAck object needs to be destroyed with #jsPubAck_Destroy
 * when no longer needed.
 *
 * @see jsPubAck_Destroy
 *
 * @param pubAck the location where to store the pub acknowledgment, or `NULL` if not needed.
 * @param js the pointer to the #jsCtx object.
 * @param msg the pointer to the #natsMsg object to send.
 * @param opts the publish options, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_PublishMsg(jsPubAck **pubAck, jsCtx *js, natsMsg *msg,
              jsPubOptions *opts, jsErrCode *errCode);

/** \brief Destroys the publish acknowledgment object.
 *
 * Releases memory allocated for this publish acknowledgment object.
 *
 * @param pubAck the #jsPubAck object to destroy.
 */
NATS_EXTERN void
jsPubAck_Destroy(jsPubAck *pubAck);

/** \brief Publishes data to JetStream but does not wait for a #jsPubAck.
 *
 * See #js_PublishMsgAsync for details.
 *
 * @param js the pointer to the #jsCtx object.
 * @param subj the subject the data is sent to.
 * @param data the data to be sent, can be `NULL`.
 * @param dataLen the length of the data to be sent.
 * @param opts the publish options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
js_PublishAsync(jsCtx *js, const char *subj, const void *data, int dataLen,
                jsPubOptions *opts);

/** \brief Publishes a message to JetStream but does not wait for a #jsPubAck.
 *
 * Publishes a message asynchronously to JetStream. User can call #js_PublishAsyncComplete
 * to be notified when all publish acknowledgments for the pending publish calls
 * have been received.
 *
 * \note If this call is successful, the library takes ownership of the message
 * and will destroy it after the acknowledgment has been received, or will
 * present it to the user through the #jsPubAckErrHandler callback. To prevent the user
 * from accessing/destroying the message while in use by the library, this function
 * requires a pointer to the pointer of the message so that it can be cleared.
 * That way, the user should always call #natsMsg_Destroy, regardless of success or
 * failure, since #natsMsg_Destroy will have no effect if the message pointer is `NULL`.
 *
 * @see js_PublishAsyncComplete
 * @see jsPubAckErrHandler
 *
 * @param js the pointer to the #jsCtx object.
 * @param msg the memory location where the pointer to the #natsMsg object is located.
 * If the library takes ownership of the message, this location will be cleared so a following
 * call to #natsMsg_Destroy would have no effect.
 * @param opts the publish options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
js_PublishMsgAsync(jsCtx *js, natsMsg **msg, jsPubOptions *opts);

/** \brief Wait for all outstanding messages to be acknowledged.
 *
 * This call will block until the library has received acknowledgment for
 * all outstanding published messages.
 *
 * To limit the wait, user can pass a #jsPubOptions with a `MaxWait` set to the
 * maximum number of milliseconds that the call should block.
 *
 * @param js the pointer to the #jsCtx object.
 * @param opts the publish options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
js_PublishAsyncComplete(jsCtx *js, jsPubOptions *opts);

/** \brief Returns the list of pending messages published asynchronously.
 *
 * This call returns the list of all asynchronously published messages
 * for which no acknowledgment have been received yet.
 *
 * The user has now back ownership of the messages and can resend send if
 * desired or simply destroy them.
 *
 * \note After this call returns, it is possible that acknowledgments arrive
 * from the server but since they have been removed from the pending list, the
 * acknowledgments will be discarded (no #jsPubAckErrHandler callback invoked).
 * If the server did receive a particular message and the user in the meantime
 * has resent that message, it would be a duplicate, so in order for the server
 * to detect this duplicate, ensure that the stream's duplicate window setting
 * is specified and a unique message ID was set when sending the message.
 *
 * \warning The user must call #natsMsgList_Destroy to release memory
 * allocated by this call and destroy all pending messages still present in the list.
 *
 * \code{.unparsed}
 * natsMsgList pending;
 *
 * s = js_PublishAsyncGetPendingList(&pending, js);
 * if (s == NATS_OK)
 * {
 *      int i;
 *
 *      for (i=0; i<pending.Count; i++)
 *      {
 *              if (your_decision_to_resend(pending.Msgs[i]))
 *              {
 *                      // If the call is successful, pending.Msgs[i] will
 *                      // be set to NULL so the message will not be
 *                      // destroyed.
 *                      js_PublishMsgAsync(js, &(pending.Msgs[i]), NULL);
 *              }
 *      }
 *
 *      // Calling this will release memory allocated to hold the
 *      // array of messages but also call natsMsg_Destroy on all
 *      // messages still present in the array.
 *      natsMsgList_Destroy(&pending);
 * }
 * \endcode
 *
 * @param pending pointer to a #natsMsgList object, typically defined on the stack.
 * @param js the pointer to the #jsCtx object.
 */
NATS_EXTERN natsStatus
js_PublishAsyncGetPendingList(natsMsgList *pending, jsCtx *js);

/** @} */ // end of jsPubGroup

/** \defgroup jsSubGroup Subscribing
 *
 *  Subscribing functions
 *  @{
 */

/** \brief Initializes a subscribe options structure.
 *
 * Use this before setting specific subscribe options and passing this
 * configuration to the JetStream subscribe APIs.
 *
 * @param opts the pointer to the #jsSubOptions to initialize.
 */
NATS_EXTERN natsStatus
jsSubOptions_Init(jsSubOptions *opts);

/** \brief Create an asynchronous subscription.
 *
 * Typically the user or administrator will have created a JetStream
 * consumer and this call will reference the stream/consumer to bind
 * to with the use of #jsSubOptions's `Stream` and `Consumer`.
 *
 * Without the stream information, the library will use the provided
 * `subject` to try figure out which stream this subscription is for.
 *
 * If a `Durable` is specified (with #jsSubOptions' `Config.Durable`),
 * the subscription will be durable. However, note the behavior described
 * below regarding JetStream consumers created by this call.
 *
 * If no `Durable` is specified, the subscription will be ephemeral
 * and removed by the server either after calling #natsSubscription_Unsubscribe
 * or after the subscription is destroyed and the `InactivityThreshold`
 * has elapsed.
 *
 * \note If a JetStream consumer does not exist and this call creates
 * it, it will be removed in the server once the user calls #natsSubscription_Unsubscribe
 * or #natsSubscription_Drain, even if this is a `Durable` subscription.
 * If the subscription should be maintained, it should be explicitly
 * created using #js_AddConsumer and then bound to with the use
 * of #jsSubOptions' `Stream` and `Consumer`.
 *
 * \warning Prior to release v3.4.0, calling #natsSubscription_Destroy
 * would delete the JetStream consumer if it was created by this call.
 * The original intent was that it would be deleted only with explicit
 * calls to unsubscribe or drain. Therefore, starting v3.4.0, if
 * the user calls only #natsSubscription_Destroy (to free memory),
 * the JetStream consumer will no longer be deleted. The user would
 * have to explicitly call #natsSubscription_Unsubscribe or #js_DeleteConsumer.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param js the pointer to the #jsCtx object.
 * @param subject the subject this subscription is created for.
 * @param cb the #natsMsgHandler callback.
 * @param cbClosure a pointer to an user defined object (can be `NULL`). See
 * the #natsMsgHandler prototype.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param subOpts the subscribe options, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_Subscribe(natsSubscription **sub, jsCtx *js, const char *subject,
             natsMsgHandler cb, void* cbClosure,
             jsOptions *opts, jsSubOptions *subOpts, jsErrCode *errCode);

/** \brief Create a synchronous subscription.
 *
 * See important notes in #js_Subscribe.
 *
 * @param sub the location where to store the pointer to the newly created
 * #natsSubscription object.
 * @param js the pointer to the #jsCtx object.
 * @param subject the subject this subscription is created for.
 * the #natsMsgHandler prototype.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param subOpts the subscribe options, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_SubscribeSync(natsSubscription **sub, jsCtx *js, const char *subject,
                 jsOptions *opts, jsSubOptions *subOpts, jsErrCode *errCode);

/** \brief Create a pull subscriber.
 *
 * A pull based consumer is a type of consumer that does not have a delivery subject,
 * that is the library has to request for the messages to be delivered as needed from the server.
 *
 * \note If no durable name is provided, the pull subscription will create an ephemeral
 * JetStream consumer. The requirement for a durable name is lifted in NATS C client v3.4.0+
 * and NATS Server v2.7.0+.
 * \note If a durable name is specified, it cannot contain the character ".".
 *
 * See important notes in #js_Subscribe.
 *
 * @param sub the location where to store the pointer to the newly created #natsSubscription object.
 * @param js the pointer to the #jsCtx object.
 * @param subject the subject this subscription is created for.
 * @param durable the optional durable name.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param subOpts the subscribe options, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
js_PullSubscribe(natsSubscription **sub, jsCtx *js, const char *subject, const char *durable,
                 jsOptions *opts, jsSubOptions *subOpts, jsErrCode *errCode);

/** \brief Fetches messages for a pull subscription.
 *
 * Fetches up to `batch` messages from the server, waiting up to `timeout` milliseconds.
 * No more thant `batch` messages will be returned, however, it can be less.
 *
 * For `batch` greater than `1`, this call will not necessarily wait up `timeout` milliseconds
 * if some messages were collected and the library receives a notification that
 * no more messages are available at this time.<br>
 * It means that calling `natsSubscription_Fetch(&list, sub, 10, 5000)` may
 * return after less than 5 seconds with only 3 messages.
 *
 * @param list the location to a #natsMsgList that will be filled by the result of this call.
 * @param sub the pointer to the #natsSubscription object.
 * @param batch the batch size, that is, the maximum number of messages to return.
 * @param timeout the timeout (required) expressed in number of milliseconds.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
natsSubscription_Fetch(natsMsgList *list, natsSubscription *sub, int batch, int64_t timeout,
                       jsErrCode *errCode);

/** \brief Initializes a fetch request options structure.
 *
 * Use this before setting specific fetch options and passing it to #natsSubscription_FetchRequest.
 *
 * @param request the pointer to the #jsFetchRequest object.
 */
NATS_EXTERN natsStatus
jsFetchRequest_Init(jsFetchRequest *request);

/** \brief Fetches messages for a pull subscription with a complete request configuration
 *
 * Similar to #natsSubscription_Fetch but a full #jsFetchRequest configuration is provided
 * for maximum control.
 *
 * Initialize the #jsFetchRequest structure using #jsFetchRequest_Init and then set
 * the parameters desired, then invoke this function.
 *
 * @param list the location to a #natsMsgList that will be filled by the result of this call.
 * @param sub the pointer to the #natsSubscription object.
 * @param request the pointer to a #jsFetchRequest configuration.
 */
NATS_EXTERN natsStatus
natsSubscription_FetchRequest(natsMsgList *list, natsSubscription *sub, jsFetchRequest *request);

/** \brief Returns the jsConsumerInfo associated with this subscription.
 *
 * Returns the #jsConsumerInfo associated with this subscription.
 *
 * @param ci the location where to store the pointer to the new #jsConsumerInfo object.
 * @param sub the pointer to the #natsSubscription object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
natsSubscription_GetConsumerInfo(jsConsumerInfo **ci, natsSubscription *sub,
                                 jsOptions *opts, jsErrCode *errCode);

/** \brief Returns the consumer sequence mismatch information.
 *
 * If `Heartbeat` is configured in #jsConsumerConfig object (or configured in an existing
 * JetStream consumer), the server sends heartbeats to the client at the given interval.
 *
 * Those heartbeats contains information that allow the application to detect a mismatch
 * between the server and client's view of the state of the consumer.
 *
 * If the library detects a sequence mismatch, the behavior is different depending on
 * the type of subscription:
 *
 * * For asynchronous subscriptions: the error #NATS_MISMATCH is published to the error handler
 * (see #natsOptions_SetErrorHandler).
 * * For synchronous subscriptions: the next call to #natsSubscription_NextMsg() will
 * return the error #NATS_MISMATCH (but will succeed afterwards).
 *
 * In both cases, the user should check what the mismatch is using this function and
 * possibly recreate the consumer based on the provided information.
 *
 * \note For a valid JetStream subscription, this function will return #NATS_NOT_FOUND
 * if no consumer sequence mismatch has been detected.
 *
 * @see jsConsumerSequenceMismatch
 *
 * @param csm the pointer location where to copy the consumer sequence mismatch information.
 * @param sub the pointer to the #natsSubscription object.
 */
NATS_EXTERN natsStatus
natsSubscription_GetSequenceMismatch(jsConsumerSequenceMismatch *csm, natsSubscription *sub);

/** @} */ // end of jsSubGroup

/** \defgroup jsMsg Messages
 *
 *  Function specific to JetStream messages
 *  @{
 */

/** \brief Returns metadata from this JetStream message.
 *
 * This works only for JetStream messages that have been received through
 * a subscription callback or calling #natsSubscription_NextMsg.
 *
 * \note The user must destroy the returned object with #jsMsgMetaData_Destroy.
 *
 * \note This function will return an error for non JetStream messages.
 *
 * @param new_meta the location where to store the pointer to the newly created
 * #jsMsgMetaData object.
 * @param msg the pointer to the #natsMsg object, which should be a JetStream
 * message received through a subscription callback or #natsSubscription_NextMsg.
 */
NATS_EXTERN natsStatus
natsMsg_GetMetaData(jsMsgMetaData **new_meta, natsMsg *msg);

/** \brief Destroys the message metadata object.
 *
 * Releases memory allocated for this #jsMsgMetaData object.
 *
 * @param meta the pointer to the #jsMsgMetaData object.
 */
NATS_EXTERN void
jsMsgMetaData_Destroy(jsMsgMetaData *meta);

/** \brief Acknowledges a message.
 *
 * This tells the server that the message was successfully processed and
 * it can move on to the next message.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsMsg_Ack(natsMsg *msg, jsOptions *opts);

/** \brief Acknowledges a message and wait for a confirmation.
 *
 * This is the synchronous version of #natsMsg_Ack. This indicates successful
 * message processing, and waits for confirmation from the server that
 * the acknowledgment has been processed.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 * @param errCode the location where to store the JetStream specific error code, or `NULL`
 * if not needed.
 */
NATS_EXTERN natsStatus
natsMsg_AckSync(natsMsg *msg, jsOptions *opts, jsErrCode *errCode);

/** \brief Negatively acknowledges a message.
 *
 * This tells the server to redeliver the message. You can configure the
 * number of redeliveries by passing `MaxDeliver` when you subscribe.
 *
 * The default is infinite redeliveries.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsMsg_Nak(natsMsg *msg, jsOptions *opts);

/** \brief Negatively acknowledges a message.
 *
 * This tells the server to redeliver the message after the given `delay`
 * duration expressed in milliseconds. You can configure the number of
 * redeliveries by passing `MaxDeliver` when you subscribe.
 *
 * The default is infinite redeliveries.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param delay the amount of time before the redelivery expressed in milliseconds.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsMsg_NakWithDelay(natsMsg *msg, int64_t delay, jsOptions *opts);

/** \brief Resets redelivery timer on the server.
 *
 * This tells the server that this message is being worked on. It resets
 * the redelivery timer on the server.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsMsg_InProgress(natsMsg *msg, jsOptions *opts);

/** \brief Abandon this message.
 *
 * This tells the server to not redeliver this message, regardless of
 * the value `MaxDeliver`.
 *
 * @param msg the pointer to the #natsMsg object.
 * @param opts the pointer to the #jsOptions object, possibly `NULL`.
 */
NATS_EXTERN natsStatus
natsMsg_Term(natsMsg *msg, jsOptions *opts);

/** \brief Returns the sequence number of this JetStream message.
 *
 * Returns the sequence number of this JetStream message, or `0` if `msg` is `NULL`
 * or not a JetStream message.
 *
 * \note This applies to JetStream messages retrieved with #js_GetMsg or #js_GetLastMsg.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN uint64_t
natsMsg_GetSequence(natsMsg *msg);

/** \brief Returns the timestamp (in UTC) of this JetStream message.
 *
 * Returns the timestamp (in UTC) of this JetStream message, or `0` if `msg` is `NULL`
 * or not a JetStream message.
 *
 * \note This applies to JetStream messages retrieved with #js_GetMsg or #js_GetLastMsg.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN int64_t
natsMsg_GetTime(natsMsg *msg);

/** @} */ // end of jsMsg

/** @} */ // end of jsGroup

/** \defgroup kvGroup KeyValue store
 *
 * A KeyValue store is a materialized view of JetStream.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \defgroup kvGroupMgt KeyValue store management
 *
 * These functions allow to create, get or delete a KeyValue store.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \brief Initializes a KeyValue configuration structure.
 *
 * Use this before setting specific #kvConfig options and passing it to #js_CreateKeyValue.
 *
 * @see js_CreateKeyValue
 *
 * @param cfg the pointer to the stack variable #kvConfig to initialize.
 */
NATS_EXTERN natsStatus
kvConfig_Init(kvConfig *cfg);

/** \brief Creates a KeyValue store with a given configuration.
 *
 * Creates a KeyValue store with a given configuration.
 *
 * Bucket names are restricted to this set of characters: `A-Z`, `a-z`, `0-9`, `_` and `-`.
 *
 * \note The return #kvStore object needs to be destroyed using #kvStore_Destroy when
 * no longer needed to free allocated memory. This is different from deleting a KeyValue store
 * from the server using the #js_DeleteKeyValue API.
 *
 * @param new_kv the location where to store the newly created #kvStore object.
 * @param js the pointer to the #jsCtx object.
 * @param cfg the pointer to the #kvConfig configuration information used to create the #kvStore object.
 */
NATS_EXTERN natsStatus
js_CreateKeyValue(kvStore **new_kv, jsCtx *js, kvConfig *cfg);

/** \brief Looks-up and binds to an existing KeyValue store.
 *
 * This call is when the user wants to use an existing KeyValue store.
 * If the store does not already exists, an error is returned.
 *
 * Bucket names are restricted to this set of characters: `A-Z`, `a-z`, `0-9`, `_` and `-`.
 *
 * \note The return #kvStore object needs to be destroyed using #kvStore_Destroy when
 * no longer needed to free allocated memory. This is different from deleting a KeyValue store
 * from the server using the #js_DeleteKeyValue API.
 *
 * @param new_kv the location where to store the newly created #kvStore object.
 * @param js the pointer to the #jsCtx object.
 * @param bucket the name of the bucket of the existing KeyValue store.
 */
NATS_EXTERN natsStatus
js_KeyValue(kvStore **new_kv, jsCtx *js, const char *bucket);

/** \brief Deletes a KeyValue store.
 *
 * This will delete the KeyValue store with the `bucket` name.
 *
 * Bucket names are restricted to this set of characters: `A-Z`, `a-z`, `0-9`, `_` and `-`.
 *
 * @param js the pointer to the #jsCtx object.
 * @param bucket the name of the bucket of the existing KeyValue store.
 */
NATS_EXTERN natsStatus
js_DeleteKeyValue(jsCtx *js, const char *bucket);

/** \brief Destroys a KeyValue store object.
 *
 * This will simply free memory resources in the library for this #kvStore,
 * but does not delete the KeyValue store in the server.
 *
 * @param kv the pointer to the #kvStore object.
 */
NATS_EXTERN void
kvStore_Destroy(kvStore *kv);

/** @} */ // end of  kvGroupMgt

/** \defgroup kvEntry KeyValue store entries
 *
 * These functions allow to inspect a the value, or entry, of a given key.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \brief Returns the name of the bucket the data was loaded from.
 *
 * Returns the name of the bucket the data was loaded from, or `NULL` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN const char*
kvEntry_Bucket(kvEntry *e);

/** \brief Returns the name of the key that was retrieved.
 *
 * Returns the name of the key that was retrieved, or `NULL` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN const char*
kvEntry_Key(kvEntry *e);

/** \brief Returns the value for this key.
 *
 * Returns the value for this key, or `NULL` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN const void*
kvEntry_Value(kvEntry *e);

/** \brief Returns the value length for this key.
 *
 * Returns the value length for this key, or `-1` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN int
kvEntry_ValueLen(kvEntry *e);

/** \brief Returns the value, as a string, for this key.
 *
 * If the value is an actual string, this call will return a NULL terminating string (`const char*`),
 * or `NULL` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN const char*
kvEntry_ValueString(kvEntry *e);

/** \brief Returns the unique sequence for this value.
 *
 * Returns the unique sequence for this value, or `0` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN uint64_t
kvEntry_Revision(kvEntry *e);

/** \brief Returns the time (in UTC) the data was put in the bucket.
 *
 * Returns the time (in UTC) the data was put in the bucket, or `0` if `e` itself is `NULL`.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN int64_t
kvEntry_Created(kvEntry *e);

/** \brief Returns the distance from the latest value.
 *
 * Returns the distance from the latest value, or `0` if `e` itself is `NULL`.
 *
 * If history is enabled this is effectively the index of the historical value,
 * 0 for latest, 1 for most recent etc...
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN uint64_t
kvEntry_Delta(kvEntry *e);

/** \brief Returns the type of operation of this value.
 *
 * Returns the type of operation of this value.
 *
 * @see kvOperation
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN kvOperation
kvEntry_Operation(kvEntry *e);

/** \brief Destroys the KeyValue entry object.
 *
 * Releases memory allocated for this #kvEntry object.
 *
 * @param e the pointer to the #kvEntry object.
 */
NATS_EXTERN void
kvEntry_Destroy(kvEntry *e);

/** @} */ // end of kvEntry

/** \brief Returns the latest entry for the key.
 *
 * Returns the latest entry for the key.
 *
 * \note The entry should be destroyed to release memory using #kvEntry_Destroy.
 *
 * @param new_entry the location where to store the pointer to the entry associated with the `key`.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 */
NATS_EXTERN natsStatus
kvStore_Get(kvEntry **new_entry, kvStore *kv, const char *key);

/** \brief Returns the entry at the specific revision for the key.
 *
 * Returns the entry at the specific revision for the key, or #NATS_NOT_FOUND if there is no
 * entry for that key and revision.
 *
 * \note The entry should be destroyed to release memory using #kvEntry_Destroy.
 *
 * @param new_entry the location where to store the pointer to the entry associated with the `key`.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param revision the revision of the entry (must be > 0).
 */
NATS_EXTERN natsStatus
kvStore_GetRevision(kvEntry **new_entry, kvStore *kv, const char *key, uint64_t revision);

/** \brief Places the new value for the key into the store.
 *
 * Places the new value for the key into the store.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the data in memory.
 * @param len the number of bytes to copy from the data's memory location.
 */
NATS_EXTERN natsStatus
kvStore_Put(uint64_t *rev, kvStore *kv, const char *key, const void *data, int len);

/** \brief Places the new value (as a string) for the key into the store.
 *
 * Places the new value, as a string, for the key into the store.
 *
 * \note This is equivalent of calling #kvStore_Put with `(int) strlen(data)`.
 *
 * \warning The NULL terminating character is not included in the number of bytes stored in the KeyValue store.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the string to store.
 */
NATS_EXTERN natsStatus
kvStore_PutString(uint64_t *rev, kvStore *kv, const char *key, const char *data);

/** \brief Places the value for the key into the store if and only if the key does not exist.
 *
 * Places the value for the key into the store if and only if the key does not exist.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the data in memory.
 * @param len the number of bytes to copy from the data's memory location.
 */
NATS_EXTERN natsStatus
kvStore_Create(uint64_t *rev, kvStore *kv, const char *key, const void *data, int len);

/** \brief Places the value (as a string) for the key into the store if and only if the key does not exist.
 *
 * Places the value (as a string) for the key into the store if and only if the key does not exist.
 *
 * \note This is equivalent of calling #kvStore_Create with `(int) strlen(data)`.
 *
 * \warning The NULL terminating character is not included in the number of bytes stored in the KeyValue store.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the string.
 */
NATS_EXTERN natsStatus
kvStore_CreateString(uint64_t *rev, kvStore *kv, const char *key, const char *data);

/** \brief Updates the value for the key into the store if and only if the latest revision matches.
 *
 * Updates the value for the key into the store if and only if the latest revision matches.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the data in memory.
 * @param len the number of bytes to copy from the data's memory location.
 * @param last the expected latest revision prior to the update.
 */
NATS_EXTERN natsStatus
kvStore_Update(uint64_t *rev, kvStore *kv, const char *key, const void *data, int len, uint64_t last);

/** \brief Updates the value (as a string) for the key into the store if and only if the latest revision matches.
 *
 * Updates the value (as a string) for the key into the store if and only if the latest revision matches.
 *
 * \note This is equivalent of calling #kvStore_Update with `(int) strlen(data)`.
 *
 * \warning The NULL terminating character is not included in the number of bytes stored in the KeyValue store.
 *
 * @param rev the location where to store the revision of this value, or `NULL` if the stream information is not needed.
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param data the pointer to the string.
 * @param last the expected latest revision prior to the update.
 */
NATS_EXTERN natsStatus
kvStore_UpdateString(uint64_t *rev, kvStore *kv, const char *key, const char *data, uint64_t last);

/** \brief Deletes a key by placing a delete marker and leaving all revisions.
 *
 * Deletes a key by placing a delete marker and leaving all revisions.
 *
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 */
NATS_EXTERN natsStatus
kvStore_Delete(kvStore *kv, const char *key);

/** \brief Deletes a key by placing a purge marker and removing all revisions.
 *
 * Deletes a key by placing a purge marker and removing all revisions.
 *
 * @param kv the pointer to the #kvStore object.
 * @param key the name of the key.
 * @param opts the pointer to the #kvPurgeOptions, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_Purge(kvStore *kv, const char *key, kvPurgeOptions *opts);

/** \brief Initializes a KeyValue watcher options structure.
 *
 * Use this before setting specific watcher options and passing it
 * to #kvStore_Watch.
 *
 * @param opts the pointer to the #kvWatchOptions to initialize.
 */
NATS_EXTERN natsStatus
kvWatchOptions_Init(kvWatchOptions *opts);

/** \brief Initializes a KeyValue purge options structure.
 *
 * Use this before setting specific purge options and passing it
 * to #kvStore_Purge or #kvStore_PurgeDeletes.
 *
 * @param opts the pointer to the #kvPurgeOptions to initialize.
 */
NATS_EXTERN natsStatus
kvPurgeOptions_Init(kvPurgeOptions *opts);

/** \brief Purge and removes delete markers.
 *
 * Removes data and delete markers, but may keep the markers that are considered
 * more recent than a certain threshold (default is 30 minutes).
 *
 * This is a maintenance option if there is a larger buildup of delete markers.
 *
 * \note Use #kvPurgeOptions.Timeout to specify how long to wait (in milliseconds)
 * in gathering all keys that have purge markers. This function will still
 * purge some of the keys and return #NATS_TIMEOUT to indicate that it may not
 * have deleted them all.
 *
 * @see kvPurgeOptions_Init
 *
 * @param kv the pointer to the #kvStore object.
 * @param opts the pointer to the #kvPurgeOptions, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_PurgeDeletes(kvStore *kv, kvPurgeOptions *opts);

/** \brief Returns a watcher for any updates to keys that match the `keys` argument.
 *
 * Returns a watcher for any updates to keys that match the `keys` argument, which
 * could include wildcard.
 *
 * A `NULL` entry will be posted when the watcher has received all initial values.
 *
 * Call #kvWatcher_Next to get the next #kvEntry.
 *
 * \note The watcher should be destroyed to release memory using #kvWatcher_Destroy.
 *
 * @param new_watcher the location where to store the pointer to the new #kvWatcher object.
 * @param kv the pointer to the #kvStore object.
 * @param keys the keys (wildcard possible) to create the watcher for.
 * @param opts the watcher options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_Watch(kvWatcher **new_watcher, kvStore *kv, const char *keys, kvWatchOptions *opts);

/** \brief Returns a watcher for any updates to any keys of the KeyValue store bucket.
 *
 * Returns a watcher for any updates to any keys of the KeyValue store bucket.
 *
 * A `NULL` entry will be posted when the watcher has received all initial values.
 *
 * Call #kvWatcher_Next to get the next #kvEntry.
 *
 * \note The watcher should be destroyed to release memory using #kvWatcher_Destroy.
 *
 * @param new_watcher the location where to store the pointer to the new #kvWatcher object.
 * @param kv the pointer to the #kvStore object.
 * @param opts the watcher options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_WatchAll(kvWatcher **new_watcher, kvStore *kv, kvWatchOptions *opts);

/** \brief Returns all keys in the bucket.
 *
 * Returns all keys in the bucket.
 *
 * \note Use #kvWatchOptions.Timeout to specify how long to wait (in milliseconds)
 * to gather all keys for this bucket. If the deadline is reached, this function
 * will return #NATS_TIMEOUT and no keys.
 *
 * \warning The user should call #kvKeysList_Destroy to release memory allocated
 * for the entries list.
 *
 * @see kvWatchOptions_Init
 * @see kvKeysList_Destroy
 *
 * @param list list the pointer to a #kvKeysList that will be initialized and filled with resulting key strings.
 * @param kv the pointer to the #kvStore object.
 * @param opts the history options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_Keys(kvKeysList *list, kvStore *kv, kvWatchOptions *opts);

/** \brief Destroys this list of KeyValue store key strings.
 *
 * This function iterates through the list of all key strings and free them.
 * It then frees the array that was allocated to hold pointers to those keys.
 *
 * \note The #kvKeysList object itself is not freed since it is expected that
 * users will pass a pointer to a stack object. Should the user create its own
 * object, it will be the user responsibility to free this object.
 *
 * @param list the #kvKeysList list of key strings to destroy.
 */
NATS_EXTERN void
kvKeysList_Destroy(kvKeysList *list);

/** \brief Returns all historical entries for the key.
 *
 * Returns all historical entries for the key
 *
 * Use the options to alter the behavior. For instance, if delete markers
 * are not desired, option #kvWatchOptions.IgnoreDeletes should be specified.
 *
 * \note Use #kvWatchOptions.Timeout to specify how long to wait (in milliseconds)
 * to gather all entries for this key. If the deadline is reached, this function
 * will return #NATS_TIMEOUT and no entries.
 *
 * \warning The user should call #kvEntryList_Destroy to release memory allocated
 * for the entries list.
 *
 * @see kvWatchOptions_Init
 * @see kvEntryList_Destroy
 *
 * @param list the pointer to a #kvEntryList that will be initialized and filled with resulting entries.
 * @param kv the pointer to the #kvStore object.
 * @param key the key for which the history is requested.
 * @param opts the history options, possibly `NULL`.
 */
NATS_EXTERN natsStatus
kvStore_History(kvEntryList *list, kvStore *kv, const char *key, kvWatchOptions *opts);

/** \brief Destroys this list of KeyValue store entries.
 *
 * This function iterates through the list of all entries and call #kvEntry_Destroy.
 * It then frees the array that was allocated to hold pointers to those entries.
 *
 * \note The #kvEntryList object itself is not freed since it is expected that
 * users will pass a pointer to a stack object. Should the user create its own
 * object, it will be the user responsibility to free this object.
 *
 * @param list the #kvEntryList list of #kvEntry objects to destroy.
 */
NATS_EXTERN void
kvEntryList_Destroy(kvEntryList *list);

/** \brief Returns the bucket name of this KeyValue store object.
 *
 * Returns the bucket name of this KeyValue store object, or `NULL` if
 * `kv` itself is NULL.
 *
 * \warning Do not free the string returned by this function.
 *
 * @param kv the pointer to the #kvStore object.
 */
NATS_EXTERN const char*
kvStore_Bucket(kvStore *kv);

// PurgeDeletes

/** \brief Returns the status and configuration of a bucket.
 *
 * Returns the status and configuration of a bucket.
 *
 * \note The status should be destroyed to release memory using #kvStatus_Destroy.
 *
 * @param new_status the location where to store the status of this KeyValue store.
 * @param kv the pointer to the #kvStore object.
 */
NATS_EXTERN natsStatus
kvStore_Status(kvStatus **new_status, kvStore *kv);

/** \defgroup kvWatcher KeyValue store watcher
 *
 * These functions allow to receive updates for key(s) on a given bucket.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \brief Returns the next entry for this watcher.
 *
 * Returns the next entry for this watcher. The entry may be `NULL`
 * (with #NATS_OK status) to indicate that the initial state has
 * been retrieved.
 *
 * If a thread is waiting on this call, it can be canceled with a call to
 * #kvWatcher_Stop.
 *
 * \note The entry should be destroyed to release memory using #kvEntry_Destroy.
 *
 * @param new_entry the location where to store the pointer to the #kvEntry object.
 * @param w the pointer to the #kvWatcher object.
 * @param timeout how long to wait (in milliseconds) for the next entry.
 */
NATS_EXTERN natsStatus
kvWatcher_Next(kvEntry **new_entry, kvWatcher *w, int64_t timeout);

/** \brief Stops the watcher.
 *
 * Stops the watcher. Stopping a stopped watcher returns #NATS_OK.
 *
 * After this call, new and existing calls to #kvWatcher_Next (that are waiting
 * for an update) will return with #NATS_ILLEGAL_STATE.
 *
 * @param w the pointer to the #kvWatcher object.
 */
NATS_EXTERN natsStatus
kvWatcher_Stop(kvWatcher *w);

/** \brief Destroys the KeyValue watcher object.
 *
 * Releases memory allocated for this #kvWatcher object.
 *
 * @param w the pointer to the #kvWatcher object.
 */
NATS_EXTERN void
kvWatcher_Destroy(kvWatcher *w);

/** @} */ // end of kvWatcher

/** \defgroup kvStatus KeyValue store status
 *
 * These functions allow to inspect the status of a bucket.
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 *
 *  @{
 */

/** \brief Returns the bucket name.
 *
 * Returns the bucket name, or `NULL` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN const char*
kvStatus_Bucket(kvStatus *sts);

/** \brief Returns how many messages are in the bucket, including historical values.
 *
 * Returns how many messages are in the bucket, including historical values, or `0` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN uint64_t
kvStatus_Values(kvStatus *sts);

/** \brief Returns the configured history kept per key.
 *
 * Returns the configured history kept per key, or `0` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN int64_t
kvStatus_History(kvStatus *sts);

/** \brief Returns how long the bucket keeps values for.
 *
 * Returns how long the bucket keeps values for, or `0` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN int64_t
kvStatus_TTL(kvStatus *sts);

/** \brief Returns the number of replicas to keep for a bucket.
 *
 * Returns the number of replicas to keep for a bucket, or `0` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN int64_t
kvStatus_Replicas(kvStatus *sts);

/** \brief Returns the size (in bytes) of this bucket.
 *
 * Returns the size (in bytes) of this bucket, or `0` if `sts` itself is `NULL`.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN uint64_t
kvStatus_Bytes(kvStatus *sts);

/** \brief Destroys the KeyValue status object.
 *
 * Releases memory allocated for this #kvStatus object.
 *
 * @param sts the pointer to the #kvStatus object.
 */
NATS_EXTERN void
kvStatus_Destroy(kvStatus *sts);

/** @} */ // end of kvStatus

/** @} */ // end of kvGroup

/** @} */ // end of funcGroup

//
// Microservices.
//

/** \defgroup microGroup EXPERIMENTAL - Microservices
 *
 * \warning EXPERIMENTAL FEATURE! We reserve the right to change the API without
 * necessarily bumping the major version of the library.
 * 
 * ### NATS Microservices.
 *
 * Microservices can expose one or more request-response endpoints that process
 * incoming NATS messages. 
 *
 * Microservices are created by calling micro_AddService, and configured by
 * passing a microServiceConfig to it. Many microservices can share a single
 * connection to a NATS server. 
 *
 * Once created, a microservice will subscribe to all endpoints' subjects and
 * associate them with the configured handlers. It will also subscribe to and
 * service monitoring subjects for service-specific pings, metadata, and
 * statistics requests. The endpoint subscriptions are created with a queue
 * group, so that incoming requests are automatically load-balanced across all
 * running instances of a microservice. The monitoring subscriptions are not
 * groupped, each service instance receives and responds to all monitoring
 * requests.
 *
 * Once created, the microservice is asyncronous, message handlers and other
 * callbacks will be invoked in separate threads. No further action is needed.
 * Your program can use microService_Stop, microService_IsStopped to control the
 * execution of the service.
 *
 *  @{
 */

/** \defgroup microTypes Types
 *
 * Microservice types.
 *
 *  @{
 */

/**
 * @brief The Microservice client.
 *
 * Initialize with #micro_NewClient and destroy with #microClient_Destroy.
 *
 * @see micro_NewClient, microClient_Destroy
 */
typedef struct micro_client_s microClient;

/**
 * @brief The Microservice configuration object. For forward compatibility only.
 */
typedef struct __for_forward_compatibility_only microClientConfig;

/**
 * @brief `microEndpoint` represents a microservice endpoint.
 *
 * The only place where this struct is used by the user is in callbacks, to
 * identify which endpoint was called, or caused an error.
 *
 * @see microRequestHandler, microErrorHandler, microServiceConfig,
 * microEndpointConfig
 */
typedef struct micro_endpoint_s microEndpoint;

/**
 * @brief The Microservice endpoint configuration object.
 *
 * @see micro_endpoint_config_s for descriptions of the fields,
 * micro_service_config_s, microServiceConfig, microService_AddEndpoint,
 * microGroup_AddEndpoint
 */
typedef struct micro_endpoint_config_s microEndpointConfig;

/**
 * @brief static information about an endpoint.
 *
 * microEndpointInfo is returned by microService_GetInfo function, as part of microServiceInfo. It
 * is also accessible by sending a `$SRV.INFO.<service-name>[.<id>]` request to
 * the service. See micro_endpoint_info_s for descriptions of the fields.
 *
 * @see micro_endpoint_info_s, micro_service_info_s, microService_GetInfo
 */
typedef struct micro_endpoint_info_s microEndpointInfo;

/**
 * @brief The Microservice endpoint-level stats struct.
 *
 * Returned as part of microEndpointStats. See micro_endpoint_stats_s for
 * descriptions of the fields.
 *
 * @see micro_endpoint_stats_s, microServiceStats, microService_GetStats
 */
typedef struct micro_endpoint_stats_s microEndpointStats;

/**
 * @brief the Microservice error object.
 *
 * This error type is returned by most microservice functions. You can create
 * your own custom errors by using #micro_Errorf and wrap existing errors using
 * #microError_Wrapf. Errors are heap-allocated and must be freed with either
 * #microError_Destroy or by passing it into #microRequest_Respond. 
 *
 * There are no public fields in this struct, use #microError_Code,
 * #microError_Status, and #microError_String to get more information about the
 * error.
 */
typedef struct micro_error_s microError;

/**
 * @brief a collection of endpoints and other groups, with a
 * common prefix to their subjects and names.
 * 
 * It has no other purpose than
 * convenience, for organizing endpoint subject space.
 */
typedef struct micro_group_s microGroup;

/**
 * @brief a request received by a microservice endpoint.
 *
 * @see micro_request_s for descriptions of the fields.
 */
typedef struct micro_request_s microRequest;

/** 
 * @brief the main object for a configured microservice.
 *
 * It can be created with #micro_AddService, and configured by passing a
 * microServiceConfig to it. Once no longer needed, a microservice should be
 * destroyed with microService_Destroy.
 *
 * @see micro_AddService, microServiceConfig, microEndpointConfig,
 * microService_Destroy, microService_Stop, microService_IsStopped,
 * microService_Run
 */
typedef struct micro_service_s microService;

/**
 * @brief The microservice configuration object.
 *
 * The service is created with a clone of the config and all of its values, so
 * the original can be freed or modified after calling #micro_AddService. See
 * micro_service_config_s for descriptions of the fields.
 *
 * @see micro_service_config_s
 */
typedef struct micro_service_config_s microServiceConfig;

/**
 * @brief Information about a running microservice.
 *
 * microServiceInfo is the struct returned by microService_GetInfo function. It
 * is also accessible by sending a `$SRV.INFO.<service-name>[.<id>]` request to
 * the service. See micro_service_info_s for descriptions of the fields.
 *
 * @see micro_service_info_s, microService_GetInfo
 */
typedef struct micro_service_info_s microServiceInfo;

/**
 * @brief The Microservice service-level stats struct. 
 *
 * @see micro_service_stats_s for descriptions of the fields,
 * microService_GetStats
 */
typedef struct micro_service_stats_s microServiceStats;

/** @} */ // end of microTypes

/** \defgroup microCallbacks Callbacks
 *
 *  Microservice callbacks.
 *  @{
 */

/**
 * @brief Callback type for request processing.
 *
 * This is the callback that one provides when creating a microservice endpoint.
 * The library will invoke this callback for each message arriving to the
 * specified subject.
 *
 * @param req The request object, containing the message and other relevant
 * references.
 *
 * @see microEndpointConfig, micro_endpoint_config_s.
 */
typedef microError *(*microRequestHandler)(microRequest *req);

/**
 * @brief Callback type for async error notifications.
 *
 * If specified in microServiceConfig, this callback is invoked for internal
 * errors (e.g. message delivery failures) related to a microservice. If the
 * error is associated with an endpoint, the ep parameter points at the
 * endpoint. However, this handler may be invoked for errors happening in
 * monitoring subjects, in which case ep is NULL.
 *
 * The error handler is invoked asynchronously, in a separate theread.
 *
 * The error handler is not invoked for microservice-level errors that are sent
 * back to the client as responses. Note that the error counts in
 * microEndpointStats include both internal and service-level errors.
 *
 * @param m The microservice object.
 * @param ep The endpoint object, or NULL if the error is not associated with an
 * endpoint.
 * @param s The NATS status for the error.
 *
 * @see microServiceConfig, micro_service_config_s.
 */
typedef void (*microErrorHandler)(microService *m, microEndpoint *ep, natsStatus s);

/**
 * @brief Callback type for `Done` (service stopped) notifications.
 *
 * If specified in microServiceConfig, this callback is invoked right after the
 * service stops. In the C client, this callback is invoked directly from the
 * microService_Stop function, in whatever thread is executing it.
 *
 * @param m The microservice object.
 *
 * @see microServiceConfig, micro_service_config_s.
 */
typedef void (*microDoneHandler)(microService *m);

/** @} */ // end of microCallbacks

/** \defgroup microStructs Public structs
 *
 *  Microservice public structs.
 * 
 *  @{
 */

/**
 * The Microservice endpoint configuration object.
 */
struct micro_endpoint_config_s
{
    /**
     * @brief The name of the endpoint.
     *
     * Used in the service stats to list endpoints by name. Must not be empty.
     */
    const char *Name;

    /**
     * @brief The NATS subject the endpoint will listen on.
     *
     * Wildcards are allowed. If `Subject` is empty, it attempts to default to
     * `Name`, provided it is a valid subject.
     *
     * For endpoints added to a group, the subject is automatically prefixed
     * with the group's prefix.
     */
    const char *Subject;

    /**
     * @briefMetadata for the endpoint, a JSON-encoded user-provided object,
     * e.g. `{"key":"value"}`
     */
    natsMetadata Metadata;

    /**
     * @brief The request handler for the endpoint.
     */
    microRequestHandler Handler;

    /**
     * @brief A user-provided pointer to store with the endpoint
     * (state/closure).
     */
    void *State;
};

/**
 * microEndpointInfo is the struct for the endpoint's static metadata.
 */
struct micro_endpoint_info_s
{
    /**
     * @brief The name of the service.
     */
    const char *Name;

    /**
     * @brief The semantic version of the service.
     */
    const char *Subject;

    /**
     * @briefMetadata for the endpoint, a JSON-encoded user-provided object,
     * e.g. `{"key":"value"}`
     */
    natsMetadata Metadata;
};

/**
 * The Microservice endpoint stats struct.
 */
struct micro_endpoint_stats_s
{
    const char *Name;
    const char *Subject;
    
    /**
     * @brief The number of requests received by the endpoint.
     */
    int64_t NumRequests;

    /**
     * @brief The number of errors, service-level and internal, associated with
     * the endpoint.
     */
    int64_t NumErrors;

    /**
     * @brief total request processing time (the seconds part).
     */
    int64_t ProcessingTimeSeconds;

    /**
     * @brief total request processing time (the nanoseconds part).
     */
    int64_t ProcessingTimeNanoseconds;

    /**
     * @brief average request processing time, in ns.
     */
    int64_t AverageProcessingTimeNanoseconds;

    /**
     * @brief a copy of the last error message.
     */
    char LastErrorString[2048];
};

/**
 * @brief The Microservice top-level configuration object.
 *
 * The service is created with a clone of the config and all of its values, so
 * the original can be freed or modified after calling micro_AddService.
 */
struct micro_service_config_s
{
    /**
     * @brief The name of the service.
     *
     * It can be used to compose monitoring messages specific to this service.
     */
    const char *Name;

    /**
     * @brief The (semantic) version of the service.
     */
    const char *Version;

    /**
     * @brief The description of the service.
     */
    const char *Description;

    /**
     * @brief Metadata for the service, a JSON-encoded user-provided object, e.g. `{"key":"value"}`
     */
    natsMetadata Metadata;

    /**
     * @brief The "main" (aka default) endpoint configuration. 
     *
     * It is the default in that it does not require calling
     * microService_AddEndpoint, it is added automatically when creating the
     * service.
     */
    microEndpointConfig *Endpoint;

    /**
     * @brief A custom stats handler.
     *
     * It will be called to output the service's stats. It replaces the default
     * stats handler but can pull the service stats using microService_GetStats
     * function, then marshal them itself, as appropriate.
     */
    microRequestHandler StatsHandler;

    /**
     * @brief An error notification handler.
     *
     * It will be called asynchonously upon internal errors. It does not get
     * called for application-level errors, successfully sent out by the
     * microservice.
     */
    microErrorHandler ErrHandler;

    /**
     * @brief A callback handler for handling the final cleanup `Done` event,
     * right before the service is destroyed.
     *
     * It will be called directly from #microService_Stop method, so it may be
     * executed in any of the user threads or in the async callback thread if
     * the service stops itself on connection closed or an error event.
     */
    microDoneHandler DoneHandler;

    /**
     * @brief A user-provided pointer to state data.
     *
     * A closure that is accessible from the request, stats, and internal event
     * handlers. Please note that handlers are invoked on separate threads,
     * consider thread-safe mechanisms of accessing the data.
     */
    void *State;
};

/**
 * microServiceInfo is the struct returned by microService_GetInfo function. It
 * is also accessible by sending a `$SRV.INFO.<service-name>[.<id>]` request to
 * the service.
 */
struct micro_service_info_s
{
    /**
     * @brief Response type. Always `"io.nats.micro.v1.info_response"`.
     */
    const char *Type;
    
    /**
     * @brief The name of the service.
     */
    const char *Name;

    /**
     * @brief The semantic version of the service.
     */
    const char *Version;

    /**
     * @brief The description of the service.
     */
    const char *Description;
    
    /**
     * @brief The ID of the service instance responding to the request.
     */
    const char *Id;

    /**
     * @brief Metadata for the service, a JSON-encoded user-provided object, e.g. `{"key":"value"}`
     */
    natsMetadata Metadata;

    /**
     * @brief Endpoints.
     */
    microEndpointInfo *Endpoints;

    /**
     * @brief The number of endpoints in the `Endpoints` array.
     */
    int EndpointsLen;
};

/**
 * The Microservice stats struct.
 */
struct micro_service_stats_s
{
    /**
     * @brief Response type. Always `"io.nats.micro.v1.stats_response"`.
     */
    const char *Type;

    /**
     * @brief The name of the service.
     */
    const char *Name;

    /**
     * @brief The semantic version of the service.
     */
    const char *Version;

    /**
     * @brief The ID of the service instance responding to the request.
     */
    const char *Id;

    /**
     * @brief The timestamp of when the service was started.
     */
    int64_t Started;

    /**
     * @brief The stats for each endpoint of the service.
     */
    microEndpointStats *Endpoints;
    
    /**
     * @brief The number of endpoints in the `endpoints` array.
     */
    int EndpointsLen;
};

/** @} */ // end of microStructs

/** \defgroup microConstants Public constants
 *
 *  Microservice public constants.
 *  @{
 */

/**
 * @brief The prefix for all microservice monitoring subjects.
 * 
 * For example, `"$SRV.PING"`.
 */
#define MICRO_API_PREFIX "$SRV"

/**
 * @brief The `type` set in the `$SRV.INFO` responses.
 */
#define MICRO_INFO_RESPONSE_TYPE "io.nats.micro.v1.info_response"

/**
 * @brief For `$SRV.INFO.*` subjects.
 */
#define MICRO_INFO_VERB "INFO"

/**
 * @brief The `type` set in the `$SRV.PING` response.
 */
#define MICRO_PING_RESPONSE_TYPE "io.nats.micro.v1.ping_response"

/**
 * @brief For `$SRV.PING` subjects.
 */
#define MICRO_PING_VERB "PING"

/**
 * @brief The `type` set in the `STATS` response.
 */
#define MICRO_STATS_RESPONSE_TYPE "io.nats.micro.v1.stats_response"

/**
 * @brief The "verb" used in `$SRV.STATS` subjects.
 */
#define MICRO_STATS_VERB "STATS"

/**
 * @brief The response message header used to communicate an erroneous NATS
 * status back to the requestor.
 */
#define MICRO_STATUS_HDR "Nats-Status"

/**
 * @brief The response message header used to communicate an error message back
 * to the requestor.
 */
#define MICRO_ERROR_HDR "Nats-Service-Error"

/**
 * @brief The response message header used to communicate an integer error code
 * back to the requestor.
 */
#define MICRO_ERROR_CODE_HDR "Nats-Service-Error-Code"

/** @} */ // end of microConstants

/** \defgroup microFunctions Functions
 *
 *  Microservice functions.
 *  @{
 */

/** \defgroup microServiceFunctions microService
 *
 *  Functions that operate with #microService.
 *  @{
 */

/** @brief Creates and starts a new microservice.
 *
 * @note The microservice should be destroyed to clean up using
 * #microService_Destroy.
 *
 * @param new_microservice the location where to store the pointer to the new
 * #microService object.
 * @param nc the #natsConnection the service will use to receive and respond to
 * requests.
 * @param config a #microServiceConfig object with the configuration of the
 * service. See #micro_service_config_s for descriptions of the fields.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microService_Destroy, #microService_AddEndpoint, #microServiceConfig,
 * #microEndpointConfig
 */
NATS_EXTERN microError *
micro_AddService(microService **new_microservice, natsConnection *nc, microServiceConfig *config);

/** @brief Adds an endpoint to a microservice and starts listening for messages.
 *
 * Endpoints are currently destroyed when the service is stopped, there is no
 * present way to remove or stop individual endpoints.
 *
 * @param m the #microService that the endpoint will be added to.
 * @param config a #microEndpointConfig object with the configuration of the
 * endpoint. See #micro_endpoint_config_s for descriptions of the fields.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microService_Destroy, #microEndpointConfig
 */
NATS_EXTERN microError *
microService_AddEndpoint(microService *m, microEndpointConfig *config);

/** @brief Adds an group (prefix) to a microservice.
 *
 * Groups are associated with a service, and are destroyed when the service is
 * destroyed.
 *
 * @param new_group the location where to store the pointer to the new
 * #microGroup object.
 * @param m the #microService that the group will be added to.
 * @param prefix a prefix to use on names and subjects of all endpoints in the
 * group.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microGroup_AddGroup, #microGroup_AddEndpoint
 */
NATS_EXTERN microError *
microService_AddGroup(microGroup **new_group, microService *m, const char *prefix);

/** @brief Destroys a microservice, stopping it first if needed.
 *
 * @note This function may fail while stopping the service, do not assume
 * unconditional success if clean up is important.
 * 
 * @param m the #microService to stop and destroy.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microService_Stop, #microService_Run
 */
NATS_EXTERN microError *
microService_Destroy(microService *m);

/** @brief Returns the connection associated with the service. If the service
 * was successfully started, it is safe to assume it's not NULL, however it may
 * already have been disconnected or closed.
 *
 * @param m the #microService.
 *
 * @return a #natsConnection associated with the service.
 */
NATS_EXTERN natsConnection *
microService_GetConnection(microService *m);

/** @brief Returns a #microServiceInfo for a microservice.
 *
 * @note the #microServiceInfo struct returned by this call should be freed with
 * #microServiceInfo_Destroy.
 *
 * @param new_info receives the pointer to the #microServiceInfo.
 * @param m the #microService to query.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microServiceInfo, #micro_service_info_s
 */
NATS_EXTERN microError *
microService_GetInfo(microServiceInfo **new_info, microService *m);

/** @brief Returns the pointer to state data (closure). It is originally
 * provided in #microServiceConfig.State.
 *
 * @param m the #microService.
 *
 * @return the state pointer.
 *
 * @see #microServiceConfig
 */
NATS_EXTERN void *
microService_GetState(microService *m);

/** @brief Returns run-time statistics for a microservice.
 *
 * @note the #microServiceStats struct returned by this call should be freed
 * with #microServiceStats_Destroy.
 *
 * @param new_stats receives the pointer to the #microServiceStats.
 * @param m the #microService to query.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microServiceStats, #micro_service_stats_s
 */
NATS_EXTERN microError *
microService_GetStats(microServiceStats **new_stats, microService *m);

/** @brief Checks if the service is stopped.
 *
 * @param m the #microService.
 *
 * @return `true` if stopped, otherwise `false`.
 *
 * @see #micro_AddService, #microService_Stop, #microService_Run
 */
NATS_EXTERN bool
microService_IsStopped(microService *m);

/** @brief Waits for a microservice to stop.
 *
 * #micro_AddService starts the service with async subscriptions.
 * #microService_Run waits for the service to stop.
 *
 * @param m the #microService.
 *
 * @return a #microError for invalid arguments, otherwise always succeeds.
 *
 * @see #micro_AddService, #microService_Stop
 */
NATS_EXTERN microError *
microService_Run(microService *m);

/** @brief Stops a running microservice.
 *
 * Drains and closes the all subscriptions (endpoints and monitoring), resets
 * the stats, and calls the `Done` callback for the service, so it can do its
 * own clean up if needed.
 *
 * It is possible that this call encounters an error while stopping the service,
 * in which case it aborts and returns the error. The service then may be in a
 * partially stopped state, and the `Done` callback will not have been called.
 *
 * @param m the #microService.
 *
 * @return a #microError if an error occurred.
 *
 * @see #micro_AddService, #microService_Run
 */
NATS_EXTERN microError *microService_Stop(microService *m);

/** @} */ // end of microServiceFunctions

/** \defgroup microGroupFunctions microGroup
 *
 *  Functions that operate with #microGroup.
 *  @{
 */

/** @brief Adds a sub-group to #microGroup.
 *
 * The new subgroup will be prefixed as "parent_prefix.prefix.". Groups are
 * associated with a service, and are destroyed when the service is destroyed.
 *
 * @param new_group the location where to store the pointer to the new
 * #microGroup object.
 * @param parent the #microGroup that the new group will be added to.
 * @param prefix a prefix to use on names and subjects of all endpoints in the
 * group.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microGroup_AddGroup, #microGroup_AddEndpoint
 */
NATS_EXTERN microError *
microGroup_AddGroup(microGroup **new_group, microGroup *parent, const char *prefix);

/** @brief Adds an endpoint to a #microGroup and starts listening for messages.
 *
 * This is a convenience method to add an endpoint with its `Subject` and `Name`
 * prefixed with the group's prefix.
 *
 * @param g the #microGroup that the endpoint will be added to.
 * @param config a #microEndpointConfig object with the configuration of the
 * endpoint. See #micro_endpoint_config_s for descriptions of the fields.
 *
 * @return a #microError if an error occurred.
 *
 * @see #microService_Destroy, #microService_AddEndpoint, #microEndpointConfig
 */
NATS_EXTERN microError *
microGroup_AddEndpoint(microGroup *g, microEndpointConfig *config);

/** @} */ // end of microGroupFunctions

/** \defgroup microRequestFunctions microRequest
 *
 *  Functions that operate with #microRequest.
 *  @{
 */

/** @brief Adds a header to the underlying NATS request message.
 *
 * @param req the request.
 * @param key the key under which the `value` will be stored. It can't ne `NULL`
 * or empty.
 * @param value the string to add to the values associated with the given `key`.
 * The value can be `NULL` or empty string.
 *
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Add, #microRequest_DeleteHeader, #microRequest_GetHeaderKeys,
 * #microRequest_GetHeaderValue, #microRequest_GetHeaderValues
 */
NATS_EXTERN microError *
microRequest_AddHeader(microRequest *req, const char *key, const char *value);

/** @brief Deletes a header from the underlying NATS request message.
 *
 * @param req the request.
 * @param key the key to delete from the headers map.
 *
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Delete, #microRequest_AddHeader
 */
NATS_EXTERN microError *
microRequest_DeleteHeader(microRequest *req, const char *key);

/** @brief Returns the connection associated with the request. 
 *
 * @param req the request.
 *
 * @return a #natsConnection associated with the request. In fact, it's the
 * connection associated with the service, so by the time the request handler is
 * invoked the connection state may be different from when it was received.
 */
NATS_EXTERN natsConnection *
microRequest_GetConnection(microRequest *req);

/** @brief Returns the data in the the request, as a byte array. 
 *
 * @note The request owns the data, so it should not be freed other than with
 * `microRequest_Destroy`.
 * @note The data is not `NULL` terminated. Use `microRequest_GetDataLength` to
 * obtain the number of bytes.
 *
 * @param req the request.
 *
 * @return a pointer to the request's data.
 * 
 * @see #natsMsg_GetData, #microRequest_GetDataLength
 */
NATS_EXTERN const char *
microRequest_GetData(microRequest *req);

/** @brief Returns the number of data bytes in the the request. 
 *
 * @param req the request.
 *
 * @return the number of data bytes in the request.
 * 
 * @see #natsMsg_GetDataLength, #microRequest_GetData
 */
NATS_EXTERN int
microRequest_GetDataLength(microRequest *req);

/** \brief Returns the pointer to the user-provided endpoint state, if
 * the request is associated with an endpoint. 
 *
 * @param req the request.
 *
 * @return the `state` pointer provided in microEndpointConfig.
 *
 * @see #microEndpointConfig, #micro_endpoint_config_s
 */
NATS_EXTERN void *
microRequest_GetEndpointState(microRequest *req);

/** @brief Gets the list of all header keys in the NATS message underlying the
 * request.
 *
 * The returned strings are own by the library and MUST not be freed or altered.
 * However, the returned array `keys` MUST be freed by the user.
 *
 * @param req the request.
 * @param keys the memory location where the library will store the pointer to
 * the array of keys.
 * @param count the memory location where the library will store the number of
 * keys returned.
 *
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Keys, #microRequest_GetHeaderValue, #microRequest_GetHeaderValues
 */
NATS_EXTERN microError *
microRequest_GetHeaderKeys(microRequest *req, const char ***keys, int *count);

/** @brief Get the header entry associated with `key` from the NATS message underlying the request.
 *
 * @param req the request.
 * @param key the key for which the value is requested.
 * @param value the memory location where the library will store the pointer to the first
 * value (if more than one is found) associated with the `key`.
 * 
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Get, #microRequest_GetHeaderValue, #microRequest_GetHeaderValues
 */
NATS_EXTERN microError *
microRequest_GetHeaderValue(microRequest *req, const char *key, const char **value);

/** @brief Get all header values associated with `key` from the NATS message
 * underlying the request.
 *
 * @param req the request.
 * @param key the key for which the values are requested.
 * @param values the memory location where the library will store the pointer to
 * the array value (if more than one is found) associated with the `key`.
 * @param count the memory location where the library will store the number of
 * values returned.
 *
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Values, #microRequest_GetHeaderValue,
 * #microRequest_GetHeaderKeys
 */
NATS_EXTERN microError *
microRequest_GetHeaderValues(microRequest *req, const char *key, const char ***values, int *count);

/** @brief Get the NATS message underlying the request.
 *
 * @param req the request.
 *
 * @return the pointer to #natsMsg.
 */
NATS_EXTERN natsMsg *
microRequest_GetMsg(microRequest *req);

/** @brief Returns the reply subject set in this message.
 *
 * Returns the reply, possibly `NULL`.
 *
 * @warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 *
 * @param req the request.
 */
NATS_EXTERN const char *
microRequest_GetReply(microRequest *req);

/** @brief Returns the pointer to the microservice associated with the request. 
 *
 * @param req the request.
 *
 * @return the microservice pointer.
 */
NATS_EXTERN microService *
microRequest_GetService(microRequest *req);

/** @brief Returns the pointer to the user-provided service state.
 *
 * @param req the request.
 *
 * @return the `state` pointer provided in microServiceConfig.
 *
 * @see #microServiceConfig, #micro_service_config_s
 */
NATS_EXTERN void *
microRequest_GetServiceState(microRequest *req);

/** @brief Returns the subject of the request message.
 *
 * @warning The string belongs to the message and must not be freed.
 * Copy it if needed.
 *
 * @param req the request.
 */
NATS_EXTERN const char *
microRequest_GetSubject(microRequest *req);

/**
 * @brief Respond to a request, on the same NATS connection.
 *
 * @param req the request.
 * @param data the response data.
 * @param len the length of the response data.
 *
 * @return an error, if any. 
 */
NATS_EXTERN microError *
microRequest_Respond(microRequest *req, const char *data, size_t len);

/**
 * @brief Respond to a request with a simple error.
 *
 * If err is NULL, `RespondError` does nothing.
 * 
 * @note microRequest_RespondError is called automatially if the handler returns
 * an error. Usually, there is no need for a handler to use this function
 * directly. If the request
 *
 * @param req the request.
 * @param err the error to include in the response header. If `NULL`, no error.
 *
 * @return an error, if any. 
 */
NATS_EXTERN microError *
microRequest_RespondError(microRequest *req, microError *err);

/**
 * @brief Respond to a message, with an OK or an error.
 *
 * If err is NULL, `RespondErrorWithData` is equivalent to `Respond`. If err is
 * not NULL, the response will include the error in the response header, and err
 * will be freed. 
 *
 * The following example illustrates idiomatic usage in a request handler. Since
 * this handler handles its own error responses, the only error it might return
 * would be a failure to send the response.
 *
 * \code{.c}
 * err = somefunc();
 * if (err != NULL) {
 *     return microRequest_RespondCustom(req, err, error_data, data_len);
 * }
 * ...
 * \endcode
 *
 * Or, if the request handler has its own cleanup logic:
 * 
 * \code{.c}
 * if (err = somefunc(), err != NULL)
 *     goto CLEANUP;
 * ...
 *
 * CLEANUP:
 * if (err != NULL) {
 *     return microRequest_RespondCustom(req, err, error_data, data_len);
 * }
 * return NULL;
 * \endcode
 *
 * @param req the request.
 * @param err the error to include in the response header. If `NULL`, no error.
 * @param data the response data.
 * @param len the length of the response data.
 *
 * @note 
 *
 *
 * @return an error, if any. 
 */
NATS_EXTERN microError *
microRequest_RespondCustom(microRequest *req, microError *err, const char *data, size_t len);

/** @brief Add `value` to the header associated with `key` in the NATS message
 * underlying the request.
 *
 * @param req the request.
 * @param key the key under which the `value` will be stored. It can't ne `NULL`
 * or empty.
 * @param value the string to store under the given `key`. The value can be
 * `NULL` or empty string.
 *
 * @return a #microError if an error occurred.
 *
 * @see #natsMsgHeader_Set
 */
NATS_EXTERN microError *
microRequest_SetHeader(microRequest *req, const char *key, const char *value);

/** @} */ // end of microRequestFunctions

/** \defgroup microErrorFunctions microError
 *
 *  Functions that create and manipulate #microError.
 *  @{
 */

/** @brief creates a new #microError, with a printf-like formatted message.
 *
 * @note Errors must be freed with #microError_Destroy, but often they are
 * simply returned up the call stack.
 *
 * @param format printf-like format.
 *
 * @return a new #microError or micro_ErrorOutOfMemory.
 */
NATS_EXTERN microError *
micro_Errorf(const char *format, ...);

/** @brief creates a new #microError, with a code and a printf-like formatted
 * message.
 *
 * @note Errors must be freed with #microError_Destroy, but often they are
 * simply returned up the call stack.
 *
 * @param code an `int` code, loosely modeled after the HTTP status code.
 * @param format printf-like format.
 *
 * @return a new #microError or micro_ErrorOutOfMemory.
 */
NATS_EXTERN microError *
micro_ErrorfCode(int code, const char *format, ...);

/** @brief Wraps a NATS status into a #microError, if not a NATS_OK.
 *
 * @param s NATS status of receiving the response.
 *
 * @return a new #microError or `NULL` if no error if found.
 */
NATS_EXTERN microError *
micro_ErrorFromStatus(natsStatus s);

/** @brief returns the int code of the error.
 *
 * @param err the error.
 *
 * @return the int code.
 */
NATS_EXTERN int
microError_Code(microError *err);

/** @brief destroys a #microError.
 *
 * @param err the error.
 */
NATS_EXTERN void
microError_Destroy(microError *err);

#define microError_Ignore(__err) microError_Destroy(__err)

/**
 * @brief Returns the NATS status associated with the error.
 * 
 * @param err 
 * 
 * @return the status 
 */
NATS_EXTERN natsStatus
microError_Status(microError *err);

/**
 * @brief Returns a printable string with the error message.
 *
 * This function outputs into a user-provided buffer, and returns a "`%s`-able"
 * pointer to it.
 *
 * @param err the error.
 * @param buf the output buffer.
 * @param len the capacity of the output buffer.
 * @return `buf` 
 */
NATS_EXTERN const char *
microError_String(microError *err, char *buf, size_t len);

/**
 * @brief Wraps an exising #microError with a higher printf-like formatted
 * message.
 *
 * @warning The original error may be freed and should not be refered to after
 * having been wrapped.
 *
 * @param err the original error
 * @param format the new message to prepend to the original error message.
 * @param ... 
 *
 * @return a new, wrapped, error.
 */
NATS_EXTERN microError *
microError_Wrapf(microError *err, const char *format, ...);

/** @} */ // end of microErrorFunctions

/** \defgroup microClientFunctions microClient
 *
 *  @{
 */

/**
 * @brief Creates a new microservice client.
 * 
 * @param new_client received the pointer to the new client.
 * @param nc a NATS connection.
 * @param cfg for future use, use NULL for now.
 * 
 * @return a #microError if an error occurred. 
 */
NATS_EXTERN microError *
micro_NewClient(microClient **new_client, natsConnection *nc, microClientConfig *cfg);

/**
 * @brief Destroys a microservice client.
 * 
 * @param client the client to destroy.
 */
NATS_EXTERN void
microClient_Destroy(microClient *client);

/**
 * @brief Sends a request to a microservice and receives the response.
 *
 * @param reply receives the pointer to the response NATS message. `reply` must
 * be freed with #natsMsg_Destroy.
 * @param client the client to use.
 * @param subject the subject to send the request on.
 * @param data the request data.
 * @param data_len the request data length.
 * 
 * @return a #microError if an error occurred.
 */
NATS_EXTERN microError *
microClient_DoRequest(natsMsg **reply, microClient *client, const char *subject, const char *data, int data_len);

/** @} */ // end of microClientFunctions

/** \defgroup microCleanupFunctions Miscellaneous
 *
 *  Functions to destroy miscellaneous objects.
 *  @{
 */

/**
 * @brief Destroys a #microServiceInfo object.
 * 
 * @param info the object to destroy.
 */
NATS_EXTERN void
microServiceInfo_Destroy(microServiceInfo *info);

/**
 * @brief Destroys a #microServiceStats object.
 * 
 * @param stats the object to destroy.
 */
NATS_EXTERN void
microServiceStats_Destroy(microServiceStats *stats);

/** @} */ // end of microCleanupFunctions

/** @} */ // end of microFunctions

/** @} */ // end of microGroup

/**  \defgroup wildcardsGroup Wildcards
 *  @{
 *  Use of wildcards. There are two type of wildcards: `*` for partial,
 *  and `>` for full.
 *
 *  A subscription on subject `foo.*` would receive messages sent to:
 *  - `foo.bar`
 *  - `foo.baz`
 *
 *  but not on:
 *
 *  - `foo.bar.baz` (too many elements)
 *  - `bar.foo`. (does not start with `foo`).
 *
 *  A subscription on subject `foo.>` would receive messages sent to:
 *  - `foo.bar`
 *  - `foo.baz`
 *  - `foo.bar.baz`
 *
 *  but not on:
 *  - `foo` (only one element, needs at least two)
 *  - `bar.baz` (does not start with `foo`).
 ** @} */

/**  \defgroup envVariablesGroup Environment Variables
 *  @{
 *  You will find here the environment variables that change the default behavior
 *  of the NATS C Client library.
 * <br><br>
 * Name | Effect
 * -----|:-----:
 * <b>`NATS_DEFAULT_TO_LIB_MSG_DELIVERY`</b> | On #nats_Open, the library looks for this environment variable. If set (to any value), the library will default to using a global thread pool to perform message delivery. See #natsOptions_UseGlobalMessageDelivery and #nats_SetMessageDeliveryPoolSize.
 *
 ** @} */


#ifdef __cplusplus
}
#endif

#endif /* NATS_H_ */
