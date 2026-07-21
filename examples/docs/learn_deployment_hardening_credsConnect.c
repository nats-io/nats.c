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

int main(int argc, char **argv)
{
    natsConnection      *conn = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    const char          *url  = getenv("NATS_URL");

    s = natsOptions_Create(&opts);

    // NATS-DOC-START
    // Prove that auth and TLS are both live on the hardened ORDERS
    // cluster. The client trusts the CA that signed the server
    // certificate, so the link encrypts; and it presents the
    // ACME-issued order-svc credentials, so the server authenticates
    // the user before accepting the publish.
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts,
                (url == NULL) ? "tls://nats.acme.internal:4222" : url);
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts,
                "/etc/nats/certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetUserCredentialsFromFiles(opts,
                "/etc/nats/creds/order-svc.creds", NULL);

    // Connect as order-svc in the ORDERS account and publish one
    // canonical order. Success proves three things at once: the TLS
    // handshake completed, the server identity verified against the
    // CA, and the credentials authenticated the user.
    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);
    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 2000);
    if (s == NATS_OK)
        printf("published one order over the hardened path\n");
    // NATS-DOC-END

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
