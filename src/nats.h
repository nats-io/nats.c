// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef NATS_H_
#define NATS_H_

#include <stdint.h>
#include <stdbool.h>

// TODO: Probably needs to be adapted for Windows port
#include <inttypes.h>
#define NATS_PRINTF_U64     PRIu64
#define NATS_PRINTF_D64     PRId64

#include "status.h"

static const char *NATS_DEFAULT_URL = "nats://localhost:4222";

//
// Types.
//

/*
 * A natsConnection represents a bare connection to a NATS server. It will
 * send and receive byte array payloads.
 */
typedef struct __natsConnection     natsConnection;

/*
 * Tracks various statistics received and sent on a connection,
 * including counts for messages and bytes.
 */
typedef struct __natsStatistics     natsStatistics;

/*
 * A natsSubscription represents interest in a given subject.
 */
typedef struct __natsSubscription   natsSubscription;

/*
 * natsMsg is a structure used by Subscribers and natsPublishMsg().
 */
typedef struct __natsMsg            natsMsg;

/*
 * Options can be used to create a customized Connection.
 */
typedef struct __natsOptions        natsOptions;

/*
 * This can be used as the reply for a request. Inbox are meant to be
 * unique so that replies can be sent to a specific subscriber. That
 * being said, inboxes can be shared across multiple subscribers if
 * desired.
 */
typedef char                        natsInbox;


//
// Callbacks.
//

/*
 * natsMsgHandler is a callback function that processes messages delivered to
 * asynchronous subscribers.
 */
typedef void (natsMsgHandler)(
        natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

/*
 * natsConnectionHandler is used for asynchronous events such as disconnected
 * and closed connections.
 */
typedef void (natsConnectionHandler)(
        natsConnection  *nc, void *closure);

/*
 * ErrHandlers are used to process asynchronous errors encountered while processing
 * inbound messages.
 */
typedef void (natsErrHandler)(
        natsConnection *nc, natsSubscription *subscription, natsStatus err,
        void *closure);


//
// Functions.
//

/*
 * This initializes the library.
 *
 * It is invoked automatically when creating a connection, using a default
 * spin count. However, you can call this explicitly before creating the very
 * first connection in order for your chosen spin count to take effect.
 */
natsStatus
nats_Open(int64_t lockSpinCount);

/*
 * Releases memory used by the library. Note that for this to take effect,
 * all NATS objects that you have created must first be destroyed.
 */
void
nats_Close(void);

/*
 * Creates a statistics object that can be passed to natsConnection_GetStats().
 */
natsStatus
natsStatistics_Create(natsStatistics **newStats);

/*
 * Gets the counts out of the statistics object. Note that you can pass NULL
 * to any of the count your are not interested in getting.
 */
natsStatus
natsStatistics_GetCounts(natsStatistics *stats,
                         uint64_t *inMsgs, uint64_t *inBytes,
                         uint64_t *outMsgs, uint64_t *outBytes,
                         uint64_t *reconnects);

/*
 * Destroys the statistics object, freeing up memory.
 */
void
natsStatistics_Destroy(natsStatistics *stats);

/*
 * Creates a natsOptions object. This object is used when one wants to set
 * specific options prior to connecting to the NATS server.
 *
 * After making the appropriate natsOptions_Set calls, this object is passed
 * to the natsConnection_Connect() call, which will clone this object. After
 * natsConnection_Connect() returns, modifications to the options object
 * will not affect the connection.
 *
 * The options object should be destroyed when no longer used needed.
 */
natsStatus
natsOptions_Create(natsOptions **newOpts);

/*
 * Sets the URL of the NATS server the client should try to connect to.
 * The URL can contain optional user name and password.
 *
 * Some valid URLS:
 *
 * nats://localhost:4222
 * nats://user@localhost:4222
 * nats://user:password@localhost:4222
 *
 */
natsStatus
natsOptions_SetURL(natsOptions *opts, const char *url);

/*
 * This specifies a list of servers to try to connect to. Note that if you
 * call natsOptions_SetURL() too, the actual list will contain the one
 * from natsOptions_SetURL() and the ones specified in this call.
 */
natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount);

