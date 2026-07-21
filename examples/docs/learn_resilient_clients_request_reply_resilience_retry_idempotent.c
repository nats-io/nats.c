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
    const char          *url  = getenv("NATS_URL");

    s = natsConnection_ConnectTo(&conn, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // The safe retry: branch on the failure, bound the attempts, and keep
    // the payload -- including its order_id -- identical on every attempt
    // so the inventory responder can de-dupe a re-sent request.
    const char *order       = "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                              "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}";
    int        maxAttempts  = 5;
    int        attempt;

    for (attempt = 1; attempt <= maxAttempts; attempt++)
    {
        natsMsg *reply = NULL;

        s = natsConnection_RequestString(&reply, conn,
                                         "orders.inventory.check",
                                         order, 2000);
        if (s == NATS_OK)
        {
            printf("inventory answered on attempt %d: %.*s\n", attempt,
                   natsMsg_GetDataLength(reply), natsMsg_GetData(reply));
            natsMsg_Destroy(reply);
            break;
        }
        else if (s == NATS_TIMEOUT)
        {
            // The responder is up but slow: a fast retry is reasonable.
            fprintf(stderr, "attempt %d timed out, retrying shortly\n",
                    attempt);
            nats_Sleep(200);
        }
        else if (s == NATS_NO_RESPONDERS)
        {
            // Nothing is listening yet: back off exponentially, with
            // jitter so a fleet of requesters does not retry in lockstep.
            int64_t wait = (1000 << (attempt - 1)) + (rand() % 250);

            fprintf(stderr, "attempt %d found no responder, backing off "
                    "%dms\n", attempt, (int) wait);
            nats_Sleep(wait);
        }
        else
            break; // an unexpected error; do not keep retrying
    }
    if (s != NATS_OK)
        fprintf(stderr, "inventory check failed after %d attempts: %s\n",
                maxAttempts, natsStatus_GetText(s));
    // NATS-DOC-END

    natsConnection_Destroy(conn);

    return (s == NATS_OK) ? 0 : 1;
}
