// Copyright 2015-2018 The NATS Authors
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
    {
        opts->url = NATS_STRDUP(url);
        if (opts->url == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
    }

    UNLOCK_OPTS(opts);

    return s;
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
            opts->servers[i] = (char*) NATS_STRDUP(servers[i]);
            if (opts->servers[i] == NULL)
                s = nats_setDefaultError(NATS_NO_MEMORY);
            else
                opts->serversCount++;
        }
    }

    if (s != NATS_OK)
        _freeServers(opts);

    UNLOCK_OPTS(opts);

    return s;
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

    NATS_FREE(opts->token);
    opts->token= NULL;
    if (token != NULL)
    {
        opts->token = NATS_STRDUP(token);
        if (opts->token == NULL)
            s = nats_setDefaultError(NATS_NO_MEMORY);
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
// See section RFC 6125 Sections 2.4 and 3.1
static bool
_hostnameMatches(char *expr, char *string)
{
    int i, j;

    for (i = 0, j = 0; i < (int) strlen(expr); i++)
    {
        if (expr[i] == '*')
        {
            if (string[j] == '.')
                return 0;
            while (string[j] != '.')
                j++;
        }
        else if (expr[i] != string[j])
        {
            return false;
        }
        else
            j++;
    }

    return (j == (int) strlen(string));
}

// Does this hostname match an entry in the subjectAltName extension?
// returns: 0 if no, 1 if yes, -1 if no subjectAltName entries were found.
static int
_hostnameMatchesSubjectAltName(char *hostname, X509 *cert)
{
    bool                    foundAnyEntry = false;
    bool                    foundMatch = false;
    GENERAL_NAME            *namePart = NULL;
    STACK_OF(GENERAL_NAME)  *san;

    san = (STACK_OF(GENERAL_NAME)*) X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

    while (sk_GENERAL_NAME_num(san) > 0)
    {
        namePart = sk_GENERAL_NAME_pop(san);

        if (namePart->type == GEN_DNS)
        {
            foundAnyEntry = true;
            foundMatch = _hostnameMatches((char*) ASN1_STRING_data(namePart->d.uniformResourceIdentifier),
                                           hostname);
        }

        GENERAL_NAME_free(namePart);

        if (foundMatch)
            break;
    }

    GENERAL_NAMES_free(san);

    if (foundMatch)
        return 1;

    return (foundAnyEntry ? 0 : -1);
}

static bool
_hostnameMatchesSubjectCN(char *hostname, X509 *cert)
{
    X509_NAME       *name;
    X509_NAME_ENTRY *name_entry;
    char            *certname;
    int             position;

    name = X509_get_subject_name(cert);
    position = -1;
    while (1)
    {
        position = X509_NAME_get_index_by_NID(name, NID_commonName, position);
        if (position == -1)
            break;
        name_entry = X509_NAME_get_entry(name, position);
        certname = (char*) X509_NAME_ENTRY_get_data(name_entry)->data;
        if (_hostnameMatches(certname, hostname))
            return true;
    }

    return false;
}

static int
_hostnameMatchesCertificate(char *hostname, X509 *cert)
{
    int san_result = _hostnameMatchesSubjectAltName(hostname, cert);
    if (san_result > -1)
        return san_result;

    return _hostnameMatchesSubjectCN(hostname, cert);
}

static int
_verifyCb(int preverifyOk, X509_STORE_CTX* ctx)
{
    char            buf[256];
    SSL             *ssl  = NULL;
    X509            *cert = X509_STORE_CTX_get_current_cert(ctx);
    int             depth = X509_STORE_CTX_get_error_depth(ctx);
    int             err   = X509_STORE_CTX_get_error(ctx);
    natsConnection  *nc   = NULL;

    // Retrieve the SSL object, then our connection...
    ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    nc = (natsConnection*) SSL_get_ex_data(ssl, 0);

    // Should we skip serve certificate verification?
    if (nc->opts->sslCtx->skipVerify)
        return 1;

    // If the depth is greater than the limit (when not set, the limit is
    // 100), then report as an error.
    if (depth > 100)
    {
        preverifyOk = 0;
        err = X509_V_ERR_CERT_CHAIN_TOO_LONG;
        X509_STORE_CTX_set_error(ctx, err);
    }

    if (!preverifyOk)
    {
        X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
        snprintf(nc->errStr, sizeof(nc->errStr), "%d:%s:depth=%d:%s",
                 err, X509_verify_cert_error_string(err),
                 depth, buf);
    }

    if (!preverifyOk && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT))
    {
        X509_NAME_oneline(X509_get_issuer_name(cert), buf, sizeof(buf));
        snprintf(nc->errStr, sizeof(nc->errStr), "issuer=%s", buf);
    }

    if (preverifyOk
        && (depth == 0) // Verify hostname only for the server certificate.
        && (natsSSLCtx_getExpectedHostname(nc->opts->sslCtx) != NULL))
    {
        if (!_hostnameMatchesCertificate(
                natsSSLCtx_getExpectedHostname(nc->opts->sslCtx), cert))
        {
            snprintf(nc->errStr, sizeof(nc->errStr),
                     "Did not get expected hostname '%s'",
                     natsSSLCtx_getExpectedHostname(nc->opts->sslCtx));

            preverifyOk = 0;
        }
    }

    return preverifyOk;
}

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
#if defined(NATS_USE_TLS_CLIENT_METHOD)
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

        SSL_CTX_set_options(ctx->ctx, SSL_OP_NO_SSLv2);
        SSL_CTX_set_options(ctx->ctx, SSL_OP_NO_SSLv3);

        // Set to SSL_VERIFY_NONE so that we can get more error trace in
        // the verifyCb that is then used in conn.c's makeTLSConn function.
        SSL_CTX_set_verify(ctx->ctx, SSL_VERIFY_NONE, _verifyCb);

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
natsOptions_SetExpectedHostname(natsOptions *opts, const char *hostname)
{
    natsStatus s = NATS_OK;

    LOCK_AND_CHECK_OPTIONS(opts, ((hostname == NULL) || (hostname[0] == '\0')));

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
natsOptions_LoadCertificatesChain(natsOptions *opts,
                                  const char *certFileName,
                                  const char *keyFileName)
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
natsOptions_SetErrorHandler(natsOptions *opts, natsErrHandler errHandler,
                            void *closure)
{
    LOCK_AND_CHECK_OPTIONS(opts, 0);

    opts->asyncErrCb = errHandler;
    opts->asyncErrCbClosure = closure;

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
    natsMutex_Destroy(opts->mu);
    natsSSLCtx_release(opts->sslCtx);
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

    opts->allowReconnect = true;
    opts->secure         = false;
    opts->maxReconnect   = NATS_OPTS_DEFAULT_MAX_RECONNECT;
    opts->reconnectWait  = NATS_OPTS_DEFAULT_RECONNECT_WAIT;
    opts->pingInterval   = NATS_OPTS_DEFAULT_PING_INTERVAL;
    opts->maxPingsOut    = NATS_OPTS_DEFAULT_MAX_PING_OUT;
    opts->maxPendingMsgs = NATS_OPTS_DEFAULT_MAX_PENDING_MSGS;
    opts->timeout        = NATS_OPTS_DEFAULT_TIMEOUT;
    opts->libMsgDelivery = natsLib_isLibHandlingMsgDeliveryByDefault();

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