/*
 * If 'noRandomize' is true, then the list of server URLs is used in the order
 * provided by natsOptions_SetURL() + natsOptions_SetServers(). Otherwise, the
 * list is formed in a random order.
 */
natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize);

/*
 * This timeout, expressed in milliseconds, is used to interrupt a (re)connect
 * attempt to a NATS server. This timeout is used both for the low level TCP
 * connect call, and for timing out the response from the server to the client's
 * initial PING protocol.
 */
natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout);

/*
 * This name is sent as part of the CONNECT protocol. There is no default name.
 */
natsStatus
natsOptions_SetName(natsOptions *opts, const char *name);

/*
 * Sets the verbose mode. The default is 'false'.
 */
natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose);

/*
 * Sets the pedantic mode. The default is 'false'
 */
natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic);

/*
 * Interval, expressed in milliseconds, in which the client sends PING
 * protocols to the NATS server.
 */
natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval);

/*
 * Specifies the maximum number of PINGs without corresponding PONGs (which
 * should be received from the server) before closing the connection with
 * the STALE_CONNECTION status. If reconnection is allowed, the client
 * library will try to reconnect.
 */
natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPignsOut);

/*
 * Specifies whether or not the client library should try to reconnect when
 * losing the connection to the NATS server.
 */
natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow);

/*
 * Specifies the maximum number of reconnect attempts.
 */
natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect);

/*
 * Specifies how long to wait between two reconnect attempts.
 */
natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait);

/*
 * Specifies the maximum number of inbound messages can be buffered in the
 * library before severing the connection with a SLOW_CONSUMER status.
 */
natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending);

/*
 * Specifies the callback to invoke when an asynchronous error
 * occurs. This is used by applications having only asynchronous
 * subscriptions that would not know otherwise that a problem with the
 * connection occurred.
 */
natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure);

/*
 * Specifies the callback to invoke when a connection is terminally closed,
 * that is, after all reconnect attempts have failed (when reconnection is
 * allowed).
 */
natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure);

/*
 * Specifies the callback to invoke when a connection to the NATS server
 * is lost. There could be two instances of the callback when reconnection
 * is allowed: one before attempting the reconnect attempts, and one when
 * all reconnect attempts have failed and the connection is going to be
 * permanently closed.
 */
natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure);

/*
 * Specifies the callback to invoke when the client library has successfully
 * reconnected to a NATS server.
 */
natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure);

/*
 * Destroys the natsOptions object, freeing used memory. See the note in
 * the natsOptions_Create() call.
 */
void
natsOptions_Destroy(natsOptions *opts);


/*
 * Gives the current time in milliseconds.
 */
int64_t
nats_Now(void);

/*
 * Gives the current time in nanoseconds. When such granularity is not
 * available, the time returned is still expressed in nanoseconds.
 */
int64_t
nats_NowInNanoSeconds(void);

/*
 * This sleeps for the given number of milliseconds.
 */
void
nats_Sleep(int64_t sleepTime);

/*
 * Returns an inbox string which can be used for directed replies from
 * subscribers. These are guaranteed to be unique, but can be shared
 * and subscribed to by others.
 */
natsStatus
natsInbox_Create(char **newInbox);

/*
 * Destroys the inbox.
 */
void
natsInbox_Destroy(char *inbox);


/*
 * Creates a natsMsg object. This is used by the subscription related calls
 * and by natsConnection_PublishMsg().
 *
 * Messages need to be destroyed with natsMsg_Destroy() when no longer needed.
 */
natsStatus
natsMsg_Create(natsMsg **newMsg, const char *subj, const char *reply,
               const char *data, int dataLen);

/*
 * Returns the subject inside the message object. The string belongs to the
 * message and must not be freed. Copy it if needed.
 */
const char*
natsMsg_GetSubject(natsMsg *msg);

/*
 * Returns the reply inside the message object. The string belongs to the
 * message and must not be freed. Copy it if needed.
 */
