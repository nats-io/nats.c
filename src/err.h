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


#ifndef ERR_H_
#define ERR_H_

#include "status.h"
#include "nats.h"
#include "natsp.h"

#define NATS_SSL_ERR_REASON_STRING ERR_reason_error_string(ERR_get_error())

#define nats_setDefaultError(e) nats_setError((e), "%s", natsStatus_GetText(e))

#define nats_setError(e, f, ...) nats_setErrorReal(__FILE__, __NATS_FUNCTION__, __LINE__, (e), (f), __VA_ARGS__)

natsStatus
nats_setErrorReal(const char *fileName, const char *funcName, int line, natsStatus errSts, const void *errTxtFmt, ...);

#define NATS_UPDATE_ERR_STACK(s) (s == NATS_OK ? s : nats_updateErrStack(s, __NATS_FUNCTION__))

natsStatus
nats_updateErrStack(natsStatus err, const char *func);

void
nats_clearLastError(void);

void
nats_doNotUpdateErrStack(bool skipStackUpdate);

#define NATS_UPDATE_ERR_TXT(f, ...) nats_updateErrTxt(__FILE__, __NATS_FUNCTION__, __LINE__, (f), __VA_ARGS__)

void
nats_updateErrTxt(const char *fileName, const char *funcName, int line, const void *errTxtFmt, ...);

#endif /* ERR_H_ */
