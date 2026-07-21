// Copyright 2026 The NATS Authors
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

// NATS-DOC-START
// Print which subscription pattern (passed as the closure) got the message
static void
onSensorMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("%s%-15.*s (%s)\n",
           (const char*) closure,
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg),
           natsMsg_GetSubject(msg));
    natsMsg_Destroy(msg);
}
// NATS-DOC-END

int main(int argc, char **argv)
{
    natsConnection      *conn     = NULL;
    natsSubscription    *alarms   = NULL;
    natsSubscription    *critical = NULL;
    natsSubscription    *all      = NULL;
    natsStatus          s;
    const char          *url      = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // NATS-DOC-START
    // Subscribe to all alarms
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&alarms, conn, "sensor.alarm.*",
                                     onSensorMsg, (void*) "[sensor.alarm.*]       ");

    // Subscribe to all critical
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&critical, conn, "sensor.*.*.critical",
                                     onSensorMsg, (void*) "[sensor.*.*.critical]  ");

    // Subscribe to everything
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&all, conn, "sensor.>",
                                     onSensorMsg, (void*) "[sensor.>]             ");

    // Publish to specific subjects
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "sensor.alarm.smoke", "kitchen,14:22");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "sensor.alarm.smoke.critical", "kitchen,14:23");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "sensor.alarm.water", "basement,16:42");
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "sensor.alarm.water.critical", "basement,16:43");
    // NATS-DOC-END

    // Give the callbacks a moment to print the messages
    if (s == NATS_OK)
        nats_Sleep(500);

    // Anything that is created need to be destroyed
    natsSubscription_Destroy(alarms);
    natsSubscription_Destroy(critical);
    natsSubscription_Destroy(all);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
