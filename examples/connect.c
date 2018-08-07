// Copyright 2018 The NATS Authors
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

#include "examples.h"

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    if (print)
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

    // We should be using a mutex to protect those variables since
    // they are used from the subscription's delivery and the main
    // threads. For demo purposes, this is fine.
    count++;

    natsMsg_Destroy(msg);
}

static void
connectedCB(natsConnection *nc, void *closure)
{
    char url[256];

    natsConnection_GetConnectedUrl(nc, url, sizeof(url));
    printf("Connected to %s\n", url);
}

static void
closedCB(natsConnection *nc, void *closure)
{
    bool        *closed = (bool*)closure;
    const char  *err    = NULL;

    natsConnection_GetLastError(nc, &err);
    printf("Connect failed: %s\n", err);
    *closed = true;
}

int main(int argc, char **argv)
{
    natsConnection      *conn  = NULL;
    natsOptions         *opts  = NULL;
    natsSubscription    *sub   = NULL;
    bool                closed = false;
    natsStatus          s;

    opts = parseArgs(argc, argv, "");

    // Set a max (re)connect attempts of 50 with a delay of 100 msec.
    // Total time will then be around 5 seconds.

    s = natsOptions_SetMaxReconnect(opts, 50);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);

    // Instruct the library to block the connect call for that
    // long until it can get a connection or fails.
    if (s == NATS_OK)
        s = natsOptions_SetRetryOnFailedConnect(opts, true, NULL, NULL);

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        exit(1);
    }

    printf("Ensure no server is running, attempt to connect...\n");

    // If the server is not running, this will block for about 5 seconds.
    start = nats_Now();
    s = natsConnection_Connect(&conn, opts);
    elapsed = nats_Now() - start;

    printf("natsConnection_Connect call took %" PRId64 " ms and returned: %s\n", elapsed, natsStatus_GetText(s));

    // Close/destroy the connection in case you had a server running...
    natsConnection_Destroy(conn);
    conn = NULL;

    // Now reduce the count, set a connected callback to allow
    // connect to be done asynchronously and a closed callback
    // to show that if connect fails, the callback is invoked.
    s = natsOptions_SetMaxReconnect(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetRetryOnFailedConnect(opts, true, connectedCB, NULL);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, closedCB, (void*)&closed);

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        exit(1);
    }

    printf("\n\n");
    printf("Ensure no server is running, attempt to connect with async connect...\n");

    // Start the connect. If no server is running, it should return
    // NATS_NOT_YET_CONNECTED.
    s = natsConnection_Connect(&conn, opts);
    printf("natsConnection_Connect call returned: %s\n", natsStatus_GetText(s));

    // Wait for the closed callback to be invoked
    while (!closed)
        nats_Sleep(100);

    // Destroy the connection object
    natsConnection_Destroy(conn);
    conn = NULL;


    // Now change the options to increase the attempts again.
    s = natsOptions_SetMaxReconnect(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 1000);
    // Remove the closed CB for this test
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, NULL, NULL);

    if (s != NATS_OK)
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
        exit(1);
    }

    printf("\n\n");
    printf("Ensure no server is running, attempt to connect with async connect...\n");

    s = natsConnection_Connect(&conn, opts);
    printf("Connect returned: %s\n", natsStatus_GetText(s));

    // Create a subscription and send a message...
    s = natsConnection_Subscribe(&sub, conn, subj, onMsg, NULL);
    if (s == NATS_OK)
        s = natsConnection_Publish(conn, "foo", (const void*)"hello", 5);

    printf("\nStart a server now...\n\n");

    // Wait for the connect to succeed and message to be received.
    while ((s == NATS_OK) && (count != 1))
        nats_Sleep(100);

    printf("Received %d message\n", (int) count);

    // Destroy all our objects to avoid report of memory leak
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    return 0;
}
