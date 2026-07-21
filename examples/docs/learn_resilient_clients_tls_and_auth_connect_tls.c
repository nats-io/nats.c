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

    // NATS-DOC-START
    // Trust the cluster CA and require TLS. The client validates the
    // server's certificate against cluster-ca.pem during the handshake,
    // so the link is encrypted and the server proven before any
    // credential is sent. The creds then authenticate order-svc over
    // that secure link.
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "cluster-ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetUserCredentialsFromFiles(opts, "order-svc.creds",
                                                    NULL);

    if (s == NATS_OK)
        s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsConnection_PublishString(conn, "orders.created",
                "{\"order_id\":\"ord_8w2k\",\"customer\":\"acme-co\","
                "\"total_cents\":4200,\"ts\":\"2026-05-22T10:14:22Z\"}");
    // NATS-DOC-END

    if (s == NATS_OK)
        s = natsConnection_Flush(conn);

    natsConnection_Destroy(conn);
    natsOptions_Destroy(opts);

    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}
