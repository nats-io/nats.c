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

#include <string.h>

#include "mem.h"
#include "opts.h"
#include "util.h"
#include "conn.h"

natsStatus
natsOptions_SetURL(natsOptions *opts, const char* url)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if (opts->url != NULL)
    {
        NATS_FREE(opts->url);
        opts->url = NULL;
    }

    if (url != NULL)
        s = nats_Trim(&(opts->url), url);

    UNLOCK_OPTS(opts);

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_freeServers(natsOptions *opts)
{
    int i;

    if ((opts->servers == NULL) || (opts->serversCount == 0))
        return;

    for (i = 0; i < opts->serversCount; i++)
        NATS_FREE(opts->servers[i]);

    NATS_FREE(opts->servers);

    opts->servers       = NULL;
    opts->serversCount  = 0;
}

natsStatus
natsOptions_SetServers(natsOptions *opts, const char** servers, int serversCount)
{
    natsStatus  s = NATS_OK;
    int         i;

    LOCK_AND_CHECK_OPTIONS(opts,
                           (((servers != NULL) && (serversCount <= 0))
                            || ((servers == NULL) && (serversCount != 0))));

    _freeServers(opts);

    if (servers != NULL)
    {
        opts->servers = (char**) NATS_CALLOC(serversCount, sizeof(char*));
        if (opts->servers == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);

        for (i = 0; (s == NATS_OK) && (i < serversCount); i++)
        {
            s = nats_Trim(&(opts->servers[i]), servers[i]);
            if (s == NATS_OK)
                opts->serversCount++;
        }
    }

    if (s != NATS_OK)
        _freeServers(opts);

    UNLOCK_OPTS(opts);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_SetNoRandomize(natsOptions *opts, bool noRandomize)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->noRandomize = noRandomize;

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetTimeout(natsOptions *opts, int64_t timeout)
{
    LOCK_AND_CHECK_OPTIONS(opts, (timeout < 0));

    opts->timeout = timeout;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}


natsStatus
natsOptions_SetName(natsOptions *opts, const char *name)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    NATS_FREE(opts->name);
    opts->name = NULL;
    if (name != NULL)
    {
        opts->name = NATS_STRDUP(name);
        if (opts->name == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetUserInfo(natsOptions *opts, const char *user, const char *password)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    NATS_FREE(opts->user);
    opts->user= NULL;
    NATS_FREE(opts->password);
    opts->password = NULL;
    if (user != NULL)
    {
        opts->user = NATS_STRDUP(user);
        if (opts->user== NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    if ((s == NATS_OK) && (password != NULL))
    {
        opts->password = NATS_STRDUP(password);
        if (opts->password == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetToken(natsOptions *opts, const char *token)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if ((token != NULL) && (opts->tokenCb != NULL))
        s = nats_setError(NATS_ILLEGAL_STATE, "%s", "Cannot set a token if a token handler has already been set");
    else
    {
        NATS_FREE(opts->token);
        opts->token = NULL;
        if (token != NULL)
        {
            opts->token = NATS_STRDUP(token);
            if (opts->token == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetTokenHandler(natsOptions *opts, natsTokenHandler tokenCb, void *closure)
{
    natsStatus  s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if ((tokenCb != NULL) && (opts->token != NULL))
        s = nats_setError(NATS_ILLEGAL_STATE, "%s", "Cannot set a token handler if a token has already been set");
    else
    {
        opts->tokenCb = tokenCb;
        opts->tokenCbClosure = closure;
    }

    UNLOCK_OPTS(opts);

    return s;
}

static void
natsSSLCtx_release(natsSSLCtx *ctx)
{
    int refs;

    if (ctx == NULL)
        return;

    natsMutex_Lock(ctx->lock);

    refs = --(ctx->refs);

    natsMutex_Unlock(ctx->lock);

    if (refs == 0)
    {
        NATS_FREE(ctx->expectedHostname);
        SSL_CTX_free(ctx->ctx);
        natsMutex_Destroy(ctx->lock);
        NATS_FREE(ctx);
    }
}

static natsSSLCtx*
natsSSLCtx_retain(natsSSLCtx *ctx)
{
    natsMutex_Lock(ctx->lock);
    ctx->refs++;
    natsMutex_Unlock(ctx->lock);

    return ctx;
}

#if defined(NATS_HAS_TLS)

static natsStatus
_createSSLCtx(natsSSLCtx **newCtx)
{
    natsStatus  s    = NATS_OK;
    natsSSLCtx  *ctx = NULL;

    ctx = (natsSSLCtx*) NATS_CALLOC(1, sizeof(natsSSLCtx));
    if (ctx == NULL)
        s = nats_setDefaultError(NATS_NO_MEMORY);

    if (s == NATS_OK)
    {
        ctx->refs = 1;

        s = natsMutex_Create(&(ctx->lock));
    }
    if (s == NATS_OK)
    {
#if defined(NATS_USE_OPENSSL_1_1)
        ctx->ctx = SSL_CTX_new(TLS_client_method());
#else
        ctx->ctx = SSL_CTX_new(TLSv1_2_client_method());
#endif
        if (ctx->ctx == NULL)
            s = nats_setError(NATS_SSL_ERROR,
                              "Unable to create SSL context: %s",
                              NATS_SSL_ERR_REASON_STRING);
    }

    if (s == NATS_OK)
    {
        (void) SSL_CTX_set_mode(ctx->ctx, SSL_MODE_AUTO_RETRY);

#if defined(NATS_USE_OPENSSL_1_1)
        SSL_CTX_set_min_proto_version(ctx->ctx, TLS1_2_VERSION);
#else
        SSL_CTX_set_options(ctx->ctx, SSL_OP_NO_SSLv2);
        SSL_CTX_set_options(ctx->ctx, SSL_OP_NO_SSLv3);
#endif
        SSL_CTX_set_default_verify_paths(ctx->ctx);

        *newCtx = ctx;
    }
    else if (ctx != NULL)
    {
        natsSSLCtx_release(ctx);
    }

    return NATS_UPDATE_ERR_STACK(s);
}

static natsStatus
_getSSLCtx(natsOptions *opts)
{
    natsStatus s;

    s = nats_sslInit();
    if ((s == NATS_OK) && (opts->sslCtx != NULL))
    {
        bool createNew = false;

        natsMutex_Lock(opts->sslCtx->lock);

        // If this context is retained by a cloned natsOptions, we need to
        // release it and create a new context.
        if (opts->sslCtx->refs > 1)
            createNew = true;

        natsMutex_Unlock(opts->sslCtx->lock);

        if (createNew)
        {
            natsSSLCtx_release(opts->sslCtx);
            opts->sslCtx = NULL;
        }
        else
        {
            // We can use this ssl context.
            return NATS_OK;
        }
    }

    if (s == NATS_OK)
        s = _createSSLCtx(&(opts->sslCtx));

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_SetSecure(natsOptions *opts, bool secure)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    if (!secure && (opts->sslCtx != NULL))
    {
        natsSSLCtx_release(opts->sslCtx);
        opts->sslCtx = NULL;
    }
    else if (secure && (opts->sslCtx == NULL))
    {
        s = _getSSLCtx(opts);
    }

    if (s == NATS_OK)
        opts->secure = secure;

    UNLOCK_OPTS(opts);

    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_LoadCATrustedCertificates(natsOptions *opts, const char *fileName)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, ((fileName == NULL) || (fileName[0] == '\0')));

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        nats_sslRegisterThreadForCleanup();

        if (SSL_CTX_load_verify_locations(opts->sslCtx->ctx, fileName, NULL) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error loading trusted certificates '%s': %s",
                              fileName,
                              NATS_SSL_ERR_REASON_STRING);
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetCATrustedCertificates(natsOptions *opts, const char *certs)
{
    natsStatus s = NATS_OK;

    if (nats_IsStringEmpty(certs))
    {
        return nats_setError(NATS_INVALID_ARG, "%s",
                             "CA certificates can't be NULL nor empty");
    }

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        BIO                 *bio  = NULL;
        X509_STORE          *cts  = NULL;
        STACK_OF(X509_INFO) *inf  = NULL;
        int i;

        nats_sslRegisterThreadForCleanup();

        cts = SSL_CTX_get_cert_store(opts->sslCtx->ctx);
        if (cts == NULL)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "unable to get certificates store: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
        if (s == NATS_OK)
        {
            bio = BIO_new_mem_buf((char*) certs, -1);
            if (bio != NULL)
                inf = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL);
            if ((inf == NULL) || (sk_X509_INFO_num(inf) == 0))
            {
                s = nats_setError(NATS_SSL_ERROR,
                                  "unable to get CA certificates: %s",
                                  NATS_SSL_ERR_REASON_STRING);
            }
        }
        for (i = 0; ((s == NATS_OK) && (i < (int)sk_X509_INFO_num(inf))); i++)
        {
            X509_INFO *itmp = sk_X509_INFO_value(inf, i);
            if (itmp->x509)
            {
                if (X509_STORE_add_cert(cts, itmp->x509) != 1)
                {
                    s = nats_setError(NATS_SSL_ERROR,
                                      "error adding CA certificates: %s",
                                      NATS_SSL_ERR_REASON_STRING);
                }
            }
            if ((s == NATS_OK) && (itmp->crl))
            {
                if (X509_STORE_add_crl(cts, itmp->crl) != 1)
                {
                    s = nats_setError(NATS_SSL_ERROR,
                                      "error adding CA CRL: %s",
                                      NATS_SSL_ERR_REASON_STRING);
                }
            }
        }

        if (inf != NULL)
            sk_X509_INFO_pop_free(inf, X509_INFO_free);

        if (bio != NULL)
            BIO_free(bio);
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_LoadCertificatesChain(natsOptions *opts,
                                  const char *certFileName,
                                  const char *keyFileName)
{
    natsStatus s = NATS_OK;

    if ((certFileName == NULL) || (certFileName[0] == '\0')
        || (keyFileName == NULL) || (keyFileName[0] == '\0'))
    {
        return nats_setError(NATS_INVALID_ARG, "%s",
                             "certificate and key file names can't be NULL nor empty");
    }

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        nats_sslRegisterThreadForCleanup();

        if (SSL_CTX_use_certificate_chain_file(opts->sslCtx->ctx, certFileName) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error loading certificate chain '%s': %s",
                              certFileName,
                              NATS_SSL_ERR_REASON_STRING);
        }
    }
    if (s == NATS_OK)
    {
        if (SSL_CTX_use_PrivateKey_file(opts->sslCtx->ctx, keyFileName, SSL_FILETYPE_PEM) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error loading private key '%s': %s",
                              keyFileName,
                              NATS_SSL_ERR_REASON_STRING);
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetCertificatesChain(natsOptions *opts, const char *certStr, const char *keyStr)
{
    natsStatus  s = NATS_OK;

    if (nats_IsStringEmpty(certStr) || nats_IsStringEmpty(keyStr))
    {
        return nats_setError(NATS_INVALID_ARG, "%s",
                             "certificate and key can't be NULL nor empty");
    }

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        X509 *cert = NULL;
        BIO  *bio  = NULL;

        nats_sslRegisterThreadForCleanup();

        bio = BIO_new_mem_buf((char*) certStr, -1);
        if ((bio == NULL) || ((cert = PEM_read_bio_X509(bio, NULL, 0, NULL)) == NULL))
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error creating certificate: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
        if ((s == NATS_OK) && (SSL_CTX_use_certificate(opts->sslCtx->ctx, cert) != 1))
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error using certificate: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
        if (cert != NULL)
            X509_free(cert);
        if (bio != NULL)
            BIO_free(bio);
    }
    if (s == NATS_OK)
    {
        BIO         *bio  = NULL;
        EVP_PKEY    *pkey = NULL;

        bio = BIO_new_mem_buf((char*) keyStr, -1);
        if ((bio == NULL) || ((pkey = PEM_read_bio_PrivateKey(bio, NULL, 0, NULL)) == NULL))
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error creating key: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }

        if ((s == NATS_OK) && (SSL_CTX_use_PrivateKey(opts->sslCtx->ctx, pkey) != 1))
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error using private key: %s",
                              NATS_SSL_ERR_REASON_STRING);
        }
        if (pkey != NULL)
            EVP_PKEY_free(pkey);
        if (bio != NULL)
            BIO_free(bio);
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetCiphers(natsOptions *opts, const char *ciphers)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, ((ciphers == NULL) || (ciphers[0] == '\0')));

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        nats_sslRegisterThreadForCleanup();

        if (SSL_CTX_set_cipher_list(opts->sslCtx->ctx, ciphers) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error setting ciphers '%s': %s",
                              ciphers,
                              NATS_SSL_ERR_REASON_STRING);
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetCipherSuites(natsOptions *opts, const char *ciphers)
{
    natsStatus s = NATS_OK;

#if defined(NATS_USE_OPENSSL_1_1)
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        nats_sslRegisterThreadForCleanup();

        if (SSL_CTX_set_ciphersuites(opts->sslCtx->ctx, ciphers) != 1)
        {
            s = nats_setError(NATS_SSL_ERROR,
                              "Error setting ciphers '%s': %s",
                              ciphers,
                              NATS_SSL_ERR_REASON_STRING);
        }
    }

    UNLOCK_OPTS(opts);
#else
    s = nats_setError(NATS_ERR, "%s", "Setting TLSv1.3 ciphersuites requires OpenSSL 1.1+");
#endif

    return s;
}

natsStatus
natsOptions_SetExpectedHostname(natsOptions *opts, const char *hostname)
{
    natsStatus s = NATS_OK;

    // Allow hostname to be empty in order to reset...
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
    {
        NATS_FREE(opts->sslCtx->expectedHostname);
        opts->sslCtx->expectedHostname = NULL;

        if (hostname != NULL)
        {
            opts->sslCtx->expectedHostname = NATS_STRDUP(hostname);
            if (opts->sslCtx->expectedHostname == NULL)
            {
                s = nats_setDefaultError(NATS_NO_MEMORY);
            }
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SkipServerVerification(natsOptions *opts, bool skip)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    s = _getSSLCtx(opts);
    if (s == NATS_OK)
        opts->sslCtx->skipVerify = skip;

    UNLOCK_OPTS(opts);

    return s;
}

#else

natsStatus
natsOptions_SetSecure(natsOptions *opts, bool secure)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_LoadCATrustedCertificates(natsOptions *opts, const char *fileName)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_SetCATrustedCertificates(natsOptions *opts, const char *certificates)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_LoadCertificatesChain(natsOptions *opts,
                                  const char *certFileName,
                                  const char *keyFileName)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_SetCertificatesChain(natsOptions *opts, const char *certStr, const char *keyStr)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_SetCiphers(natsOptions *opts, const char *ciphers)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_SetExpectedHostname(natsOptions *opts, const char *hostname)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

natsStatus
natsOptions_SkipServerVerification(natsOptions *opts, bool skip)
{
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
}

#endif

natsStatus
natsOptions_SetVerbose(natsOptions *opts, bool verbose)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->verbose = verbose;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetPedantic(natsOptions *opts, bool pedantic)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->pedantic = pedantic;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetPingInterval(natsOptions *opts, int64_t interval)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->pingInterval = interval;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxPingsOut(natsOptions *opts, int maxPignsOut)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->maxPingsOut = maxPignsOut;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetIOBufSize(natsOptions *opts, int ioBufSize)
{
    LOCK_AND_CHECK_OPTIONS(opts, (ioBufSize < 0));

    opts->ioBufSize = ioBufSize;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetAllowReconnect(natsOptions *opts, bool allow)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->allowReconnect = allow;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxReconnect(natsOptions *opts, int maxReconnect)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->maxReconnect = maxReconnect;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectWait(natsOptions *opts, int64_t reconnectWait)
{
    LOCK_AND_CHECK_OPTIONS(opts, (reconnectWait < 0));

    opts->reconnectWait = reconnectWait;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectJitter(natsOptions *opts, int64_t jitter, int64_t jitterTLS)
{
    LOCK_AND_CHECK_OPTIONS(opts, ((jitter < 0) || (jitterTLS < 0)));

    opts->reconnectJitter    = jitter;
    opts->reconnectJitterTLS = jitterTLS;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetCustomReconnectDelay(natsOptions *opts,
                                    natsCustomReconnectDelayHandler cb,
                                    void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->customReconnectDelayCB        = cb;
    opts->customReconnectDelayCBClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectBufSize(natsOptions *opts, int reconnectBufSize)
{
    LOCK_AND_CHECK_OPTIONS(opts, (reconnectBufSize < 0));

    opts->reconnectBufSize = reconnectBufSize;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxPendingMsgs(natsOptions *opts, int maxPending)
{
    LOCK_AND_CHECK_OPTIONS(opts, (maxPending <= 0));

    opts->maxPendingMsgs = maxPending;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetMaxPendingBytes(natsOptions* opts, int64_t maxPending)
{
    LOCK_AND_CHECK_OPTIONS(opts, (maxPending <= 0));

    opts->maxPendingBytes = maxPending;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->asyncErrCb = errHandler;
    opts->asyncErrCbClosure = closure;

    if (opts->asyncErrCb == NULL)
        opts->asyncErrCb = natsConn_defaultErrHandler;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetClosedCB(natsOptions *opts, natsConnectionHandler closedCb,
                        void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->closedCb = closedCb;
    opts->closedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_setMicroCallbacks(natsOptions *opts, natsConnectionHandler closed, natsErrHandler errHandler)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->microClosedCb = closed;
    opts->microAsyncErrCb = errHandler;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetDisconnectedCB(natsOptions *opts,
                              natsConnectionHandler disconnectedCb,
                              void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->disconnectedCb = disconnectedCb;
    opts->disconnectedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetReconnectedCB(natsOptions *opts,
                             natsConnectionHandler reconnectedCb,
                             void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->reconnectedCb = reconnectedCb;
    opts->reconnectedCbClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetDiscoveredServersCB(natsOptions *opts,
                                   natsConnectionHandler discoveredServersCb,
                                   void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->discoveredServersCb = discoveredServersCb;
    opts->discoveredServersClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetIgnoreDiscoveredServers(natsOptions *opts, bool ignore)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->ignoreDiscoveredServers = ignore;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetLameDuckModeCB(natsOptions *opts,
                              natsConnectionHandler lameDuckCb,
                              void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->lameDuckCb = lameDuckCb;
    opts->lameDuckClosure = closure;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetEventLoop(natsOptions *opts,
                         void *loop,
                         natsEvLoop_Attach          attachCb,
                         natsEvLoop_ReadAddRemove   readCb,
                         natsEvLoop_WriteAddRemove  writeCb,
                         natsEvLoop_Detach          detachCb)
{
    LOCK_AND_CHECK_OPTIONS(opts, (loop == NULL)
                                 || (attachCb == NULL)
                                 || (readCb == NULL)
                                 || (writeCb == NULL)
                                 || (detachCb == NULL));

    opts->evLoop        = loop;
    opts->evCbs.attach  = attachCb;
    opts->evCbs.read    = readCb;
    opts->evCbs.write   = writeCb;
    opts->evCbs.detach  = detachCb;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_UseGlobalMessageDelivery(natsOptions *opts, bool global)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    // Sets if the subscriptions created from the connection will
    // create their own delivery thread or use the one(s) from
    // the library.
    opts->libMsgDelivery = global;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_IPResolutionOrder(natsOptions *opts, int order)
{
    LOCK_AND_CHECK_OPTIONS(opts, ((order != 0)
                                    && (order != 4)
                                    && (order != 6)
                                    && (order != 46)
                                    && (order != 64)));

    opts->orderIP = order;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetSendAsap(natsOptions *opts, bool sendAsap)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);
    opts->sendAsap = sendAsap;
    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetNoEcho(natsOptions *opts, bool noEcho)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);
    opts->noEcho = noEcho;
    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetRetryOnFailedConnect(natsOptions *opts, bool retry,
        natsConnectionHandler connectedCb, void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);
    opts->retryOnFailedConnect = retry;
    if (!retry)
    {
        opts->connectedCb = NULL;
        opts->connectedCbClosure = NULL;
    }
    else
    {
        opts->connectedCb = connectedCb;
        opts->connectedCbClosure = closure;
    }
    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_UseOldRequestStyle(natsOptions *opts, bool useOldStype)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);
    opts->useOldRequestStyle = useOldStype;
    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetFailRequestsOnDisconnect(natsOptions *opts, bool failRequests)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);
    opts->failRequestsOnDisconnect = failRequests;
    UNLOCK_OPTS(opts);

    return NATS_OK;
}

static void
_freeUserCreds(userCreds *uc)
{
    if (uc == NULL)
        return;

    NATS_FREE(uc->userOrChainedFile);
    NATS_FREE(uc->seedFile);
    NATS_FREE(uc->jwtAndSeedContent);
    NATS_FREE(uc);
}

static natsStatus
_createUserCreds(userCreds **puc, const char *uocf, const char *sf, const char* jwtAndSeedContent)
{
    natsStatus  s   = NATS_OK;
    userCreds   *uc = NULL;

    uc = NATS_CALLOC(1, sizeof(userCreds));
    if (uc == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    // in case of content, we do not need to read the files anymore
    if (jwtAndSeedContent != NULL)
    {
        uc->jwtAndSeedContent = NATS_STRDUP(jwtAndSeedContent);
        if (uc->jwtAndSeedContent == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }
    else
    {
        if (uocf)
        {
            uc->userOrChainedFile = NATS_STRDUP(uocf);
            if (uc->userOrChainedFile == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
        if ((s == NATS_OK) && sf != NULL)
        {
            uc->seedFile = NATS_STRDUP(sf);
            if (uc->seedFile == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
        }
    }
    if (s != NATS_OK)
        _freeUserCreds(uc);
    else
        *puc = uc;

    return NATS_UPDATE_ERR_STACK(s);
}

static void
_setAndUnlockOptsFromUserCreds(natsOptions *opts, userCreds *uc)
{
    // Free previous object
    _freeUserCreds(opts->userCreds);
    // Set to new one (possibly NULL)
    opts->userCreds = uc;

    if (uc != NULL)
    {
        opts->userJWTHandler = natsConn_userCreds;
        opts->userJWTClosure = (void*) uc;

        opts->sigHandler = natsConn_signatureHandler;
        opts->sigClosure = (void*) uc;

        // NKey and UserCreds are mutually exclusive.
        if (opts->nkey != NULL)
        {
            NATS_FREE(opts->nkey);
            opts->nkey = NULL;
        }
    }
    else
    {
        opts->userJWTHandler = NULL;
        opts->userJWTClosure = NULL;

        opts->sigHandler = NULL;
        opts->sigClosure = NULL;
    }

    UNLOCK_OPTS(opts);
}

natsStatus
natsOptions_SetUserCredentialsFromFiles(natsOptions *opts, const char *userOrChainedFile, const char *seedFile)
{
    natsStatus  s   = NATS_OK;
    userCreds   *uc = NULL;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    // Both files can be NULL (to unset), but if seeFile can't
    // be set if userOrChainedFile is not.
    if (nats_IsStringEmpty(userOrChainedFile) && !nats_IsStringEmpty(seedFile))
    {
        UNLOCK_OPTS(opts);
        return nats_setError(NATS_INVALID_ARG, "%s", "user or chained file need to be specified");
    }

    if (!nats_IsStringEmpty(userOrChainedFile))
    {
        s = _createUserCreds(&uc, userOrChainedFile, seedFile, NULL);
        if (s != NATS_OK)
        {
            UNLOCK_OPTS(opts);
            return NATS_UPDATE_ERR_STACK(s);
        }
    }

    _setAndUnlockOptsFromUserCreds(opts, uc);

    return NATS_OK;
}

natsStatus
natsOptions_SetUserCredentialsFromMemory(natsOptions *opts, const char *jwtAndSeedContent)
{
    natsStatus  s   = NATS_OK;
    userCreds   *uc = NULL;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    // if content is not NULL create user creds from it;
    // otherwise NULL will later lead to setting handlers to NULL
    if (jwtAndSeedContent != NULL)
    {
        s = _createUserCreds(&uc, NULL, NULL, jwtAndSeedContent);
        if (s != NATS_OK)
        {
            UNLOCK_OPTS(opts);
            return NATS_UPDATE_ERR_STACK(s);
        }
    }

    _setAndUnlockOptsFromUserCreds(opts, uc);

    return NATS_OK;
}

natsStatus
natsOptions_SetUserCredentialsCallbacks(natsOptions *opts,
                                        natsUserJWTHandler      ujwtCB,
                                        void                    *ujwtClosure,
                                        natsSignatureHandler    sigCB,
                                        void                    *sigClosure)
{
    // Callbacks can all be NULL (to unset), however, if one is set,
    // the other must be.
    LOCK_AND_CHECK_OPTIONS(opts,
            (((ujwtCB != NULL) && (sigCB == NULL)) ||
                    ((ujwtCB == NULL) && (sigCB != NULL))));

    _freeUserCreds(opts->userCreds);
    opts->userCreds = NULL;

    opts->userJWTHandler = ujwtCB;
    opts->userJWTClosure = ujwtClosure;

    opts->sigHandler = sigCB;
    opts->sigClosure = sigClosure;

    // If setting callbacks and there is an NKey, erase it
    // (NKey and UserCreds are mutually exclusive).
    if ((ujwtCB != NULL) && (opts->nkey != NULL))
    {
        NATS_FREE(opts->nkey);
        opts->nkey = NULL;
    }

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_SetNKey(natsOptions             *opts,
                    const char              *pubKey,
                    natsSignatureHandler    sigCB,
                    void                    *sigClosure)
{
    char        *nk = NULL;

    // If pubKey is not empty, then signature must be specified
    LOCK_AND_CHECK_OPTIONS(opts,
            (!nats_IsStringEmpty(pubKey) && (sigCB == NULL)));

    if (!nats_IsStringEmpty(pubKey))
    {
        nk = NATS_STRDUP(pubKey);
        if (nk == NULL)
        {
            UNLOCK_OPTS(opts);
            return nats_setDefaultError(NATS_NO_MEMORY);
        }
    }

    // Free previous value
    NATS_FREE(opts->nkey);

    // Set new values
    opts->nkey       = nk;
    opts->sigHandler = sigCB;
    opts->sigClosure = sigClosure;

    // If we set an NKey, make sure that userJWT is unset
    // since the two are mutually exclusive.
    if (nk != NULL)
    {
        if (opts->userCreds != NULL)
        {
            _freeUserCreds(opts->userCreds);
            opts->userCreds = NULL;
        }
        opts->userJWTHandler = NULL;
        opts->userJWTClosure = NULL;
    }
    UNLOCK_OPTS(opts);
    return NATS_OK;
}

natsStatus
natsOptions_SetNKeyFromSeed(natsOptions *opts,
                            const char  *pubKey,
                            const char  *seedFile)
{
    natsStatus  s;
    char        *nk = NULL;
    userCreds   *uc = NULL;

    LOCK_AND_CHECK_OPTIONS(opts,
        (!nats_IsStringEmpty(pubKey) && nats_IsStringEmpty(seedFile)));

    if (!nats_IsStringEmpty(pubKey))
    {
        nk = NATS_STRDUP(pubKey);
        if (nk == NULL)
        {
            UNLOCK_OPTS(opts);
            return nats_setDefaultError(NATS_NO_MEMORY);
        }
        s = _createUserCreds(&uc, NULL, seedFile, NULL);
        if (s != NATS_OK)
        {
            NATS_FREE(nk);
            UNLOCK_OPTS(opts);
            return NATS_UPDATE_ERR_STACK(s);
        }
    }

    // Free previous values
    NATS_FREE(opts->nkey);
    _freeUserCreds(opts->userCreds);

    // Set new values
    opts->nkey       = nk;
    opts->sigHandler = (nk == NULL ? NULL : natsConn_signatureHandler);
    opts->sigClosure = (nk == NULL ? NULL : (void*) uc);
    opts->userCreds  = (nk == NULL ? NULL : uc);
    // NKey and JWT mutually exclusive
    opts->userJWTHandler = NULL;
    opts->userJWTClosure = NULL;

    UNLOCK_OPTS(opts);
    return NATS_OK;
}

natsStatus
natsOptions_SetWriteDeadline(natsOptions *opts, int64_t deadline)
{
    LOCK_AND_CHECK_OPTIONS(opts, (deadline < 0));

    opts->writeDeadline = deadline;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

natsStatus
natsOptions_DisableNoResponders(natsOptions *opts, bool disabled)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->disableNoResponders = disabled;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

static natsStatus
_setCustomInboxPrefix(natsOptions *opts, const char *inboxPrefix, bool check)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, 0);

    NATS_FREE(opts->inboxPfx);
    opts->inboxPfx = NULL;

    if (!nats_IsStringEmpty(inboxPrefix))
    {
        // If not called from clone(), we need to check the validity of the
        // inbox prefix.
        if (check && !nats_IsSubjectValid(inboxPrefix, false))
            s = nats_setError(NATS_INVALID_ARG, "Invalid inbox prefix '%s'", inboxPrefix);

        if (s == NATS_OK)
        {
            // If invoked from user, there will not be the last '.', which
            // we will add here.
            if (check)
            {
                if (nats_asprintf(&opts->inboxPfx, "%s.", inboxPrefix) < 0)
                    s = nats_setDefaultError(NATS_NO_MEMORY);
            }
            else
            {
                // We are invoked from clone(), simply duplicate the string.
                DUP_STRING(s, opts->inboxPfx, inboxPrefix);
            }
        }
    }

    UNLOCK_OPTS(opts);

    return s;
}

natsStatus
natsOptions_SetCustomInboxPrefix(natsOptions *opts, const char *inboxPrefix)
{
    natsStatus s = _setCustomInboxPrefix(opts, inboxPrefix, true);
    return NATS_UPDATE_ERR_STACK(s);
}

natsStatus
natsOptions_SetMessageBufferPadding(natsOptions *opts, int paddingSize)
{
    LOCK_AND_CHECK_OPTIONS(opts, (paddingSize < 0));

    opts->payloadPaddingSize = paddingSize;

    UNLOCK_OPTS(opts);

    return NATS_OK;
}

static void
_freeOptions(natsOptions *opts)
{
    if (opts == NULL)
        return;

    NATS_FREE(opts->url);
    NATS_FREE(opts->name);
    _freeServers(opts);
    NATS_FREE(opts->user);
    NATS_FREE(opts->password);
    NATS_FREE(opts->token);
    NATS_FREE(opts->nkey);
    natsSSLCtx_release(opts->sslCtx);
    _freeUserCreds(opts->userCreds);
    NATS_FREE(opts->inboxPfx);
    natsMutex_Destroy(opts->mu);
    NATS_FREE(opts);
}

natsStatus
natsOptions_Create(natsOptions **newOpts)
{
    natsStatus  s;
    natsOptions *opts = NULL;

    // Ensure the library is loaded
    s = nats_Open(-1);
    if (s != NATS_OK)
        return s;

    opts = (natsOptions*) NATS_CALLOC(1, sizeof(natsOptions));
    if (opts == NULL)
        return nats_setDefaultError(NATS_NO_MEMORY);

    if (natsMutex_Create(&(opts->mu)) != NATS_OK)
    {
        NATS_FREE(opts);
        return NATS_UPDATE_ERR_STACK(NATS_NO_MEMORY);
    }

    opts->allowReconnect        = true;
    opts->secure                = false;
    opts->maxReconnect          = NATS_OPTS_DEFAULT_MAX_RECONNECT;
    opts->reconnectWait         = NATS_OPTS_DEFAULT_RECONNECT_WAIT;
    opts->pingInterval          = NATS_OPTS_DEFAULT_PING_INTERVAL;
    opts->maxPingsOut           = NATS_OPTS_DEFAULT_MAX_PING_OUT;
    opts->ioBufSize             = NATS_OPTS_DEFAULT_IO_BUF_SIZE;
    opts->maxPendingMsgs        = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;
    opts->maxPendingBytes       = -1;
    opts->timeout               = NATS_OPTS_DEFAULT_TIMEOUT;
    opts->libMsgDelivery        = natsLib_isLibHandlingMsgDeliveryByDefault();
    opts->writeDeadline         = natsLib_defaultWriteDeadline();
    opts->reconnectBufSize      = NATS_OPTS_DEFAULT_RECONNECT_BUF_SIZE;
    opts->reconnectJitter       = NATS_OPTS_DEFAULT_RECONNECT_JITTER;
    opts->reconnectJitterTLS    = NATS_OPTS_DEFAULT_RECONNECT_JITTER_TLS;
    opts->asyncErrCb            = natsConn_defaultErrHandler;

    *newOpts = opts;

    return NATS_OK;
}

natsOptions*
natsOptions_clone(natsOptions *opts)
{
    natsStatus  s       = NATS_OK;
    natsOptions *cloned = NULL;
    int         muSize;

    if ((s = natsOptions_Create(&cloned)) != NATS_OK)
    {
        NATS_UPDATE_ERR_STACK(s);
        return NULL;
    }

    natsMutex_Lock(opts->mu);

    muSize = sizeof(cloned->mu);

    // Make a blind copy first...
    memcpy((char*)cloned + muSize, (char*)opts + muSize,
           sizeof(natsOptions) - muSize);

    // Then remove all pointers, so that if we fail while
    // strduping them, and free the cloned, we don't free the strings
    // from the original.
    cloned->name    = NULL;
    cloned->servers = NULL;
    cloned->url     = NULL;
    cloned->sslCtx  = NULL;
    cloned->user    = NULL;
    cloned->password= NULL;
    cloned->token   = NULL;
    cloned->nkey    = NULL;
    cloned->userCreds = NULL;
    cloned->inboxPfx  = NULL;

    // Also, set the number of servers count to 0, until we update
    // it (if necessary) when calling SetServers.
    cloned->serversCount = 0;

    if (opts->name != NULL)
        s = natsOptions_SetName(cloned, opts->name);

    if ((s == NATS_OK) && (opts->url != NULL))
        s = natsOptions_SetURL(cloned, opts->url);

    if ((s == NATS_OK) && (opts->servers != NULL))
        s = natsOptions_SetServers(cloned,
                                   (const char**)opts->servers,
                                   opts->serversCount);

    if ((s == NATS_OK) && (opts->user != NULL))
        s = natsOptions_SetUserInfo(cloned, opts->user, opts->password);

    if ((s == NATS_OK) && (opts->token != NULL))
        s = natsOptions_SetToken(cloned, opts->token);

    if ((s == NATS_OK) && (opts->sslCtx != NULL))
        cloned->sslCtx = natsSSLCtx_retain(opts->sslCtx);

    if ((s == NATS_OK) && (opts->nkey != NULL))
    {
        if (opts->userCreds != NULL)
            s = natsOptions_SetNKeyFromSeed(cloned, opts->nkey, opts->userCreds->seedFile);
        else
            s = natsOptions_SetNKey(cloned, opts->nkey, opts->sigHandler, opts->sigClosure);
    }
    else if ((s == NATS_OK) && (opts->userCreds != NULL))
    {
        if (opts->userCreds->jwtAndSeedContent != NULL)
        {
            s = natsOptions_SetUserCredentialsFromMemory(cloned,
                                                        opts->userCreds->jwtAndSeedContent);
        }
        else
        {
            s = natsOptions_SetUserCredentialsFromFiles(cloned,
                                                        opts->userCreds->userOrChainedFile,
                                                        opts->userCreds->seedFile);
        }
    }
    if ((s == NATS_OK) && (opts->inboxPfx != NULL))
        s = _setCustomInboxPrefix(cloned, opts->inboxPfx, false);

    if (s != NATS_OK)
    {
        _freeOptions(cloned);
        cloned = NULL;
        NATS_UPDATE_ERR_STACK(s);
    }

    natsMutex_Unlock(opts->mu);

    return cloned;
}

void
natsOptions_Destroy(natsOptions *opts)
{
    if (opts == NULL)
        return;

    _freeOptions(opts);
}
