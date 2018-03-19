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

#ifndef URL_H_
#define URL_H_

#include "status.h"

typedef struct __natsUrl
{
    char    *fullUrl;
    char    *host;
    int     port;
    char    *username;
    char    *password;

} natsUrl;

natsStatus
natsUrl_Create(natsUrl **newUrl, const char *urlStr);

void
natsUrl_Destroy(natsUrl *url);

#endif /* SRC_URL_H_ */
