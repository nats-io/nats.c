// Copyright 2026 The NATS Authors
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

#include <nats/nats.h>
#include <nats/status.h>
#include <nats/version.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__,  \
                    #condition);                                        \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main(void)
{
    natsMsg *msg = NULL;
    natsOptions *opts = NULL;
    jsOptions js_opts;
    const char *value = NULL;

    CHECK(nats_Open(-1) == NATS_OK);
    CHECK(strcmp(nats_GetVersion(), NATS_VERSION_STRING) == 0);
    CHECK(nats_GetVersionNumber() == NATS_VERSION_NUMBER);
    CHECK(jsOptions_Init(&js_opts) == NATS_OK);
    CHECK(js_opts.Wait == 0);
    CHECK(js_opts.Domain == NULL);
    CHECK(natsMsg_Create(&msg, "bcr.smoke", NULL, "hello", 5) == NATS_OK);
    CHECK(strcmp(natsMsg_GetSubject(msg), "bcr.smoke") == 0);
    CHECK(natsMsg_GetDataLength(msg) == 5);
    CHECK(memcmp(natsMsg_GetData(msg), "hello", 5) == 0);
    CHECK(natsMsgHeader_Set(msg, "Bcr-Test", "smoke") == NATS_OK);
    CHECK(natsMsgHeader_Get(msg, "Bcr-Test", &value) == NATS_OK);
    CHECK(strcmp(value, "smoke") == 0);
    natsMsg_Destroy(msg);

    CHECK(natsOptions_Create(&opts) == NATS_OK);
    // These setters fail if TLS support was omitted from the build.
    CHECK(natsOptions_SetSecure(opts, true) == NATS_OK);
    CHECK(natsOptions_SetExpectedHostname(opts, "localhost") == NATS_OK);
    natsOptions_Destroy(opts);

    CHECK(nats_CloseAndWait(5000) == NATS_OK);
    return 0;
}
