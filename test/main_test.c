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

#include <execinfo.h>

#include "natsp.h"
#include "test.h"

int __tests = 0;
bool __failed = false;
char __namebuf[1024];

struct test_s
{
    const char *name;
    void (*f)(void);
};

#define _TEST_PROTO
#include "all_tests.h"
#undef _TEST_PROTO

#define _TEST_LIST
struct test_s allTests[] =
{
#include "all_tests.h"
};
#undef _TEST_LIST

#ifndef _WIN32
static void _sigsegv_handler(int sig)
{
    void *array[20];
    int size = backtrace(array, 20);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}
#endif // _WIN32

int main(int argc, char **argv)
{
    // const char *envStr;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s [testname]\n", argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0)
    {
        for (int i = 0; i < (int) (sizeof(allTests) / sizeof(struct test_s)); i++)
            printf("%s\n", allTests[i].name);

        return 0;
    }

    const char *testname = argv[1];

#ifndef _WIN32
    // signal(SIGSEGV, _sigsegv_handler);
#endif // _WIN32

    if (nats_open() != NATS_OK)
    {
        printf("@@ Unable to run tests: unable to initialize the library!\n");
        return 1;
    }

    // Execute tests
    for (int i = 0;i < (int)(sizeof(allTests) / sizeof(struct test_s)); i++)
    {
        if (strcmp(allTests[i].name, testname) != 0)
            continue;

#ifdef _WIN32
        printf("\n== %s ==\n", allTests[i].name);
#else
        printf("\033[0;34m\n== %s ==\n\033[0;0m", allTests[i].name);
#endif
        (*(allTests[i].f))();
    }

#ifdef _WIN32
    if (logHandle != NULL)
    {
        CloseHandle(logHandle);
        DeleteFile(LOGFILE_NAME);
    }
#else
    // remove(LOGFILE_NAME);
#endif

    // // Shutdown servers that are still running likely due to failed test
    // {
    //     natsHash *pids = NULL;
    //     natsHashIter iter;
    //     int64_t key;

    //     if (natsHash_Create(&pids, 16) == NATS_OK)
    //     {
    //         natsMutex_Lock(slMu);
    //         natsHashIter_Init(&iter, slMap);
    //         while (natsHashIter_Next(&iter, &key, NULL))
    //         {
    //             natsHash_Set(pids, key, NULL, NULL);
    //             natsHashIter_RemoveCurrent(&iter);
    //         }
    //         natsHashIter_Done(&iter);
    //         natsHash_Destroy(slMap);
    //         slMap = NULL;
    //         natsMutex_Unlock(slMu);

    //         natsHashIter_Init(&iter, pids);
    //         while (natsHashIter_Next(&iter, &key, NULL))
    //             _stopServer((natsPid)key);

    //         natsHash_Destroy(pids);
    //     }
    //     else
    //     {
    //         natsHash_Destroy(slMap);
    //     }
    //     natsMutex_Destroy(slMu);
    // }

    // // Makes valgrind happy
    // nats_CloseAndWait((failed ? 1 : 2000));

    if (__failed)
    {
        printf("*** TEST FAILED ***\n");
        return 1;
    }

    printf("ALL PASSED\n");
    return 0;
}
