// Copyright 2015 Apcera Inc. All rights reserved.

#ifndef STATUS_H_
#define STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

/// The connection state
typedef enum
{
    DISCONNECTED = 0, ///< The connection has been disconnected
    CONNECTING,       ///< The connection is in the process or connecting
    CONNECTED,        ///< The connection is connected
    CLOSED,           ///< The connection is closed
    RECONNECTING      ///< The connection is in the process or reconnecting

} natsConnStatus;

/// Status returned by most of the APIs
typedef enum
{
    NATS_OK         = 0,                ///< Success

    NATS_ERR,                           ///< Generic error
    NATS_PROTOCOL_ERROR,                ///< Error when parsing a protocol message,
                                        ///  or not getting the expected message.
    NATS_IO_ERROR,                      ///< IO Error (network communication).
    NATS_LINE_TOO_LONG,                 ///< The protocol message read from the socket
                                        ///  does not fit in the read buffer.

    NATS_CONNECTION_CLOSED,             ///< Operation on this connection failed because
                                        ///  the connection is closed.
    NATS_NO_SERVER,                     ///< Unable to connect, the server could not be
                                        ///  reached or is not running.
    NATS_STALE_CONNECTION,              ///< The server closed our connection because it
                                        ///  did not receive PINGs at the expected interval.
    NATS_SECURE_CONNECTION_WANTED,      ///< The client is configured to use TLS, but the
                                        ///  server is not.
    NATS_SECURE_CONNECTION_REQUIRED,    ///< The server expects a TLS connection.
    NATS_CONNECTION_DISCONNECTED,       ///< The connection was disconnected. Depending on
                                        ///  the configuration, the connection may reconnect.

    NATS_CONNECTION_AUTH_FAILED,        ///< The connection failed due to authentication error.
    NATS_NOT_PERMITTED,                 ///< The action is not permitted.
    NATS_NOT_FOUND,                     ///< An action could not complete because something
                                        ///  was not found. So far, this is an internal error.

    NATS_ADDRESS_MISSING,               ///< Incorrect URL. For instance no host specified in
                                        ///  the URL.

    NATS_INVALID_SUBJECT,               ///< Invalid subject, for instance NULL or empty string.
    NATS_INVALID_ARG,                   ///< An invalid argument is passed to a function. For
                                        ///  instance passing NULL to an API that does not
                                        ///  accept this value.
    NATS_INVALID_SUBSCRIPTION,          ///< The call to a subscription function fails because
                                        ///  the subscription has previously been closed.
    NATS_INVALID_TIMEOUT,               ///< Timeout must be positive numbers.

    NATS_ILLEGAL_STATE,                 ///< An unexpected state, for instance calling
                                        ///  #natsSubscription_NextMsg() on an asynchronous
                                        ///  subscriber.

    NATS_SLOW_CONSUMER,                 ///< The maximum number of messages waiting to be
                                        ///  delivered has been reached. Messages are dropped.

    NATS_MAX_PAYLOAD,                   ///< Attempt to send a payload larger than the maximum
                                        ///  allowed by the NATS Server.
    NATS_MAX_DELIVERED_MSGS,            ///< Attempt to receive more messages than allowed, for
                                        ///  instance because of #natsSubscription_AutoUnsubscribe().

    NATS_INSUFFICIENT_BUFFER,           ///< A buffer is not large enough to accommodate the data.

    NATS_NO_MEMORY,                     ///< An operation could not complete because of insufficient
                                        ///  memory.

    NATS_SYS_ERROR,                     ///< Some system function returned an error.

    NATS_TIMEOUT,                       ///< An operation timed-out. For instance
                                        ///  #natsSubscription_NextMsg().

    NATS_FAILED_TO_INITIALIZE,          ///< The library failed to initialize.
    NATS_NOT_INITIALIZED,               ///< The library is not yet initialized.

    NATS_SSL_ERROR                      ///< An SSL error occurred when trying to establish a
                                        ///  connection.

} natsStatus;

#ifdef __cplusplus
}
#endif

#endif /* STATUS_H_ */
