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

#define natsMicroserviceAPIPrefix "$SRV"

enum natsMicroserviceVerb
{
    natsMicroserviceVerbPing = 0,
    natsMicroserviceVerbStats,
    natsMicroserviceVerbInfo,
    natsMicroserviceVerbSchema,
    natsMicroserviceVerbMax
};

typedef enum natsMicroserviceVerb natsMicroserviceVerb;

#define natsMicroserviceInfoResponseType "io.nats.micro.v1.info_response"
#define natsMicroservicePingResponseType "io.nats.micro.v1.ping_response"
#define natsMicroserviceStatsResponseType "io.nats.micro.v1.stats_response"
#define natsMicroserviceSchemaResponseType "io.nats.micro.v1.schema_response"

static natsStatus
natsMicroserviceVerb_String(const char **new_subject, natsMicroserviceVerb verb)
{
    if (new_subject == NULL)
        return nats_setDefaultError(NATS_INVALID_ARG);

    switch (verb)
    {
    case natsMicroserviceVerbPing:
        *new_subject = "PING";
        return NATS_OK;

    case natsMicroserviceVerbStats:
        *new_subject = "STATS";
        return NATS_OK;

    case natsMicroserviceVerbInfo:
        *new_subject = "INFO";
        return NATS_OK;

    case natsMicroserviceVerbSchema:
        *new_subject = "SCHEMA";
        return NATS_OK;
    default:
        return nats_setError(NATS_INVALID_ARG, "Invalid microservice verb %d", verb);
    }
}

#endif /* MICRO_H_ */
