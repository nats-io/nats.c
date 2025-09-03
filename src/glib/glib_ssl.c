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

#include "glibp.h"


void
nats_cleanupThreadSSL(void *localStorage)
{
#if defined(NATS_HAS_TLS) && !defined(NATS_USE_OPENSSL_1_1)
    ERR_remove_thread_state(0);
#endif
}

void nats_sslRegisterThreadForCleanup(void)
{
#if defined(NATS_HAS_TLS)
    natsLib *lib = nats_lib();
    // Set anything. The goal is that at thread exit, the thread local key
    // will have something non NULL associated, which will trigger the
    // destructor that we have registered.
    (void)natsThreadLocal_Set(lib->sslTLKey, (void *)1);
#endif
}

natsStatus
nats_initSSL(void)
{
    natsLib *lib = nats_lib();
    natsStatus s = NATS_OK;

    // Ensure the library is loaded
    s = nats_openLib(NULL);
    if (s != NATS_OK)
        return s;

    natsMutex_Lock(lib->lock);

    if (!lib->sslInitialized)
    {
        // Regardless of success, mark as initialized so that we
        // can do cleanup on exit.
        lib->sslInitialized = true;

#if defined(NATS_HAS_TLS)
#if !defined(NATS_USE_OPENSSL_1_1)
        // Initialize SSL.
        SSL_library_init();
        SSL_load_error_strings();
#endif
#endif
        s = natsThreadLocal_CreateKey(&(lib->sslTLKey), nats_cleanupThreadSSL);
    }

    natsMutex_Unlock(lib->lock);

    return NATS_UPDATE_ERR_STACK(s);
}

static bool hashNoErrorOnNoSSL = false;

void
nats_hashNoErrorOnNoSSL(bool noError)
{
    hashNoErrorOnNoSSL = noError;
}

natsStatus
nats_hashNew(nats_hash **new_hash)
{
#if defined(NATS_HAS_TLS)
    EVP_MD_CTX *h = EVP_MD_CTX_new();
    if (h == NULL)
        return nats_setError(NATS_SSL_ERROR, "unable to create hash: %s", NATS_SSL_ERR_REASON_STRING);

    if (!EVP_DigestInit_ex(h, EVP_sha256(), NULL))
    {
        EVP_MD_CTX_free(h);
        return nats_setError(NATS_SSL_ERROR, "unable to create hash: %s", NATS_SSL_ERR_REASON_STRING);
    }
    *new_hash = (nats_hash*) h;
    return NATS_OK;
#else
    if (hashNoErrorOnNoSSL)
    {
        *new_hash = NULL;
        return NATS_OK;
    }
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
#endif
}

natsStatus
nats_hashWrite(nats_hash *hash, const void *data, int dataLen)
{
#if defined(NATS_HAS_TLS)
    if (!EVP_DigestUpdate((EVP_MD_CTX*) hash, data, (size_t) dataLen))
        return nats_setError(NATS_SSL_ERROR, "error writing into hash: %s", NATS_SSL_ERR_REASON_STRING);
    return NATS_OK;
#else
    if (hashNoErrorOnNoSSL)
        return NATS_OK;
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
#endif
}

natsStatus
nats_hashSum(nats_hash *hash, unsigned char *digest, unsigned int *len)
{
#if defined(NATS_HAS_TLS)
    if (!EVP_DigestFinal_ex((EVP_MD_CTX*) hash, digest, len))
        return nats_setError(NATS_SSL_ERROR, "error finalizing hash: %s", NATS_SSL_ERR_REASON_STRING);

    return NATS_OK;

#else
    if (hashNoErrorOnNoSSL)
    {
        const char          *nss = "not supported";
        const unsigned int  slen  = (unsigned int) strlen(nss);
        memcpy(digest, nss, (size_t) slen);
        *len = slen;
        return NATS_OK;
    }
    return nats_setError(NATS_ILLEGAL_STATE, "%s", NO_SSL_ERR);
#endif
}

void
nats_hashDestroy(nats_hash *hash)
{
    if (hash == NULL)
        return;

#if defined(NATS_HAS_TLS)
    EVP_MD_CTX_free((EVP_MD_CTX*) hash);
#endif
}

