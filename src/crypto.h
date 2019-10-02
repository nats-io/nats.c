// Copyright 2019 The NATS Authors
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

#ifndef CRYPTO_H_
#define CRYPTO_H_

#include "status.h"

#define NATS_CRYPTO_SECRET_BYTES    64
#define NATS_CRYPTO_SIGN_BYTES      64

natsStatus
natsCrypto_Init(void);

natsStatus
natsCrypto_Sign(const unsigned char *seed,
                const unsigned char *input, int inputLen,
                unsigned char signature[NATS_CRYPTO_SIGN_BYTES]);

void
natsCrypto_Clear(void *mem, int memLen);

#endif /* CRYPTO_H_ */
