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

#ifndef NKEYS_H_
#define NKEYS_H_

#include "natsp.h"

#define NKEYS_INVALID_ENCODED_KEY   "invalid encoded key"
#define NKEYS_INVALID_CHECKSUM      "invalid checksum"
#define NKEYS_INVALID_SEED          "invalid seed"
#define NKEYS_INVALID_PREFIX        "invalid prefix byte"

natsStatus
natsKeys_Sign(const char *encodedSeed, const unsigned char *input, int inputLen, unsigned char *signature);

#endif /* NKEYS_H_ */
