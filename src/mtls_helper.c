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

#include "mtls_helper.h"
#include "natsp.h"

natsStatus
natsOptions_ApplyMTLSConfig(natsOptions *opts, const natsMTLSConfig *config)
{
    natsStatus s = NATS_OK;

    if (opts == NULL || config == NULL)
    {
        return nats_setError(NATS_INVALID_ARG,
                             "%s", "natsOptions and natsMTLSConfig cannot be NULL");
    }

    // Enable secure/TLS mode
    s = natsOptions_SetSecure(opts, true);

    // Load CA certificates
    if (s == NATS_OK)
    {
        if (config->caCertPEM != NULL)
        {
            // Load from PEM string
            s = natsOptions_SetCATrustedCertificates(opts, config->caCertPEM);
        }
        else if (config->caCertFile != NULL)
        {
            // Load from file
            s = natsOptions_LoadCATrustedCertificates(opts, config->caCertFile);
        }
        else
        {
            s = nats_setError(NATS_INVALID_ARG,
                              "%s", "Either caCertFile or caCertPEM must be set");
        }
    }

    // Load client certificates and key
    if (s == NATS_OK)
    {
        bool hasCertPEM = (config->clientCertPEM != NULL);
        bool hasKeyPEM = (config->clientKeyPEM != NULL);
        bool hasCertFile = (config->clientCertFile != NULL);
        bool hasKeyFile = (config->clientKeyFile != NULL);

        if (hasCertPEM && hasKeyPEM)
        {
            // Load from PEM strings
            s = natsOptions_SetCertificatesChain(opts,
                                                 config->clientCertPEM,
                                                 config->clientKeyPEM);
        }
        else if (hasCertFile && hasKeyFile)
        {
            // Load from files
            if (config->dynamicCertReload)
            {
                // Use dynamic reloading
                s = natsOptions_LoadCertificatesChainDynamic(opts,
                                                             config->clientCertFile,
                                                             config->clientKeyFile);
            }
            else
            {
                // Load once
                s = natsOptions_LoadCertificatesChain(opts,
                                                      config->clientCertFile,
                                                      config->clientKeyFile);
            }
        }
        else
        {
            s = nats_setError(NATS_INVALID_ARG,
                              "%s", "Either (clientCertFile and clientKeyFile) or "
                              "(clientCertPEM and clientKeyPEM) must be set");
        }
    }

    // Apply optional settings
    if ((s == NATS_OK) && (config->expectedHostname != NULL))
    {
        s = natsOptions_SetExpectedHostname(opts, config->expectedHostname);
    }

    if ((s == NATS_OK) && (config->ciphers != NULL))
    {
        s = natsOptions_SetCiphers(opts, config->ciphers);
    }

    if ((s == NATS_OK) && (config->cipherSuites != NULL))
    {
        s = natsOptions_SetCipherSuites(opts, config->cipherSuites);
    }

    if ((s == NATS_OK) && config->tlsHandshakeFirst)
    {
        s = natsOptions_TLSHandshakeFirst(opts);
    }

    if ((s == NATS_OK) && config->allowConcurrentHandshakes)
    {
        s = natsOptions_AllowConcurrentTLSHandshakes(opts);
    }

    if ((s == NATS_OK) && config->skipServerVerification)
    {
        s = natsOptions_SkipServerVerification(opts, true);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_SetupMTLSFromFiles(natsOptions *opts,
                                const char *caCertFile,
                                const char *clientCertFile,
                                const char *clientKeyFile)
{
    natsStatus s = NATS_OK;

    if (opts == NULL || caCertFile == NULL ||
        clientCertFile == NULL || clientKeyFile == NULL)
    {
        return nats_setError(NATS_INVALID_ARG,
                             "%s", "All parameters must be non-NULL");
    }

    // Enable secure/TLS mode
    s = natsOptions_SetSecure(opts, true);

    // Load CA certificate
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, caCertFile);

    // Load client certificate and key
    if (s == NATS_OK)
        s = natsOptions_LoadCertificatesChain(opts, clientCertFile, clientKeyFile);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_SetupMTLSFromMemory(natsOptions *opts,
                                 const char *caCertPEM,
                                 const char *clientCertPEM,
                                 const char *clientKeyPEM)
{
    natsStatus s = NATS_OK;

    if (opts == NULL || caCertPEM == NULL ||
        clientCertPEM == NULL || clientKeyPEM == NULL)
    {
        return nats_setError(NATS_INVALID_ARG,
                             "%s", "All parameters must be non-NULL");
    }

    // Enable secure/TLS mode
    s = natsOptions_SetSecure(opts, true);

    // Load CA certificate from memory
    if (s == NATS_OK)
        s = natsOptions_SetCATrustedCertificates(opts, caCertPEM);

    // Load client certificate and key from memory
    if (s == NATS_OK)
        s = natsOptions_SetCertificatesChain(opts, clientCertPEM, clientKeyPEM);

    return NATS_UPDATE_ERR_STACK(s);
}
