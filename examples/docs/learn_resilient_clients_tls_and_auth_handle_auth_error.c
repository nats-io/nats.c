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
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, (url == NULL) ? NATS_DEFAULT_URL : url);
    if (s == NATS_OK)
        s = natsOptions_SetName(opts, "order-svc");
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "cluster-ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetUserCredentialsFromFiles(opts, "order-svc.creds",
                                                    NULL);
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // NATS-DOC-START
    // A secure connect fails in two distinct ways. Branch on each at the
    // connect boundary instead of collapsing both into one network error.
    s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
    {
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
        printf("connected securely and published\n");
    }
    else if (s == NATS_SSL_ERROR)
    {
        // CA validation failed: the server certificate does not chain to
        // cluster-ca.pem. The handshake was rejected before any
        // credential was sent.
        fprintf(stderr, "TLS failed: check the CA trust (%s)\n",
                nats_GetLastError(NULL));
    }
    else if (s == NATS_CONNECTION_AUTH_FAILED)
    {
        // The link was secure but the server rejected the credentials:
        // wrong, expired, or revoked creds.
        fprintf(stderr, "authorization failed: check the credentials (%s)\n",
                nats_GetLastError(NULL));
    }
    else
        fprintf(stderr, "connect failed: %s\n", natsStatus_GetText(s));
    // NATS-DOC-END

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    return (s == NATS_OK) ? 0 : 1;
}
