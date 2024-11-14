// Copyright 2015-2021 The NATS Authors
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

#include "natsp.h"

#include "mem.h"
#include "cert.h"

#if defined(NATS_HAS_TLS)
natsStatus
natsCert_create(natsCert **newCert, X509 *x509Cert)
{
    natsCert *cert = NULL;
    const ASN1_TIME* notBefore;
    const ASN1_TIME* notAfter;

    if (x509Cert == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    cert = NATS_CALLOC(1, sizeof(natsCert));
    if (cert == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    cert->subjectName = X509_NAME_oneline(X509_get_subject_name(x509Cert), NULL, 0);
    cert->issuerName = X509_NAME_oneline(X509_get_issuer_name(x509Cert), NULL, 0);

    notBefore = X509_get0_notBefore(x509Cert);
    ASN1_TIME_to_tm(notBefore, &cert->tmNotBefore);
    notAfter = X509_get0_notAfter(x509Cert);
    ASN1_TIME_to_tm(notAfter, &cert->tmNotAfter);

    *newCert = cert;

    return NATS_OK;
}

void
natsCert_free(natsCert* cert)
{
    if (cert == NULL)
        return;

    OPENSSL_free((char *)cert->subjectName);
    OPENSSL_free((char*)cert->issuerName);

    NATS_FREE(cert);
}

static natsStatus
_createChainElement(natsCertChain **newElement, X509 *x509Cert)
{
    natsCertChain   *element    = NULL;
    natsStatus      s           = NATS_OK;
    natsCert        *cert       = NULL;

    s = natsCert_create(&cert, x509Cert);
    if (s != NATS_OK)
        return s;

    element = NATS_CALLOC(1, sizeof(natsCertChain));
    if (element == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    element->cert = cert;
    element->next = NULL;

    *newElement = element;

    return NATS_OK;
}

static natsStatus
_appendChainElement(natsCertChain **chain, X509 *x509Cert)
{
    natsStatus      s           = NATS_OK;
    natsCertChain   *newElement = NULL;
    natsCertChain   *temp       = NULL;

    s = _createChainElement(&newElement, x509Cert);
    if (s != NATS_OK)
        return s;

    if (*chain == NULL)
    {
        *chain = newElement;
        return NATS_OK;
    }
    
    temp = *chain;
    while (temp->next != NULL)
    {
        temp = temp->next;
    }
    temp->next = newElement;
    return NATS_OK;
}


natsStatus
natsCertChain_create(natsCertChain **newChain, STACK_OF(X509) *x509Chain)
{
    natsCertChain   *chain          = NULL;
    int             numElements;
    X509            *x509Cert;
    natsStatus      s               = NATS_OK;

    if (x509Chain == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    numElements = sk_X509_num(x509Chain);
    for (int i = 0; i < numElements; i++)
    {
        x509Cert = sk_X509_value(x509Chain, i);
        s = _appendChainElement(&chain, x509Cert);
        if (s != NATS_OK)
        {
            natsCertChain_free(chain);
            return s;
        }
    }

    *newChain = chain;
    return NATS_OK;
}

void
natsCertChain_free(natsCertChain *chain)
{
    natsCertChain *temp;

    if (chain == NULL)
        return;

    while (chain != NULL)
    {
        temp = chain;
        chain = chain->next;
        natsCert_free(temp->cert);
        NATS_FREE(temp);
    }
}
#endif // NATS_HAS_TLS
