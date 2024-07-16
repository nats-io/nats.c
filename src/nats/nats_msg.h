// Copyright 2015-2023 The NATS Authors
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

#ifndef NATS_MESSAGE_H_
#define NATS_MESSAGE_H_

#include "nats_base.h"
#include "nats_mem.h"
#include "nats_net.h"

NATS_EXTERN natsStatus nats_AsyncPublish(natsConnection *nc, natsMessage *msg);
NATS_EXTERN natsStatus nats_AsyncPublishNoCopy(natsConnection *nc, natsMessage *msg);
NATS_EXTERN natsStatus nats_CreateMessage(natsMessage **newm, natsConnection *nc, const char *subj);
NATS_EXTERN natsStatus nats_RetainMessage(natsMessage *msg);
NATS_EXTERN natsStatus nats_SetMessageHeader(natsMessage *m, const char *key, const char *value);
NATS_EXTERN natsStatus nats_SetMessagePayload(natsMessage *m, const void *data, size_t dataLen);
NATS_EXTERN natsStatus nats_SetMessageReplyTo(natsMessage *m, const char *reply);
NATS_EXTERN natsStatus nats_SetOnMessageCleanup(natsMessage *m, void (*f)(void *), void *closure);
NATS_EXTERN natsStatus nats_SetOnMessagePublished(natsMessage *m, natsOnMessagePublishedF, void *closure);
NATS_EXTERN const uint8_t *nats_GetMessageData(natsMessage *msg);
NATS_EXTERN size_t nats_GetMessageDataLen(natsMessage *msg);

    NATS_EXTERN void nats_ReleaseMessage(natsMessage *msg);

#endif /* NATS_MESSAGE_H_ */