const char*
natsMsg_GetReply(natsMsg *msg);

/*
 * Returns the message payload. It belongs to the message and must not be
 * freed. Copy it if needed.
 */
const char*
natsMsg_GetData(natsMsg *msg);

/*
 * Returns the message's payload length.
 */
int
natsMsg_GetDataLength(natsMsg *msg);

/*
 * Destroys the message, freeing memory.
 */
void
natsMsg_Destroy(natsMsg *msg);


/*
 * Attempts to connect to a NATS server with multiple options.
 */
natsStatus
natsConnection_Connect(natsConnection **nc, natsOptions *options);

/*
 * Attempts to connect to a NATS server at the given url.
 */
natsStatus
natsConnection_ConnectTo(natsConnection **nc, const char *url);

/*
 * Tests if connection has been closed.
 */
bool
natsConnection_IsClosed(natsConnection *nc);

/*
 * Tests if connection is reconnecting.
 */
bool
natsConnection_IsReconnecting(natsConnection *nc);

/*
 * Returns the current state of the connection.
 */
natsConnStatus
natsConnection_Status(natsConnection *nc);

/*
 * Performs a round trip to the server and return when it receives the
 * internal reply.
 */
natsStatus
natsConnection_Flush(natsConnection *nc);

/*
 * Returns the number of bytes to be sent to the server, or -1 if the
 * connection is closed.
 */
int
natsConnection_Buffered(natsConnection *nc);

/*
 * Performs a round trip to the server and return when it receives the
 * internal reply, or if the call times-out (timeout is expressed in
 * milliseconds).
 */
natsStatus
natsConnection_FlushTimeout(natsConnection *nc, int64_t timeout);

/*
 * Returns the maximum message pay-load accepted by the server.
 */
int64_t
natsConnection_GetMaxPayload(natsConnection *nc);

/*
 * Copies in the provided statistics structure, a snapshot of the statistics for
 * this connection.
 */
natsStatus
natsConnection_GetStats(natsConnection *nc, natsStatistics *stats);

/*
 * Copies in the given buffer, the connected server's Url. If the buffer is too small,
 * an error is returned.
 */
natsStatus
natsConnection_GetConnectedUrl(natsConnection *nc, char *buffer, size_t bufferSize);

/*
 * Copies in the given buffer, the connected server's Id. If the buffer is too small,
 * an error is returned.
 */
natsStatus
natsConnection_GetConnectedServerId(natsConnection *nc, char *buffer, size_t bufferSize);

/*
 * Returns the last known error as a 'natsStatus' and the location to the
 * null-terminated error string. Note that the string is owned by the
 * connection object and should not be freed.
 */
natsStatus
natsConnection_GetLastError(natsConnection *nc, const char **lastError);

/*
 * Closes the connection to the server. This call will release all blocking
 * calls, such as natsConnection_Flush() and natsSubscription_NextMsg().
 * The connection object is still usable until the call to
 * natsConnection_Destroy().
 */
void
natsConnection_Close(natsConnection *nc);

/*
 * Destroys the connection object, freeing up memory.
 * If not already done, this call first closes the connection to the server.
 */
void
natsConnection_Destroy(natsConnection *nc);

/*
 * Publishes the data argument to the given subject. The data argument is left
 * untouched and needs to be correctly interpreted on the receiver.
 */
natsStatus
natsConnection_Publish(natsConnection *nc, const char *subj,
                       const void *data, int dataLen);

/*
 * Convenient function to publish a string. This call is equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsConnection_Publish(nc, subj, (const void*) myString, (int) strlen(myString));
 */
natsStatus
natsConnection_PublishString(natsConnection *nc, const char *subj,
                             const char *str);

/*
 * Publishes the natsMsg structure, which includes the subject, an optional
 * reply and optional data.
 */
natsStatus
natsConnection_PublishMsg(natsConnection *nc, natsMsg *msg);

/*
 * Publishes the data argument to the given subject expecting a response on
 * the reply subject. Use natsConnection_Request() for automatically waiting for a
 * response inline.
 */
