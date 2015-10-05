# NATS - C Client 
A C client for the [NATS messaging system](https://nats.io).

This is an Alpha work, heavily based on the [NATS GO Client](https://github.com/nats-io/nats).

[![License MIT](https://img.shields.io/npm/l/express.svg)](http://opensource.org/licenses/MIT)

## Installation

First, download the source code:
```
git clone git@github.com:kozlovic/cnats.git .
```

Then, build the library:

```
make install examples
```

This will build both the static and dynamic libraries and the examples binaries. After this step, the directory `install` contains two folders: `include` and `lib`, where the header files necessary for your application to use the C NATS library are located, and where the compiled libraries are located.

You have several options to pass to the make command. Here is a list:

* clean
* debug
* install
* examples
* test

We recommand that your run the test suite to ensure that everything works fine in your environment

```
make test
```

## Basic Usage

Note that for simplicity, error checking is not performed here.
```c
natsConnection 		*nc  = NULL;
natsSubscription 	*sub = NULL;
natsMsg				*msg = NULL;

// Connects to the default NATS Server running locally
natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);

// Simple publisher, sending the given string to subject "foo"
natsConnection_PublishString(nc, "foo", "hello world");

// Simple asynchronous subscriber on subject foo, invoking message 
// handler 'onMsg' when messages are received, and not providing a closure.
natsConnection_Subscribe(&sub, nc, "foo", onMsg, NULL);

// Simple synchronous subscriber
natsConnection_SubscribeSync(&nc, nc, "foo");

// Using a synchronous subscriber, gets the first message available, waiting
// up to 1000 milliseconds (1 second)
s = natsSubscription_NextMsg(&msg, sub, 1000);

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
ncConnection_QueueSubscribe(&sub, nc, "foo", "job_workers", onMsg, NULL);
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

