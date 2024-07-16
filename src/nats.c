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

#include "natsp.h"
#include "nats/adapters/malloc_heap.h"

#include <stdarg.h>

#include "nuid.h"
#include "hash.h"
#include "crypto.h"

int nats_devmode_log_level = DEV_MODE_DEFAULT_LOG_LEVEL;

#define MAX_FRAMES (50)

typedef struct natsTLError
{
    natsStatus sts;
    char text[256];
    const char *func[MAX_FRAMES];
    int framesCount;
    int skipUpdate;

} natsTLError;

typedef struct __natsLib
{
    natsPool *pool;
    natsHeap *heap;

    bool sslInitialized;
    bool initialized;
    bool closed;
    int refs;

    // uses `microService*` as the key and the value.
    natsHash *all_services_to_callback;

} natsLib;

static natsLib gLib;

#if defined(_WIN32) && _WIN32
#ifndef NATS_STATIC
BOOL WINAPI DllMain(HINSTANCE hinstDLL, // DLL module handle
                    DWORD fdwReason,    // reason called
                    LPVOID lpvReserved) // reserved
{
    switch (fdwReason)
    {
    // For applications linking dynamically NATS library,
    // release thread-local memory for user-created threads.
    // For portable applications, the user should manually call
    // nats_ReleaseThreadMemory() before the thread returns so
    // that no memory is leaked regardless if they link statically
    // or dynamically. It is safe to call nats_ReleaseThreadMemory()
    // twice for the same threads.
    case DLL_THREAD_DETACH:
    {
        nats_ReleaseThreadMemory();
        break;
    }
    default:
        break;
    }

    return TRUE;
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(lpvReserved);
}
#endif
#endif

static void
_freeLib(void)
{
    const unsigned int offset = (unsigned int)offsetof(natsLib, refs);

    natsNUID_free();
    nats_releasePool(gLib.pool);
    gLib.heap->destroy(gLib.heap);

    memset((void *)(offset + (char *)&gLib), 0, sizeof(natsLib) - offset);
}

void nats_retainGlobalLib(void)
{
    gLib.refs++;
}

void nats_Shutdown(void)
{
    if (gLib.closed)
        return;
    gLib.closed = true;

    nats_releaseGlobalLib();
}

void nats_releaseGlobalLib(void)
{
    int refs = 0;

    refs = --(gLib.refs);

    if (refs == 0)
        _freeLib();
}

natsPool *nats_globalPool(void)
{
    return gLib.pool;
}

natsHeap *nats_globalHeap(void)
{
    return gLib.heap;
}

natsStatus
nats_open(void)
{
    natsStatus s = NATS_OK;

    if (gLib.initialized)
    {
        return NATS_OK;
    }

    memset(&gLib, 0, sizeof(natsLib));
    gLib.refs = 1;

    nats_sysInit();
    nats_Base32_Init();

    // Initialize the heap
    s = CHECK_NO_MEMORY(gLib.heap = nats_NewMallocHeap());

    IFOK(s, nats_createPool(&(gLib.pool), &nats_defaultMemOptions, "global"));
    IFOK(s, natsCrypto_Init());
    IFOK(s, natsNUID_init());
    // IFOK(s, natsHash_Create(&gLib.all_services_to_callback, NULL, 8));

    if (STILL_OK(s))
        gLib.initialized = true;
    else
        _freeLib();
    return s;
}

// natsStatus
// natsInbox_Create(natsInbox **newInbox)
// {
//     natsStatus s;
//     char *inbox = NULL;
//     const int size = NATS_DEFAULT_INBOX_PRE_LEN + NUID_BUFFER_LEN + 1;

//     s = nats_open();
//     if (s != NATS_OK)
//         return s;

//     inbox = NATS_MALLOC(size);
//     if (inbox == NULL)
//         return nats_setDefaultError(NATS_NO_MEMORY);

//     memcpy(inbox, NATS_DEFAULT_INBOX_PRE, NATS_DEFAULT_INBOX_PRE_LEN);
//     s = natsNUID_Next(inbox + NATS_DEFAULT_INBOX_PRE_LEN, NUID_BUFFER_LEN + 1);
//     if (STILL_OK(s))
//     {
//         inbox[size - 1] = '\0';
//         *newInbox = (natsInbox *)inbox;
//     }
//     else
//         NATS_FREE(inbox);
//     return NATS_UPDATE_ERR_STACK(s);
// }

// void natsInbox_Destroy(natsInbox *inbox)
// {
//     if (inbox == NULL)
//         return;

//     NATS_FREE(inbox);
// }

const char *
nats_GetVersion(void)
{
    return LIB_NATS_VERSION_STRING;
}

uint32_t
nats_GetVersionNumber(void)
{
    return LIB_NATS_VERSION_NUMBER;
}

static void
_versionGetString(char *buffer, size_t bufLen, uint32_t verNumber)
{
    snprintf(buffer, bufLen, "%u.%u.%u",
             ((verNumber >> 16) & 0xF),
             ((verNumber >> 8) & 0xF),
             (verNumber & 0xF));
}