natsStatus
natsConnection_PublishRequest(natsConnection *nc, const char *subj,
                              const char *reply, const void *data, int dataLen);

/*
 * Convenient function to publish a request as a string. This call is
 * equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsPublishRequest(nc, subj, reply, (const void*) myString,
 *                    (int) strlen(myString));
 */
natsStatus
natsConnection_PublishRequestString(natsConnection *nc, const char *subj,
                                    const char *reply, const char *str);

/*
 * Creates a natsInbox and performs a natsConnection_PublishRequest() call
 * with the reply set to that inbox. Returns the first reply received.
 * This is optimized for the case of multiple responses.
 */
natsStatus
natsConnection_Request(natsMsg **replyMsg, natsConnection *nc, const char *subj,
                       const void *data, int dataLen, int64_t timeout);

/*
 * Convenient function to send a request as a string. This call is
 * equivalent to:
 *
 * const char* myString = "hello";
 *
 * natsConnection_Request(replyMsg, nc, subj,
 *                        (const void*) myString, (int) strlen(myString));
 */
natsStatus
natsConnection_RequestString(natsMsg **replyMsg, natsConnection *nc,
                             const char *subj, const char *str,
                             int64_t timeout);

/*
 * Expresses interest in the given subject. The subject can have wildcards
 * (partial:*, full:>). Messages will be delivered to the associated
 * natsMsgHandler.
 */
natsStatus
natsConnection_Subscribe(natsSubscription **sub, natsConnection *nc,
                         const char *subject, natsMsgHandler cb,
                         void *cbClosure);

/*
 * Similar to natsSubscribe, but creates a synchronous subscription that can
 * be polled via natsSubscription_NextMsg().
 */
natsStatus
natsConnection_SubscribeSync(natsSubscription **sub, natsConnection *nc,
                             const char *subject);

/*
 * Creates an asynchronous queue subscriber on the given subject.
 * All subscribers with the same queue name will form the queue group and
 * only one member of the group will be selected to receive any given
 * message asynchronously.
 */
natsStatus
natsConnection_QueueSubscribe(natsSubscription **sub, natsConnection *nc,
                              const char *subject, const char *queueGroup,
                              natsMsgHandler cb, void *cbClosure);

/*
 * Similar to natsQueueSubscribe, but creates a synchronous subscription that can
 * be polled via natsSubscription_NextMsg().
 */
natsStatus
natsConnection_QueueSubscribeSync(natsSubscription **sub, natsConnection *nc,
                                  const char *subject, const char *queueGroup);

/*
 * Return the next message available to a synchronous subscriber or block until
 * one is available.
 * A timeout (expressed in milliseconds) can be used to return when no message
 * has been delivered. If the value is zero, then this call will not wait and
 * return the next message that was pending in the client, and NATS_TIMEOUT
 * otherwise.
 */
natsStatus
natsSubscription_NextMsg(natsMsg **nextMsg, natsSubscription *sub,
                         int64_t timeout);

/*
 * Removes interest on the subject. Asynchronous subscription may still have
 * a callback in progress, in that case, the subscription will still be valid
 * until the callback returns.
 */
natsStatus
natsSubscription_Unsubscribe(natsSubscription *sub);

/*
 * This call issues an automatic natsSubscription_Unsubscribe that is
 * processed by the server when 'max' messages have been received.
 * This can be useful when sending a request to an unknown number
 * of subscribers.
 */
natsStatus
natsSubscription_AutoUnsubscribe(natsSubscription *sub, int max);

/*
 * Returns the number of queued messages in the client for this subscription.
 */
natsStatus
natsSubscription_QueuedMsgs(natsSubscription *sub, uint64_t *queuedMsgs);

/*
 * Returns a boolean indicating whether the subscription is still active.
 * This will return false if the subscription has already been closed,
 * or auto unsubscribed.
 */
bool
natsSubscription_IsValid(natsSubscription *sub);

/*
 * Destroys the subscription object, freeing up memory.
 * If not already done, this call will removes interest on the subject.
 */
void
natsSubscription_Destroy(natsSubscription *sub);


#endif /* NATS_H_ */
