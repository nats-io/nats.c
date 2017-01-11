# NATS - C Client 
A C client for the [NATS messaging system](https://nats.io).

This NATS Client implementation is heavily based on the [NATS GO Client](https://github.com/nats-io/nats). There is support for Mac OS/X, Linux and Windows (although we don't have specific platform support matrix).

[![License MIT](https://img.shields.io/npm/l/express.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://travis-ci.org/nats-io/cnats.svg?branch=master)](http://travis-ci.org/nats-io/cnats)
[![Release](https://img.shields.io/badge/release-v1.4.4-blue.svg?style=flat)](https://github.com/nats-io/cnats/releases/tag/v1.4.4)
[![Documentation](https://img.shields.io/badge/doc-Doxygen-brightgreen.svg?style=flat)](http://nats-io.github.io/cnats)

## Build

First, download the source code:
```
git clone git@github.com:nats-io/cnats.git .
```

To build the library, use [CMake](https://cmake.org/download/).

Make sure that CMake is added to your path. If building on Windows, open a command shell from the Visual Studio Tools menu, and select the appropriate command shell (x64 or x86 for 64 or 32 bit builds respectively). You will also probably need to run this with administrator privileges.

Create a `build` directory (any name would work) from the root source tree, and `cd` into it. Then issue this command for the first time:

```
cmake ..
```

You can also specify command line parameters to set some of the cmake options directly. For instance, if you want to build the library without TLS support, do this:

```
cmake .. -DNATS_BUILD_WITH_TLS=OFF
```

If you had previously build the library, you may need to do a `make clean`, or simply delete and re-create the build directory before executing the cmake command.

To build on Windows, you would need to select the build generator. For instance, to select `nmake`, you would run:

```
cmake .. -G "NMake Makefiles"
```

Running `cmake -h` would give you the list of possible options and all the generator names.

Alternatively, you can run the GUI version. From that same *build* command shell, start the GUI:

```
c:\program files (x86)\CMake\bin\cmake-gui.exe
```

If you started with an empty build directory, you would need to select the source and build directory, then click `Configure`. Here, you will be able to select from the drop-down box the name of the build generator. When done, click `Generate`. Then you can go back to your command shell, or Visual Studio and build.

To modify some of the build options, you need to edit the cache and rebuild.

```
make edit_cache
```

Note that if you build on Windows and have selected "NMake Makefiles", replace all following references to `make` with `nmake`.

Editing the cache allows you to select the build type (Debug, Release, etc), the architecture (64 or 32bit), and so on.

The default target will build everything, that is, the static and shared NATS libraries and also the examples and the test program. Each are located in their respective directories under your build directory: `src`, `examples` and `test`.

```
make install
```

Will copy both the static and shared libraries in the folder `install/lib` and the public headers in `install/include`.

On platforms where `valgrind` is available, you can run the tests with memory checks.
Here is an example:

```
make test ARGS="-T memcheck"
```

Or, you can invoke directly the `ctest` program:

```
ctest -T memcheck -V -I 1,4
```
The above command would run the tests with `valgrind` (`-T memcheck`), with verbose output (`-V`), and run the tests from 1 to 4 (`-I 1,4`).

If you add a test to `test/test.c`, you need to add it into the `allTests` array. Each entry contains a name, and the test function. You can add it anywhere into this array.
Build you changes:

```
$ make
[ 44%] Built target nats
[ 88%] Built target nats_static
[ 90%] Built target nats-publisher
[ 92%] Built target nats-queuegroup
[ 94%] Built target nats-replier
[ 96%] Built target nats-requestor
[ 98%] Built target nats-subscriber
Scanning dependencies of target testsuite
[100%] Building C object test/CMakeFiles/testsuite.dir/test.c.o
Linking C executable testsuite
[100%] Built target testsuite
```

Now regenerate the list by invoking the test suite without any argument:

```
$ ./test/testsuite
Number of tests: 77
```

This list the number of tests added to the file `list.txt`. Move this file to the source's test directory.

```
$ mv list.txt ../test/
```

Then, refresh the build:

```
$ cmake ..
-- Configuring done
-- Generating done
-- Build files have been written to: /home/ivan/cnats/build
```

You can use the following environment variables to influence the testsuite behavior.

When running with memory check, timing changes and overall performance is slower. The following variable allows the testsuite to adjust some of values used during the test:

```
export NATS_TEST_VALGRIND=yes
```

On Windows, it would be `set` instead of `export`.

When running the tests in verbose mode, the following environment variable allows you to see the server output from within the test itself. Without this option, the server output is silenced:

```
export NATS_TEST_KEEP_SERVER_OUTPUT=yes
```

If you want to change the default server executable name (`gnastd`) or specify a specific location, use this environment variable:

```
set NATS_TEST_SERVER_EXE=c:\test\gnatsd.exe
```

## Documentation

The public API has been documented using [Doxygen](http://www.stack.nl/~dimitri/doxygen/).

To generate the documentation, go to the `doc` directory and type the following command:

```
doxygen DoxyFile.NATS.Client
```

The generated documenation will be located in the `html` directory. To see the documentation, point your browser to the file `index.html` in that directory.

Go [here](http://nats-io.github.io/cnats) for the online documentation.

The source code is also quite documented.

## Basic Usage

Note that for simplicity, error checking is not performed here.
```c
natsConnection 		*nc  = NULL;
natsSubscription 	*sub = NULL;
natsMsg				*msg = NULL;

// Connects to the default NATS Server running locally
natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);

// Connects to a server with username and password
natsConnection_ConnectTo(&nc, "nats://ivan:secret@localhost:4222");

// Connects to a server with token authentication
natsConnection_ConnectTo(&nc, "nats://myTopSecretAuthenticationToken@localhost:4222");

// Simple publisher, sending the given string to subject "foo"
natsConnection_PublishString(nc, "foo", "hello world");

// Publish binary data. Content is not interpreted as a string.
char data[] = {1, 2, 0, 4, 5};
natsConnection_Publish(nc, "foo", (const void*) data, 5);

// Simple asynchronous subscriber on subject foo, invoking message 
// handler 'onMsg' when messages are received, and not providing a closure.
natsConnection_Subscribe(&sub, nc, "foo", onMsg, NULL);

// Simple synchronous subscriber
natsConnection_SubscribeSync(&nc, nc, "foo");

// Using a synchronous subscriber, gets the first message available, waiting
// up to 1000 milliseconds (1 second)
natsSubscription_NextMsg(&msg, sub, 1000);

// Destroy any message received (asynchronously or synchronously) or created
// by your application. Note that if 'msg' is NULL, the call has no effect.
natsMsg_Destroy(msg);

// Unsubscribing
natsSubscription_Unsubscribe(sub);

// Destroying the subscription (this will release the object, which may
// result in freeing the memory). After this call, the object must no
// longer be used.
natsSubscription_Destroy(sub);

// Publish requests to the given reply subject:
natsConnection_PublishRequestString(nc, "foo", "bar", "help!");

// Sends a request (internally creates an inbox) and Auto-Unsubscribe the
// internal subscriber, which means that the subscriber is unsubscribed
// when receiving the first response from potentially many repliers.
// This call will wait for the reply for up to 1000 milliseconds (1 second).
natsConnection_RequestString(&reply, nc, "foo", "help", 1000);

// Closing a connection (but not releasing the connection object)
natsConnection_Close(nc);

// When done with the object, free the memory. Note that this call
// closes the connection first, in other words, you could have simply
// this call instead of natsConnection_Close() followed by the destroy
// call.
natsConnection_Destroy(nc);

// Message handler
void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
	// Prints the message, using the message getters:    
	printf("Received msg: %s - %.*s\n",
            natsMsg_GetSubject(msg),
            natsMsg_GetDataLength(msg),
            natsMsg_GetData(msg));

	// Don't forget to destroy the message!
    natsMsg_Destroy(msg);
}
```

## Wildcard Subscriptions

The `*` wildcard matches any token, at any level of the subject:

```c
natsConnection_Subscribe(&sub, nc, "foo.*.baz", onMsg, NULL);
```
This subscriber would receive messages sent to:

* foo.bar.baz
* foo.a.baz
* etc...

It would not, however, receive messages on:

* foo.baz
* foo.baz.bar
* etc...

The `>` wildcard matches any length of the fail of a subject, and can only be the last token.

```c
natsConnection_Subscribe(&sub, nc, "foo.>", onMsg, NULL);
```
This subscriber would receive any message sent to:

* foo.bar
* foo.bar.baz
* foo.foo.bar.bax.22
* etc...

However, it would not receive messages sent on:

* foo
* bar.foo.baz
* etc...

Publishing on this subject would cause the two above subscriber to receive the message:
```c
natsConnection_PublishString(nc, "foo.bar.baz", "got it?");
```

## Queue Groups

All subscriptions with the same queue name will form a queue group. Each message will be delivered to only one subscriber per queue group, using queue sematics. You can have as many queue groups as you wish. Normal subscribers will continue to work as expected.

```c
natsConnection_QueueSubscribe(&sub, nc, "foo", "job_workers", onMsg, NULL);
```

## TLS

(Note that the library needs to be built with TLS support - which is by default - for these APIs to work. See the Build chapter on how to build with or without TLS for more details).

An SSL/TLS connection is configured through the use of `natsOptions`. Depending on the level of security you desire, it can be as simple as setting the secure boolean to true on the `natsOptions_SetSecure()` call.

Even with full security (client verifying server certificate, and server requiring client certificates), the setup involves only a few calls.

```c
// Here is the minimum to create a TLS/SSL connection:

// Create an options object.
natsOptions_Create(&opts);

// Set the secure flag.
natsOptions_SetSecure(opts, true);

// You may not need this, but suppose that the server certificate
// is self-signed and you would normally provide the root CA, but
// don't want to. You can disable the server certificate verification
// like this:
natsOptions_SkipServerVerification(opts, true);

// Connect now...
natsConnection_Connect(&nc, opts);

// That's it! On success you will have a secure connection with the server!

(...)

// This example shows what it takes to have a full SSL configuration,
// including server expected's hostname, root CA, client certificates
// and specific ciphers to use.

// Create an options object.
natsOptions_Create(&opts);

// Set the secure flag.
natsOptions_SetSecure(opts, true);

// For a server with a trusted chain built into the client host,
// simply designate the server name that is expected. Without this
// call, the server certificate is still verified, but not the
// hostname.
natsOptions_SetExpectedHostname(opts, "localhost");

// Instead, if you are using a self-signed cert and need to load in the CA.
natsOptions_LoadCATrustedCertificates(opts, caCertFileName);

// If the server requires client certificates, provide them along with the
// private key, all in one call.
natsOptions_LoadCertificatesChain(opts, certChainFileName, privateKeyFileName);

// You can also specify preferred ciphers if you want.
natsOptions_SetCiphers(opts, "-ALL:HIGH");

// Then simply pass the options object to the connect call:
natsConnection_Connect(&nc, opts);

// That's it! On success you will have a secure connection with the server!
```

## Advanced Usage

Flushing a connection ensures that any data buffered is flushed (sent to) the NATS Server.

```c
// Flush connection to server, returns when all messages have been processed.
natsConnection_Flush(nc);
printf("All clear!\n");

// Same as above but with a timeout value, expressed in milliseconds.
s = natsConnection_FlushTimeout(nc, 1000);
if (s == NATS_OK)
	printf("All clear!\n");
else if (s == NATS_TIMEOUT)
    printf("Flushed timed out!\n");
else
	printf("Error during flush: %d - %s\n", s, natsStatus_GetText(s));
```    

Auto-unsubscribe allows a subscription to be automatically removed when the subscriber has received a given number of messages. This is used internally by the `natsConnection_Request()` call.

```c   
// Auto-unsubscribe after 100 messages received
natsConnection_Subscribe(&sub, nc, "foo", onMsg, NULL);
natsSubscription_AutoUnsubscribe(sub, 100);
```

You can have multiple connections in your application, mixing subscribers and publishers.
```c
// Create a connection 'nc1' to host1
natsConnection_ConnectTo(&nc1, "nats://host1:4222");

// Create a connection 'nc2' to host2
natsConnection_ConnectTo(&nc2, "nats://host2:4222");

// Create a subscription on 'foo' from connection 'nc1'
natsConnection_Subscribe(&sub, nc1, "foo", onMsg, NULL);

// Uses connection 'nc2' to publish a message on subject 'foo'. The subscriber
// created previously will receive it through connection 'nc1'.
natsConnection_PublishString(nc2, "foo", "hello");
```

The use of `natsOptions` allows you to specify options used by the `natsConnection_Connect()` call. Note that the `natsOptions` object that is  passed to this call is cloned, whhich means that any modification done to the options object will not have any effect on the connected connection.

```c
natsOptions *opts = NULL;

// Create an options object
natsOptions_Create(&opts);

// Set some properties, starting with the URL to connect to:
natsOptions_SetURL(opts, "nats://host1:4222");

// Set a callback for asynchronous errors. This is useful when having an asynchronous
// subscriber, which would otherwise have no other way of reporting an error.
natsOptions_SetErrorHandler(opts, asyncCb, NULL);

// Connect using those options:
natsConnection_Connect(&nc, opts);

// Destroy the options object to free memory. The object was cloned by the connection,
// so the options can be safely destroyed.
natsOptions_Destroy(nc);
```

As we have seen, all callbacks have a `void *closure` parameter. This is useful when the callback needs to perform some work and need a reference to some object. When setting up the callback, you can specify a pointer to that object.
```c
// Our object definition
typdedef struct __Errors
{
	int count;
    
} Errors;

(...)

int
main(int argc, char **argv)
{
	// Declare an 'Errors' object on the stack.
	Errors asyncErrors;

	// Initialize this object
	memset(&asyncErrors, 0, sizeof(asyncErrors);
    
    // Create a natsOptions object.
    (...)
    
    // Set the error callback, and pass the address of our Errors object.
    natsOptions_SetErrorHandler(opts, asyncCb, (void*) &errors);
    
    // Create the connection and subscriber.
    (...)
    
    // Say that we are done subscribing, we could check the number of errors:
    if (asyncErrors.count > 1000)
    {
    	printf("That's a lot of errors!\n");
	}
    
	(...)
}
``` 

The callback would use the closure this way:
```c
static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
	Errors *errors = (Errors*) closure;
    
    printf("Async error: %d - %s\n", err, natsStatus_GetText(err));
    
    errors->count++;
}
```
This is the same for all other callbacks used in the C NATS library.

## Clustered Usage

```c
static char *servers[] = { "nats://localhost:1222",
						   "nats://localhost:1223",
                           "nats://localhost:1224"};
                           
// Setup options to include all servers in the cluster.
// We first created an options object, and pass the list of servers, specifying
// the number of servers on that list.
natsOptions_SetServers(opts, servers, 3);

// We could also set the amount to sleep between each reconnect attempt (expressed in
// milliseconds), and the number of reconnect attempts.
natsOptions_SetMaxReconnects(opts, 5);
natsOptions_SetReconnectWait(opts, 2000);

// We could also disable the randomization of the server pool
natsOptions_SetNoRandomize(opts, true);

// Setup a callback to be notified on disconnects...
natsOptions_SetDisconnectedCB(opts, disconnectedCb, NULL);

// And on reconncet
natsOptions_SetReconnectedCB(opts, reconnectedCb, NULL);

// This callback could be used to see who we are connected to on reconnect
static void
reconnectedCb(natsConnection *nc, void *closure)
{
    // Define a buffer to receive the url
	char buffer[64];
    
    buffer[0] = '\0';
    
    natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));
    printf("Got reconnected to: %s\n", buffer);
}
```

## Using an Event Loop Library

For each connection, the `NATS` library creates a thread reading data from the socket. Publishing data results in the data being appended to a buffer, which is 'flushed' from a timer callback or in place when the buffer reaches a certain size. Flushing means that we write to the socket (and the socket is in blocking-mode).

If you have multiple connections running in your process, the number of threads will increase (because each connection uses a thread for receiving data from the socket). If this becomes an issue, or if you are already using an event notification library, you can instruct the `NATS` library to use that event library instead of using a thread to do the reads, and directly writting to the socket when data is published.

This works by setting the event loop and various callbacks through the `natsOptions_SetEventLoop()` API. Depending of the event loop you are using, you then have extra API calls to make. The API is in the `adapters` directory and is documented.

We provide adapters for two event notification libraries: [libevent](https://github.com/libevent/libevent), and [libuv](https://github.com/libuv/libuv).

```c
// Create an event loop object
uv_loop_t *uvLoop = uv_default_loop();

// Set it into an options object
natsOptions_SetEventLoop(opts,
                         (void*) uvLoop,
                         natsLibuv_Attach,
                         natsLibuv_Read,
                         natsLibuv_Write,
                         natsLibuv_Detach);

// Connect (as usual)
natsConnection_Connect(&conn, opts);

// Subscribe (as usual)
natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);

// Run the event loop
uv_run(uvLoop, UV_RUN_DEFAULT);
```

The callback `onMsg` that you have registered will be triggered as usual when data becomes available.

Where it becomes tricky is when publishing data. Indeed, publishing is merely putting data in a buffer, and it is the event library that will notify a callback that write to the socket should be performed. For that, the event loop needs to be 'running'.

So if you publish from the thread where the event loop is running, you need to 'run' the loop after each (or a number) of publish calls in order for data to actually be sent out. Alternatively, you can publish from a different thread than the thread running the event loop.

The above is important to keep in mind regarding calls that are doing request-reply. They should not be made from the thread running the event loop. Here is an example of such calls:

```
natsConnection_Request()
natsConnection_Flush()
natsConnection_FlushTimeout()
...
```

Indeed, since these calls publish data and wait for a 'response', if you execute then in the event loop thread (or while the loop is not 'running'), then data will not be sent out. Calls will fail to get a response and timeout.

For `natsConnection_Request()`, use the `natsConnection_PublishRequest()` instead, and have a subscriber for the response registered.

For others, asynchronous version of these calls should be made available.

See examples in the `examples` directory for complete usage.


## License

(The MIT License)

Copyright (c) 2015 Apcera Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

