// Copyright 2020-2021 The NATS Authors
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

#include <nats.h>

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsSubscription    *sub  = NULL;
    natsMsg             *msg  = NULL;
    natsMsg             *rmsg = NULL;
    natsStatus          s;

    // Creates a connection to the default NATS URL
    s = natsConnection_ConnectTo(&conn, NATS_DEFAULT_URL);

    // Create a message
    if (s == NATS_OK)
        s = natsMsg_Create(&msg, "foo", NULL, "body", 4);

    // Create a header by setting a key/value
    if (s == NATS_OK)
        s = natsMsgHeader_Set(msg, "My-Key1", "value1");

    // Let's set a new key
    if (s == NATS_OK)
        s = natsMsgHeader_Set(msg, "My-Key2", "value2");

    // Here we add a value to the first key
    if (s == NATS_OK)
        s = natsMsgHeader_Add(msg, "My-Key1", "value3");

    // Adding yet another key
    if (s == NATS_OK)
        s = natsMsgHeader_Set(msg, "My-Key3", "value4");

    // Remove a key
    if (s == NATS_OK)
        s = natsMsgHeader_Delete(msg, "My-Key3");

    // Let's print all the keys that are currently set
    if (s == NATS_OK)
    {
        const char* *keys = NULL;
        int         nkeys = 0;
        int         i;

        s = natsMsgHeader_Keys(msg, &keys, &nkeys);
        for (i=0; (s == NATS_OK) && (i<nkeys); i++)
        {
            // To get all values that are associated with a key
            const char* *values = NULL;
            int         nvalues = 0;
            int         j;

            s = natsMsgHeader_Values(msg, keys[i], &values, &nvalues);

            for (j=0; (s == NATS_OK) && (j < nvalues); j++)
                printf("Key: '%s' Value: '%s'\n", keys[i], values[j]);

            // We need to free the returned array of pointers.
            free((void*) values);
        }
        // We need to free the returned array of pointers.
        free((void*) keys);
    }

    // Create a subscription that we will use to receive this message
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, conn, "foo");

    // Now publish the message
    if (s == NATS_OK)
        s = natsConnection_PublishMsg(conn, msg);

    // We should receive it
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&rmsg, sub, 1000);

    // Now let's check some headers from the received message
    if (s == NATS_OK)
    {
        const char *val = NULL;

        // Notice that calling natsMsgHeader_Get() on a key that has
        // several values will return the first entry.
        s = natsMsgHeader_Get(rmsg, "My-Key1", &val);
        if (s == NATS_OK)
            printf("For key 'My-Key1', got value: '%s'\n", val);

        // If we lookup for a key that does not exist, we get NATS_NOT_FOUND.
        if (s == NATS_OK)
        {
            s = natsMsgHeader_Get(rmsg, "key-does-not-exist", &val);
            if (s == NATS_NOT_FOUND)
                s = NATS_OK;
            else
                printf("Should not have found that key!\n");
        }
        // Same for delete
        if (s == NATS_OK)
        {
            s = natsMsgHeader_Delete(rmsg, "key-does-not-exist");
            if (s == NATS_NOT_FOUND)
                s = NATS_OK;
            else
                printf("Should not have found that key!\n");
        }
    }

    // Anything that is created need to be destroyed
    natsMsg_Destroy(msg);
    natsMsg_Destroy(rmsg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