bool nats_CheckCompatibilityImpl(uint32_t headerReqVerNumber, uint32_t headerVerNumber,
                                 const char *headerVerString)
{
    if ((headerVerNumber < LIB_NATS_VERSION_REQUIRED_NUMBER) || (headerReqVerNumber > LIB_NATS_VERSION_NUMBER))
    {
        char reqVerString[10];
        char libReqVerString[10];

        _versionGetString(reqVerString, sizeof(reqVerString), headerReqVerNumber);
        _versionGetString(libReqVerString, sizeof(libReqVerString), NATS_VERSION_REQUIRED_NUMBER);

        printf("Incompatible versions:\n"
               "Header : %s (requires %s)\n"
               "Library: %s (requires %s)\n",
               headerVerString, reqVerString,
               NATS_VERSION_STRING, libReqVerString);
        exit(1);
    }

    return true;
}

static natsTLError globalTLError = {
    .sts = NATS_OK,
    .framesCount = -1,
};

static natsTLError *
_getTLError(void)
{
    return &globalTLError;
}

static char *
_getErrorShortFileName(const char *fileName)
{
    char *file = strstr(fileName, "src");

    if (file != NULL)
        file = (file + 4);
    else
        file = (char *)fileName;

    return file;
}

static void
_updateStack(natsTLError *errTL, const char *funcName, natsStatus errSts,
             bool calledFromSetError)
{
    int idx = errTL->framesCount;

    if ((idx >= 0) && (idx < MAX_FRAMES) && (strcmp(errTL->func[idx], funcName) == 0))
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
__attribute__((format(printf, 5, 6)))
#endif
natsStatus
nats_setErrorReal(const char *fileName, const char *funcName, int line, natsStatus errSts, const char *errTxtFmt, ...)
{
    natsTLError *errTL = _getTLError();
    char tmp[256];
    va_list ap;
    int n;

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
        if ((n < 0) || (n >= (int)sizeof(errTL->text)))
        {
            int pos = ((int)strlen(errTL->text)) - 1;
            int i;

            for (i = 0; i < 3; i++)
                errTL->text[pos--] = '.';
        }
    }

    _updateStack(errTL, funcName, errSts, true);

    return errSts;
}

#if !defined(_WIN32)
__attribute__((format(printf, 4, 5)))
#endif
void
nats_updateErrTxt(const char *fileName, const char *funcName, int line, const char *errTxtFmt, ...)
{
    natsTLError *errTL = _getTLError();
    char tmp[256];
    va_list ap;
    int n;

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
        if ((n < 0) || (n >= (int)sizeof(errTL->text)))
        {
            int pos = ((int)strlen(errTL->text)) - 1;
            int i;

            for (i = 0; i < 3; i++)
                errTL->text[pos--] = '.';
        }
    }
}

void nats_setErrStatusAndTxt(natsStatus err, const char *errTxt)
{
    natsTLError *errTL = _getTLError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return;

    errTL->sts = err;
    snprintf(errTL->text, sizeof(errTL->text), "%s", errTxt);
    errTL->framesCount = -1;
}

natsStatus
nats_updateErrStack(natsStatus err, const char *func)
{
    natsTLError *errTL = _getTLError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return err;

    _updateStack(errTL, func, err, false);

    return err;
}

void nats_clearLastError(void)
{
    natsTLError *errTL = _getTLError();

    if ((errTL == NULL) || errTL->skipUpdate)
        return;

    errTL->sts = NATS_OK;
    errTL->text[0] = '\0';
    errTL->framesCount = -1;
}

void nats_doNotUpdateErrStack(bool skipStackUpdate)
{
    natsTLError *errTL = _getTLError();

    if (errTL == NULL)
        return;

    if (skipStackUpdate)
    {
        errTL->skipUpdate++;
    }
    else
    {
        errTL->skipUpdate--;
    }
}

const char *
nats_GetLastError(natsStatus *status)
{
    natsStatus s;
    natsTLError *errTL = _getTLError();

    if (status != NULL)
        *status = NATS_OK;

    // Ensure the library is loaded
    s = nats_open();
    if (s != NATS_OK)
        return NULL;

    if ((errTL == NULL) || (errTL->sts == NATS_OK))
        return NULL;

    if (status != NULL)
        *status = errTL->sts;

    return errTL->text;
}

natsStatus
nats_GetLastErrorStack(char *buffer, size_t bufLen)
{
    natsTLError *errTL = _getTLError();
    int offset = 0;
    int i, max, n, len;

    if ((buffer == NULL) || (bufLen == 0))
        return NATS_INVALID_ARG;

    buffer[0] = '\0';
    len = (int)bufLen;

    // Ensure the library is loaded
    if (nats_open() != NATS_OK)
        return NATS_FAILED_TO_INITIALIZE;

    if ((errTL == NULL) || (errTL->sts == NATS_OK) || (errTL->framesCount == -1))
        return NATS_OK;

    max = errTL->framesCount;
    if (max >= MAX_FRAMES)
        max = MAX_FRAMES - 1;

    for (i = 0; (i <= max) && (len > 0); i++)
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
            len -= n;
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

void nats_PrintLastErrorStack(FILE *file)
{
    natsTLError *errTL = NULL;
    int i, max;

    // Ensure the library is loaded
    if (nats_open() != NATS_OK)
        return;

    errTL = _getTLError();
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

    for (i = 0; i <= max; i++)
        fprintf(file, "  %02d - %s\n", (i + 1), errTL->func[i]);

    if (max != errTL->framesCount)
        fprintf(file, " %d more...\n", errTL->framesCount - max);

    fflush(file);
}

int64_t
nats_setTargetTime(int64_t timeout)
{
    int64_t target = nats_now() + timeout;
    if (target < 0)
        target = 0x7FFFFFFFFFFFFFFF;
    return target;
}
