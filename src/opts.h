// Copyright 2015-2018 The NATS Authors
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

#ifndef OPTS_H_
#define OPTS_H_

#define CHECK_OPTIONS(o, c)     \
    if (((o) == NULL) || ((c))) \
        return nats_setDefaultError(NATS_INVALID_ARG);

typedef struct __natsMemOptions_s
{
    // Heap memory is generally allocated in chunks of this size, with the
    // exception of rawAllocs
    size_t heapPageSize;

    // The smallest amount of free space in a read buffer that warrants using it
    // for a read operation. If the buffer does not have this much space, do not
    // use it and get a new one to read into.
    size_t readBufferMin;

    // The size of a single read buffer.
    size_t readBufferSize;

    // The initial size of the write queue (number of buffer references to
    // hold).
    size_t writeQueueBuffers;

    // The maximum number of buffers that can be held pending in the write
    // queue.
    size_t writeQueueMaxBuffers;

    // The maximum number of bytes that can be held pending in the write queue.
    int64_t writeQueueMaxBytes;

    // Attempt to batch up to this many bytes (from small buffers) when writing
    // to the socket.
    size_t writeMinBytes;
} natsMemOptions;

static natsMemOptions nats_defaultMemOptions = {
    .heapPageSize = 4096,
    .readBufferMin = 1024,
    .readBufferSize = (64 * 1024),
    .writeQueueBuffers = 16,
    .writeQueueMaxBuffers = 1024,
    .writeQueueMaxBytes = 16 * 1024 * 1024,
    .writeMinBytes = (32 * 1024),
};

typedef struct __natsNetOptions_s
{
    bool noRandomize;
    int64_t timeout;
    bool allowReconnect;
    int maxReconnect;
    int64_t reconnectWait;
    int64_t writeDeadline;
    int orderIP; // possible values: 0,4,6,46,64
    // If set to true, pending requests will fail with
    // NATS_CONNECTION_DISCONNECTED when the library detects a disconnection.
    // Otherwise, the requests will remain pending and responses will be
    // processed upon reconnect.
    bool failRequestsOnDisconnect;
    // If set to true, in case of failed connect, tries again using
    // reconnect options values.
    bool retryOnFailedConnect;
    // Reconnect jitter added to reconnect wait
    int64_t reconnectJitter;
    int64_t reconnectJitterTLS;
    bool ignoreDiscoveredServers;

    natsOnConnectionEventF connected;
    void *connectedClosure;
    natsOnConnectionEventF closed;
    void *closedClosure;
    natsOnConnectionEventF error;
    void *errorClosure;
    natsOnConnectionEventF disconnected;
    void *disconnectedClosure;

} natsNetOptions;
typedef struct __natsSecureOptions_s
{
    bool secure;

} natsSecureOptions;

typedef struct __natsProtocolOptions_s
{
    bool verbose;
    bool pedantic;
    int64_t pingInterval;
    int maxPingsOut;

    // NoEcho configures whether the server will echo back messages
    // that are sent on this connection if we also have matching subscriptions.
    // Note this is supported on servers >= version 1.2. Proto 1 or greater.
    bool noEcho;

    // Disable the "no responders" feature.
    bool disableNoResponders;

} natsProtocolOptions;

struct __natsOptions
{
    // All options values are allocated in this pool, which is passed into
    // natsOpts_create.
    natsPool *pool;

    // All fields that are not "simple" types, including counters kuke
    // serversCount must be declared before __memcpy_from_here, and should be
    // manually added to nats_cloneOptions() to duplicate into the target opts'
    // pool.
    char *name;
    char *password;
    char **servers;
    int serversCount;
    char *token;
    char *user;
    char *url;

    uint8_t __memcpy_from_here;

    natsMemOptions mem;
    natsNetOptions net;
    natsSecureOptions secure;
    natsProtocolOptions proto;
};

#define NATS_OPTS_DEFAULT_MAX_RECONNECT (60)
#define NATS_OPTS_DEFAULT_TIMEOUT (2 * 1000)            // 2 seconds
#define NATS_OPTS_DEFAULT_RECONNECT_WAIT (2 * 1000)     // 2 seconds
#define NATS_OPTS_DEFAULT_PING_INTERVAL (2 * 60 * 1000) // 2 minutes
#define NATS_OPTS_DEFAULT_MAX_PING_OUT (2)
#define NATS_OPTS_DEFAULT_IO_BUF_SIZE (32 * 1024)              // 32 KB
#define NATS_OPTS_DEFAULT_MAX_PENDING_MSGS (65536)             // 65536 messages
#define NATS_OPTS_DEFAULT_MAX_PENDING_BYTES (64 * 1024 * 1024) // 64 MB
#define NATS_OPTS_DEFAULT_RECONNECT_JITTER (100)               // 100 ms
#define NATS_OPTS_DEFAULT_RECONNECT_JITTER_TLS (1000)          // 1 second

natsStatus nats_cloneOptions(natsOptions **newOptions, natsPool *pool, natsOptions *opts);
natsStatus nats_createOptions(natsOptions **newOptions, natsPool *pool);

#endif /* OPTS_H_ */
