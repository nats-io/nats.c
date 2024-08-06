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

