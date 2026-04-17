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

#ifndef _WIN32
#include <dirent.h>
#include <execinfo.h>
#endif

#include "../src/natsp.h"
#include "../src/comsock.h"

#define test(s)         { printf("#%02d ", ++tests); printf("%s", (s)); fflush(stdout); }
#define testf(s, ...)   { printf("#%02d ", ++tests); printf((s), __VA_ARGS__); fflush(stdout); }

#ifdef _WIN32
#define testCond(c)         if(c) { printf("PASSED\n"); fflush(stdout); } else { printf("FAILED\n"); nats_PrintLastErrorStack(stdout); fflush(stdout); failed=true; return; }
#define testCondNoReturn(c) if(c) { printf("PASSED\n"); fflush(stdout); } else { printf("FAILED\n"); nats_PrintLastErrorStack(stdout); fflush(stdout); failed=true; }
#else
#define testCond(c)         if(c) { printf("\033[0;32mPASSED\033[0;0m\n"); fflush(stdout); } else { printf("\033[0;31mFAILED\033[0;0m\n"); nats_PrintLastErrorStack(stdout); fflush(stdout); failed=true; return; }
#define testCondNoReturn(c) if(c) { printf("\033[0;32mPASSED\033[0;0m\n"); fflush(stdout); } else { printf("\033[0;31mFAILED\033[0;0m\n"); nats_PrintLastErrorStack(stdout); fflush(stdout); failed=true; }
#endif

#if defined(NATS_HAS_STREAMING)
static const char *clusterName = "test-cluster";
#endif

#ifdef _WIN32
#define NATS_INVALID_PID (NULL)
#define LOGFILE_NAME "wserver.log"
#else
#define NATS_INVALID_PID (-1)
#define LOGFILE_NAME "server.log"
#endif

#define FAIL(m)                    \
    {                              \
        printf("@@ %s @@\n", (m)); \
        failed = true;             \
        return;                    \
    }

#define FAILf(f, ...)              \
    {                              \
        printf("@@ ");             \
        printf((f), __VA_ARGS__);  \
        printf(" @@\n");           \
        failed = true;             \
        return;                    \
    }

#define CHECK_SERVER_STARTED(p)  \
    if ((p) == NATS_INVALID_PID) \
    FAIL("Unable to start or verify that the server was started!")

extern natsMutex *slMu;
extern natsHash *slMap;
extern bool keepServerOutput;
extern bool failed;

static const char *natsServerExe = "nats-server";

static natsStatus
_checkStreamingStart(const char *url, int maxAttempts)
{
    natsStatus s = NATS_NOT_PERMITTED;

#if defined(NATS_HAS_STREAMING)

    stanConnOptions *opts = NULL;
    stanConnection *sc = NULL;
    int attempts = 0;

    s = stanConnOptions_Create(&opts);
    IFOK(s, stanConnOptions_SetURL(opts, url));
    IFOK(s, stanConnOptions_SetConnectionWait(opts, 250));
    if (s == NATS_OK)
    {
        while (((s = stanConnection_Connect(&sc, clusterName, "checkStart", opts)) != NATS_OK) && (attempts++ < maxAttempts))
        {
            nats_Sleep(200);
        }
    }

    stanConnection_Destroy(sc);
    stanConnOptions_Destroy(opts);

    if (s != NATS_OK)
        nats_clearLastError();
#else
#endif
    return s;
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

    natsMutex_Lock(slMu);
    if (slMap != NULL)
        natsHash_Remove(slMap, (int64_t)pid);
    natsMutex_Unlock(slMu);

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

    natsMutex_Lock(slMu);
    if (slMap != NULL)
        natsHash_Set(slMap, (int64_t)pid, NULL, NULL);
    natsMutex_Unlock(slMu);

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

    natsMutex_Lock(slMu);
    if (slMap != NULL)
        natsHash_Remove(slMap, (int64_t)pid);
    natsMutex_Unlock(slMu);
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

        ret = nats_asprintf(&exeAndCmdLine, "%s%s%s%s%s", serverExe,
                            (cmdLineOpts != NULL ? " " : ""),
                            (cmdLineOpts != NULL ? cmdLineOpts : ""),
                            (overrideAddr ? " -a 127.0.0.1" : ""),
                            (keepServerOutput ? "" : " -l " LOGFILE_NAME));
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

    natsMutex_Lock(slMu);
    if (slMap != NULL)
        natsHash_Set(slMap, (int64_t)pid, NULL, NULL);
    natsMutex_Unlock(slMu);

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
_makeUniqueDir(char *buf, int bufLen, const char *path)
{
    int n;

    if ((int) strlen(path) + 1 + NUID_BUFFER_LEN + 1 > bufLen)
        abort();

    n = snprintf(buf, bufLen, "%s", path);
    natsNUID_Next(buf+n, NUID_BUFFER_LEN+1);
    buf[n+NUID_BUFFER_LEN+1] = '\0';
}

static void
rmtree(const char *path)
{
#ifdef _WIN32
    WIN32_FIND_DATA ffd;
    HANDLE          hFind = INVALID_HANDLE_VALUE;
    char            *dir  = NULL;

    if (nats_asprintf(&dir, "%s\\*", path) < 0)
        abort();

    hFind = FindFirstFile(dir, &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        free(dir);
        return;
    }

    do
    {
        char *fullPath = NULL;

        if (!strcmp(ffd.cFileName, ".") || !strcmp(ffd.cFileName, ".."))
            continue;

        if (nats_asprintf(&fullPath, "%s\\%s", path, ffd.cFileName) < 0)
            abort();

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            rmtree(fullPath);
        else
            DeleteFile(fullPath);
        free(fullPath);
    }
    while (FindNextFile(hFind, &ffd) != 0);

    FindClose(hFind);
    RemoveDirectory(path);
    free(dir);

#else
    DIR             *dir = NULL;
    int             dfd  = -1;
    struct dirent   *entry;

    dfd = open(path, O_RDONLY | O_DIRECTORY);
    if (dfd < 0)
        return;

    dir = fdopendir(dfd);
    if (dir == NULL)
    {
        close(dfd);
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        if (unlinkat(dfd, entry->d_name, 0) == 0)
            continue;

        if ((errno == EISDIR) || (errno == EPERM))
        {
            char *fullPath = NULL;

            if (nats_asprintf(&fullPath, "%s/%s", path, entry->d_name) < 0)
                abort();

            rmtree(fullPath);
            free(fullPath);
            unlinkat(dfd, entry->d_name, AT_REMOVEDIR);
        }
    }

    closedir(dir);
    rmdir(path);
#endif
}
