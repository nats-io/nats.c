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

#ifndef NATS_MTLS_HELPER_H_
#define NATS_MTLS_HELPER_H_

#include "nats.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief mTLS Configuration Structure
 *
 * This structure holds all the necessary configuration for setting up
 * mutual TLS authentication with a NATS server.
 */
typedef struct natsMTLSConfig
{
    /** CA certificate file path (required for server verification) */
    const char *caCertFile;

    /** CA certificate PEM string (alternative to caCertFile) */
    const char *caCertPEM;

    /** Client certificate file path (required for mTLS) */
    const char *clientCertFile;

    /** Client certificate PEM string (alternative to clientCertFile) */
    const char *clientCertPEM;

    /** Client private key file path (required for mTLS) */
    const char *clientKeyFile;

    /** Client private key PEM string (alternative to clientKeyFile) */
    const char *clientKeyPEM;

    /** Expected hostname in server certificate (optional) */
    const char *expectedHostname;

    /** TLS 1.2 cipher list (optional) */
    const char *ciphers;

    /** TLS 1.3 cipher suites (optional) */
    const char *cipherSuites;

    /** Perform TLS handshake before INFO protocol (default: false) */
    bool tlsHandshakeFirst;

    /** Skip server certificate verification - INSECURE! (default: false) */
    bool skipServerVerification;

    /** Use dynamic certificate reloading (default: false) */
    bool dynamicCertReload;

    /** Allow concurrent TLS handshakes (default: false) */
    bool allowConcurrentHandshakes;

} natsMTLSConfig;

/**
 * \brief Initialize mTLS configuration structure
 *
 * Initializes all fields in the natsMTLSConfig structure to NULL/false.
 *
 * @param config Pointer to the mTLS configuration structure to initialize
 */
static inline void
natsMTLSConfig_Init(natsMTLSConfig *config)
{
    if (config == NULL)
        return;

    memset(config, 0, sizeof(natsMTLSConfig));
}

/**
 * \brief Configure natsOptions with mTLS settings
 *
 * This is a convenience function that applies all mTLS configuration
 * settings from a natsMTLSConfig structure to a natsOptions object.
 *
 * The function will:
 * 1. Enable secure/TLS mode
 * 2. Load CA certificates (from file or PEM string)
 * 3. Load client certificates (from file or PEM string)
 * 4. Apply optional settings (hostname, ciphers, etc.)
 *
 * @param opts Pointer to natsOptions object to configure
 * @param config Pointer to natsMTLSConfig with the mTLS settings
 *
 * @return NATS_OK on success, error code otherwise
 *
 * \code{.c}
 * natsOptions *opts = NULL;
 * natsMTLSConfig mtlsConfig;
 * natsStatus s;
 *
 * // Initialize the config
 * natsMTLSConfig_Init(&mtlsConfig);
 *
 * // Set required parameters
 * mtlsConfig.caCertFile = "certs/ca.pem";
 * mtlsConfig.clientCertFile = "certs/client-cert.pem";
 * mtlsConfig.clientKeyFile = "certs/client-key.pem";
 *
 * // Set optional parameters
 * mtlsConfig.expectedHostname = "nats.example.com";
 * mtlsConfig.dynamicCertReload = true;
 *
 * // Create options and apply mTLS config
 * s = natsOptions_Create(&opts);
 * if (s == NATS_OK)
 *     s = natsOptions_ApplyMTLSConfig(opts, &mtlsConfig);
 *
 * // Connect with mTLS
 * if (s == NATS_OK)
 *     s = natsConnection_Connect(&conn, opts);
 * \endcode
 */
NATS_EXTERN natsStatus
natsOptions_ApplyMTLSConfig(natsOptions *opts, const natsMTLSConfig *config);

/**
 * \brief Simplified mTLS setup from file paths
 *
 * Convenience function for the most common mTLS setup scenario where
 * all certificates are in files.
 *
 * @param opts Pointer to natsOptions object to configure
 * @param caCertFile Path to CA certificate file
 * @param clientCertFile Path to client certificate file
 * @param clientKeyFile Path to client private key file
 *
 * @return NATS_OK on success, error code otherwise
 *
 * \code{.c}
 * natsOptions *opts = NULL;
 * natsStatus s;
 *
 * s = natsOptions_Create(&opts);
 * if (s == NATS_OK)
 * {
 *     s = natsOptions_SetupMTLSFromFiles(opts,
 *                                        "certs/ca.pem",
 *                                        "certs/client-cert.pem",
 *                                        "certs/client-key.pem");
 * }
 * \endcode
 */
NATS_EXTERN natsStatus
natsOptions_SetupMTLSFromFiles(natsOptions *opts,
                                const char *caCertFile,
                                const char *clientCertFile,
                                const char *clientKeyFile);

/**
 * \brief Simplified mTLS setup from PEM strings
 *
 * Convenience function for setting up mTLS when certificates are
 * already loaded in memory as PEM-encoded strings.
 *
 * @param opts Pointer to natsOptions object to configure
 * @param caCertPEM CA certificate as PEM string
 * @param clientCertPEM Client certificate as PEM string
 * @param clientKeyPEM Client private key as PEM string
 *
 * @return NATS_OK on success, error code otherwise
 *
 * \code{.c}
 * const char *caPEM = "-----BEGIN CERTIFICATE-----\n...";
 * const char *certPEM = "-----BEGIN CERTIFICATE-----\n...";
 * const char *keyPEM = "-----BEGIN PRIVATE KEY-----\n...";
 *
 * natsOptions *opts = NULL;
 * natsStatus s;
 *
 * s = natsOptions_Create(&opts);
 * if (s == NATS_OK)
 * {
 *     s = natsOptions_SetupMTLSFromMemory(opts, caPEM, certPEM, keyPEM);
 * }
 * \endcode
 */
NATS_EXTERN natsStatus
natsOptions_SetupMTLSFromMemory(natsOptions *opts,
                                 const char *caCertPEM,
                                 const char *clientCertPEM,
                                 const char *clientKeyPEM);

#ifdef __cplusplus
}
#endif

#endif /* NATS_MTLS_HELPER_H_ */
