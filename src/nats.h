// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef NATS_H_
#define NATS_H_

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

#ifdef __cplusplus
extern "C" {
#endif

/*! \mainpage %NATS C client.
 *
 * \section intro_sec Introduction
 *
 * The %NATS C Client is part of %NATS, an open-source cloud-native
 * messaging system, and is supported by [Apcera](http://www.apcera.com).
 * This client, written in C, follows the go client closely, but
 * diverges in some places.
 *
 * \section install_sec Installation
 *
 * Instructions to build and install the %NATS C Client can be
 * found at the [NATS C Client GitHub page](https://github.com/nats-io/cnats)
 *
 * \section other_doc_section Other Documentation
 *
 * This documentation focuses on the %NATS C Client API; for additional
 * information, refer to the following:
 *
 * - [General Documentation for nats.io](http://nats.io/documentation)
 * - [NATS C Client found on GitHub](https://github.com/nats-io/cnats)
 * - [The NATS Server (gnatsd) found on GitHub](https://github.com/nats-io/gnatsd)
 */

/** \brief The default `NATS Server` URL.
 *
 *  This is the default URL a `NATS Server`, running with default listen
 *  port, can be reached at.
 */
#define NATS_DEFAULT_URL "nats://localhost:4222"

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
 * @see natsConnection_Subscribe()
 * @see natsConnection_QueueSubscribe()
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
        int             socket);

/** \brief Read event needs to be added or removed.
 *
 * The `NATS` library will invoked this callback to indicate if the event
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
 * The `NATS` library will invoked this callback to indicate if the event
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
 * The `NATS` library will invoked this callback to indicate that the connection
 * no longer needs to be attached to the event loop. User can cleanup some state.
 *
 * @param userData the pointer to an user object created in #natsEvLoop_Attach.
 */
typedef natsStatus (*natsEvLoop_Detach)(
        void            *userData);

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
 * @param lockSpinCount The number of times the library will spin trying to
 * lock a mutex object.
 */
NATS_EXTERN natsStatus
nats_Open(int64_t lockSpinCount);


/** \brief Returns the Library's version
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

/** \brief Tear down the library.
 *
 * Releases memory used by the library.
 *
 * \note For this to take effect, all NATS objects that you have created
 * must first be destroyed.
 */
NATS_EXTERN void
nats_Close(void);

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
natsStatistics_GetCounts(natsStatistics *stats,
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
 * @param opts the pointer to the #natsOptions object.
 * @param servers the array of strings representing the server URLs.
 * @param serversCount the size of the array.
 */
NATS_EXTERN natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount);

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
 * Note that the certificates
 * are added to a SSL context for this #natsOptions object at the time of
 * this call, so possible errors while loading the certificates will be
 * reported now instead of when a connection is created. You can get extra
 * information by calling #nats_GetLastError.
 *
 * @param opts the pointer to the #natsOptions object.
 * @param fileName the file containing the CA certificates.
 *
 */
NATS_EXTERN natsStatus
natsOptions_LoadCATrustedCertificates(natsOptions *opts, const char *fileName);

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

/** \brief Sets the list of available ciphers.
 *
 * Sets the list of available ciphers.
 * Check https://www.openssl.org/docs/manmaster/apps/ciphers.html for the
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
 * is lost. There could be two instances of the callback when reconnection
 * is allowed: one before attempting the reconnect attempts, and one when
 * all reconnect attempts have failed and the connection is going to be
 * permanently closed.
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
natsInbox_Create(char **newInbox);

/** \brief Destroys the inbox.
 *
 * Destroys the inbox.
 *
 * @param inbox the pointer to the #natsInbox object to destroy.
 */
NATS_EXTERN void
natsInbox_Destroy(char *inbox);

/** @} */ // end of inboxGroup

/** \defgroup msgGroup Message
 *
 *  NATS Message.
 *  @{
 */

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
natsMsg_GetSubject(natsMsg *msg);

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
natsMsg_GetReply(natsMsg *msg);

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
natsMsg_GetData(natsMsg *msg);

/** \brief Returns the message length.
 *
 * Returns the message's payload length, possibly 0.
 *
 * @param msg the pointer to the #natsMsg object.
 */
NATS_EXTERN int
natsMsg_GetDataLength(natsMsg *msg);

/** \brief Destroys the message object.
 *
 * Destroys the message, freeing memory.
 *
 * @param msg the pointer to the #natsMsg object to destroy.
 */
NATS_EXTERN void
natsMsg_Destroy(natsMsg *msg);

/** @} */ // end of msgGroup

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
 * status #CLOSED would be returned instead.
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
 * Creates a #natsInbox and performs a #natsConnection_PublishRequest() call
 * with the reply set to that inbox. Returns the first reply received.
 * This is optimized for the case of multiple responses.
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
 * @param nextMsg the location where to store the pointer to the next availabe
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

/** @} */ // end of funcGroup

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


#ifdef __cplusplus
}
#endif

#endif /* NATS_H_ */
