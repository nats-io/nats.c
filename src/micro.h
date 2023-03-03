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

#ifndef MICRO_H_
#define MICRO_H_

#define jsMicroserviceAPIPrefix "$SRV"

enum jsMicroserviceVerb
{
    jsMicroserviceVerbPing = 0,
    jsMicroserviceVerbStats,
    jsMicroserviceVerbInfo,
    jsMicroserviceVerbSchema,
    jsMicroserviceVerbMax
};

typedef enum jsMicroserviceVerb jsMicroserviceVerb;

#define jsMicroserviceInfoResponseType "io.nats.micro.v1.info_response"
#define jsMicroservicePingResponseType "io.nats.micro.v1.ping_response"
#define jsMicroserviceStatsResponseType "io.nats.micro.v1.stats_response"
#define jsMicroserviceSchemaResponseType "io.nats.micro.v1.schema_response"

static natsStatus
jsMicroserviceVerb_String(const char **new_subject, jsMicroserviceVerb verb)
{
    if (new_subject == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    switch (verb)
    {
    case jsMicroserviceVerbPing:
        *new_subject = "PING";
        return NATS_OK;

    case jsMicroserviceVerbStats:
        *new_subject = "STATS";
        return NATS_OK;

    case jsMicroserviceVerbInfo:
        *new_subject = "INFO";
        return NATS_OK;

    case jsMicroserviceVerbSchema:
        *new_subject = "SCHEMA";
        return NATS_OK;
    default:
        return nats_setError(NATS_INVALID_ARG, "Invalid microservice verb %d", verb);
    }
}

#endif /* MICRO_H_ */
