// Copyright 2024 The NATS Authors
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

#ifndef __BENCH_H
#define __BENCH_H

#include <limits.h>

#include "natsp.h"
#include "comsock.h"

static const char *natsServerExe = "nats-server";

#define CHECK_SERVER_STARTED(p)                                                    \
    if ((p) == NATS_INVALID_PID)                                                   \
    {                                                                              \
        fprintf(stderr, "Unable to start or verify that the server was started!"); \
        exit(1);                                                                   \
    }

static natsStatus
_checkStart(const char *url, int orderIP, int maxAttempts)
{
    natsStatus s = NATS_OK;
    natsUrl *nUrl = NULL;
    int attempts = 0;
    natsSockCtx ctx;

    natsSock_Init(&ctx);
    ctx.orderIP = orderIP;

    natsDeadline_Init(&(ctx.writeDeadline), 2000);

    s = natsUrl_Create(&nUrl, url);
    if (s == NATS_OK)
    {
        while (((s = natsSock_ConnectTcp(&ctx,
                                         nUrl->host, nUrl->port)) != NATS_OK) &&
               (attempts++ < maxAttempts))
        {
            nats_Sleep(200);
        }

        natsUrl_Destroy(nUrl);

        if (s == NATS_OK)
            natsSock_Close(ctx.fd);
        else
            s = NATS_NO_SERVER;
    }

    nats_clearLastError();

    return s;
}

#ifdef _WIN32
#define NATS_INVALID_PID (NULL)
#define LOGFILE_NAME "wserver.log"
#else
#define NATS_INVALID_PID (-1)
#define LOGFILE_NAME "server.log"
#endif

#ifdef _WIN32

typedef PROCESS_INFORMATION *natsPid;

static HANDLE logHandle = NULL;

static void
_stopServer(natsPid pid)
{
    if (pid == NATS_INVALID_PID)
        return;

    TerminateProcess(pid->hProcess, 0);
    WaitForSingleObject(pid->hProcess, INFINITE);

    CloseHandle(pid->hProcess);
    CloseHandle(pid->hThread);

    free(pid);
}

static natsPid
_startServerImpl(const char *serverExe, const char *url, const char *cmdLineOpts, bool checkStart)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFO si;
    HANDLE h;
    PROCESS_INFORMATION *pid;
    DWORD flags = 0;
    BOOL createdOk = FALSE;
    BOOL hInheritance = FALSE;
    char *exeAndCmdLine = NULL;
    int ret;

    pid = calloc(1, sizeof(PROCESS_INFORMATION));
    if (pid == NULL)
        return NATS_INVALID_PID;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    ret = nats_asprintf(&exeAndCmdLine, "%s%s%s", serverExe,
                        (cmdLineOpts != NULL ? " " : ""),
                        (cmdLineOpts != NULL ? cmdLineOpts : ""));
    if (ret < 0)
    {
        printf("No memory allocating command line string!\n");
        free(pid);
        return NATS_INVALID_PID;
    }

    if (!keepServerOutput)
    {
        ZeroMemory(&sa, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        h = logHandle;
        if (h == NULL)
        {
            h = CreateFile(LOGFILE_NAME,
                           GENERIC_WRITE,
                           FILE_SHARE_WRITE | FILE_SHARE_READ,
                           &sa,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
        }

        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = NULL;
        si.hStdError = h;
        si.hStdOutput = h;

        hInheritance = TRUE;
        flags = CREATE_NO_WINDOW;

        if (logHandle == NULL)
            logHandle = h;
    }

    // Start the child process.
    if (!CreateProcess(NULL,
                       (LPSTR)exeAndCmdLine,
                       NULL,         // Process handle not inheritable
                       NULL,         // Thread handle not inheritable
                       hInheritance, // Set handle inheritance
                       flags,        // Creation flags
                       NULL,         // Use parent's environment block
                       NULL,         // Use parent's starting directory
                       &si,          // Pointer to STARTUPINFO structure
                       pid))         // Pointer to PROCESS_INFORMATION structure
    {

        printf("Unable to start '%s': error (%d).\n",
               exeAndCmdLine, GetLastError());
        free(exeAndCmdLine);
        return NATS_INVALID_PID;
    }

    free(exeAndCmdLine);

    if (checkStart)
    {
        natsStatus s;

        if (strcmp(serverExe, natsServerExe) == 0)
            s = _checkStart(url, 46, 10);
        else
            s = _checkStreamingStart(url, 10);

        if (s != NATS_OK)
        {
            _stopServer(pid);
            return NATS_INVALID_PID;
        }
    }

    return (natsPid)pid;
}

#else

typedef pid_t natsPid;

static void
_stopServer(natsPid pid)
{
    int status = 0;

    if (pid == NATS_INVALID_PID)
        return;

    if (kill(pid, SIGINT) < 0)
    {
        perror("kill with SIGINT");
        if (kill(pid, SIGKILL) < 0)
        {
            perror("kill with SIGKILL");
        }
    }

    waitpid(pid, &status, 0);
}

static natsPid
_startServerImpl(const char *serverExe, const char *url, const char *cmdLineOpts, bool checkStart)
{
    natsPid pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return NATS_INVALID_PID;
    }

    if (pid == 0)
    {
        char *exeAndCmdLine = NULL;
        char *argvPtrs[64];
        char *line = NULL;
        int index = 0;
        int ret = 0;
        bool overrideAddr = false;

        if ((cmdLineOpts == NULL) || (strstr(cmdLineOpts, "-a ") == NULL))
            overrideAddr = true;

        ret = nats_asprintf(&exeAndCmdLine, "%s%s%s%s", serverExe,
                            (cmdLineOpts != NULL ? " " : ""),
                            (cmdLineOpts != NULL ? cmdLineOpts : ""),
                            (overrideAddr ? " -a 127.0.0.1" : ""));
        if (ret < 0)
        {
            perror("No memory allocating command line string!\n");
            exit(1);
        }

        memset(argvPtrs, 0, sizeof(argvPtrs));
        line = exeAndCmdLine;

        while (*line != '\0')
        {
            while ((*line == ' ') || (*line == '\t') || (*line == '\n'))
                *line++ = '\0';

            argvPtrs[index++] = line;
            while ((*line != '\0') && (*line != ' ') && (*line != '\t') && (*line != '\n'))
            {
                line++;
            }
        }
        argvPtrs[index++] = NULL;

        // Child process. Replace with NATS server
        execvp(argvPtrs[0], argvPtrs);
        perror("Exec failed: ");
        exit(1);
    }
    else if (checkStart)
    {
        natsStatus s;

        s = _checkStart(url, 46, 10);
        if (s != NATS_OK)
        {
            _stopServer(pid);
            return NATS_INVALID_PID;
        }
    }

    // parent, return the child's PID back.
    return pid;
}
#endif

static natsPid
_startServer(const char *url, const char *cmdLineOpts, bool checkStart)
{
    return _startServerImpl(natsServerExe, url, cmdLineOpts, checkStart);
}

static void
asyncCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void *closure)
{
    int64_t dropped = 0;
    natsSubscription_GetDropped(sub, (int64_t *)&dropped);
    printf("Async error: sid:%" PRId64 ", dropped:%" PRId64 ": %u - %s\n", sub->sid, dropped, err, natsStatus_GetText(err));
}

#endif // __BENCH_H
