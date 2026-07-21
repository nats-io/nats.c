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

int main(void)
{
    natsConnection      *conn = NULL;
    natsStatus          s;

    // NATS-DOC-START
    // Connect to the NATS server
    const char *url = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);

    // Publish a message
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "hello", "Hello NATS!");

    if (s == NATS_OK)
        printf("Message published to hello\n");
    // NATS-DOC-END

    // Make sure the message is sent before we close the connection.
    if (s == NATS_OK)
        s = natsConnection_Flush(conn);

    natsConnection_Destroy(conn);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
