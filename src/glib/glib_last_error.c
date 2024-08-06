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

static natsTLError*
_getThreadError(void)
{
    natsLib     *lib     = nats_lib();
    natsTLError *errTL   = NULL;
    bool        needFree = false;

    // The library should already be initialized, but let's protect against
    // situations where foo() invokes bar(), which invokes baz(), which
    // invokes nats_Open(). If that last call fails, when we un-wind down
    // to foo(), it may be difficult to know that nats_Open() failed and
    // that we should not try to invoke natsLib_setError. So we check again
    // here that the library has been initialized properly, and if not, we
    // simply don't set the error.
    if (nats_Open(-1) != NATS_OK)
        return NULL;

    errTL = natsThreadLocal_Get(lib->errTLKey);
    if (errTL == NULL)
    {
        errTL = (natsTLError*) NATS_CALLOC(1, sizeof(natsTLError));
        if (errTL != NULL)
            errTL->framesCount = -1;
        needFree = (errTL != NULL);

    }

    if ((errTL != NULL)
        && (natsThreadLocal_SetEx(lib->errTLKey,
                                  (const void*) errTL, false) != NATS_OK))
    {
        if (needFree)
            NATS_FREE(errTL);

        errTL = NULL;
    }

    return errTL;
}

static char*
_getErrorShortFileName(const char* fileName)
{
    char *file = strstr(fileName, "src");

    if (file != NULL)
        file = (file + 4);
    else
        file = (char*) fileName;

    return file;
}

static void
_updateStack(natsTLError *errTL, const char *funcName, natsStatus errSts,
             bool calledFromSetError)
{
    int idx;

    idx = errTL->framesCount;
    if ((idx >= 0)
        && (idx < MAX_FRAMES)
        && (strcmp(errTL->func[idx], funcName) == 0))
    {
        return;
    }

    // In case no error was already set...
    if ((errTL->framesCount == -1) && !calledFromSetError)
        errTL->sts = errSts;

    idx = ++(errTL->framesCount);

    if (idx >= MAX_FRAMES)
        return;

    errTL->func[idx] = funcName;
}

#if !defined(_WIN32)
__attribute__ ((format (printf, 5, 6)))
#endif
natsStatus
nats_setErrorReal(const char *fileName, const char *funcName, int line, natsStatus errSts, const char *errTxtFmt, ...)
{
    natsTLError *errTL  = _getThreadError();
    char        tmp[256];
    va_list     ap;
    int         n;

    if ((errTL == NULL) || errTL->skipUpdate)
        return errSts;

    errTL->sts = errSts;
    errTL->framesCount = -1;

    tmp[0] = '\0';

    va_start(ap, errTxtFmt);
    nats_vsnprintf(tmp, sizeof(tmp), errTxtFmt, ap);
    va_end(ap);

    if (strlen(tmp) > 0)
    {
        n = snprintf(errTL->text, sizeof(errTL->text), "(%s:%d): %s",
                     _getErrorShortFileName(fileName), line, tmp);
        if ((n < 0) || (n >= (int) sizeof(errTL->text)))
        {
            int pos = ((int) strlen(errTL->text)) - 1;
            int i;

            for (i=0; i<3; i++)
                errTL->text[pos--] = '.';
        }
    }

    _updateStack(errTL, funcName, errSts, true);

    return errSts;
}

#if !defined(_WIN32)
__attribute__ ((format (printf, 4, 5)))
#endif
void
nats_updateErrTxt(const char *fileName, const char *funcName, int line, const char *errTxtFmt, ...)
{
    natsTLError *errTL  = _getThreadError();
    char        tmp[256];
    va_list     ap;
    int         n;

    if ((errTL == NULL) || errTL->skipUpdate)
        return;

    tmp[0] = '\0';

    va_start(ap, errTxtFmt);
    nats_vsnprintf(tmp, sizeof(tmp), errTxtFmt, ap);
    va_end(ap);

    if (strlen(tmp) > 0)
    {
        n = snprintf(errTL->text, sizeof(errTL->text), "(%s:%d): %s",
                     _getErrorShortFileName(fileName), line, tmp);
        if ((n < 0) || (n >= (int) sizeof(errTL->text)))
        {
            int pos = ((int) strlen(errTL->text)) - 1;
            int i;

            for (i=0; i<3; i++)
                errTL->text[pos--] = '.';
        }
    }
}

