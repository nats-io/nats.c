// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef ERR_H_
#define ERR_H_

#include "status.h"
#include "nats.h"

#define NATS_SSL_ERR_REASON_STRING ERR_reason_error_string(ERR_get_error())

#define nats_setDefaultError(e) nats_setError((e), "%s", natsStatus_GetText(e))

#define nats_setError(e, f, ...) nats_setErrorReal(__FILE__, __FUNCTION__, __LINE__, (e), (f), __VA_ARGS__)

natsStatus
nats_setErrorReal(const char *fileName, const char *funcName, int line, natsStatus errSts, const void *errTxtFmt, ...);

#define NATS_UPDATE_ERR_STACK(s) (s == NATS_OK ? s : nats_updateErrStack(s, __FUNCTION__))

natsStatus
nats_updateErrStack(natsStatus err, const char *func);

void
nats_clearLastError(void);

void
nats_doNotUpdateErrStack(bool skipStackUpdate);

#endif /* ERR_H_ */
