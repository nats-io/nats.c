// Copyright 2015-2024 The NATS Authors
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

#ifndef CERT_H_
#define CERT_H_

#include "status.h"

struct __natsCert
{
    const char    *subjectName;
    const char    *issuerName;
    struct tm     tmNotBefore;
    struct tm     tmNotAfter;
};

struct __natsCertChainElement
{
    natsCert* cert;
    struct __natsCertChainElement *next;
};

#if defined(NATS_HAS_TLS)
natsStatus
natsCert_create(natsCert **newCert, X509 *x509Cert);

void
natsCert_free(natsCert *cert);

natsStatus
natsCertChain_create(natsCertChain **newChain, STACK_OF(X509) *x509Chain);

void
natsCertChain_free(natsCertChain *chain);
#endif // NATS_HAS_TLS

#endif // CERT_H_