void
nats_setErrStatusAndTxt(natsStatus err, const char *errTxt)
{
    natsTLError *errTL  = _getThreadError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return;

    errTL->sts         = err;
    snprintf(errTL->text, sizeof(errTL->text), "%s", errTxt);
    errTL->framesCount = -1;
}

natsStatus
nats_updateErrStack(natsStatus err, const char *func)
{
    natsTLError *errTL = _getThreadError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return err;

    _updateStack(errTL, func, err, false);

    return err;
}

void
nats_clearLastError(void)
{
    natsTLError *errTL  = _getThreadError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return;

    errTL->sts         = NATS_OK;
    errTL->text[0]     = '\0';
    errTL->framesCount = -1;
}

void
nats_doNotUpdateErrStack(bool skipStackUpdate)
{
    natsTLError *errTL  = _getThreadError();

    if (errTL == NULL)
        return;

    if (skipStackUpdate)
    {
        errTL->skipUpdate++;
    }
    else
    {
        errTL->skipUpdate--;
        assert(errTL->skipUpdate >= 0);
    }
}

const char*
nats_GetLastError(natsStatus *status)
{
    natsStatus  s;
    natsLib     *lib    = nats_lib();
    natsTLError *errTL  = NULL;

    if (status != NULL)
        *status = NATS_OK;

    // Ensure the library is loaded
    s = nats_Open(-1);
    if (s != NATS_OK)
        return NULL;

    errTL = natsThreadLocal_Get(lib->errTLKey);
    if ((errTL == NULL) || (errTL->sts == NATS_OK))
        return NULL;

    if (status != NULL)
        *status = errTL->sts;

    return errTL->text;
}

natsStatus
nats_GetLastErrorStack(char *buffer, size_t bufLen)
{
    natsLib     *lib    = nats_lib();
    natsTLError *errTL  = NULL;
    int         offset  = 0;
    int         i, max, n, len;

    if ((buffer == NULL) || (bufLen == 0))
        return NATS_INVALID_ARG;

    buffer[0] = '\0';
    len = (int) bufLen;

    // Ensure the library is loaded
    if (nats_Open(-1) != NATS_OK)
        return NATS_FAILED_TO_INITIALIZE;

    errTL = natsThreadLocal_Get(lib->errTLKey);
    if ((errTL == NULL) || (errTL->sts == NATS_OK) || (errTL->framesCount == -1))
        return NATS_OK;

    max = errTL->framesCount;
    if (max >= MAX_FRAMES)
        max = MAX_FRAMES - 1;

    for (i=0; (i<=max) && (len > 0); i++)
    {
        n = snprintf(buffer + offset, len, "%s%s",
                     errTL->func[i],
                     (i < max ? "\n" : ""));
        // On Windows, n will be < 0 if len is not big enough.
        if (n < 0)
        {
            len = 0;
        }
        else
        {
            offset += n;
            len    -= n;
        }
    }

    if ((max != errTL->framesCount) && (len > 0))
    {
        n = snprintf(buffer + offset, len, "\n%d more...",
                     errTL->framesCount - max);
        // On Windows, n will be < 0 if len is not big enough.
        if (n < 0)
            len = 0;
        else
            len -= n;
    }

    if (len <= 0)
        return NATS_INSUFFICIENT_BUFFER;

    return NATS_OK;
}

void
nats_PrintLastErrorStack(FILE *file)
{
    natsLib     *lib    = nats_lib();
    natsTLError *errTL  = NULL;
    int i, max;

    // Ensure the library is loaded
    if (nats_Open(-1) != NATS_OK)
        return;

    errTL = natsThreadLocal_Get(lib->errTLKey);
    if ((errTL == NULL) || (errTL->sts == NATS_OK) || (errTL->framesCount == -1))
        return;

    fprintf(file, "Error: %u - %s",
            errTL->sts, natsStatus_GetText(errTL->sts));
    if (errTL->text[0] != '\0')
        fprintf(file, " - %s", errTL->text);
    fprintf(file, "\n");
    fprintf(file, "Stack: (library version: %s)\n", nats_GetVersion());

    max = errTL->framesCount;
    if (max >= MAX_FRAMES)
        max = MAX_FRAMES - 1;

    for (i=0; i<=max; i++)
        fprintf(file, "  %02d - %s\n", (i+1), errTL->func[i]);

    if (max != errTL->framesCount)
        fprintf(file, " %d more...\n", errTL->framesCount - max);

    fflush(file);
}

