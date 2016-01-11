// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "buf.h"
#include "timer.h"
#include "url.h"
#include "opts.h"
#include "util.h"
#include "hash.h"
#include "conn.h"
#include "sub.h"
#include "msg.h"
#include "stats.h"
#include "comsock.h"

static int tests = 0;
static int fails = 0;

static bool keepServerOutput    = false;
static bool valgrind            = false;

static const char *natsServerExe = "gnatsd";

#define test(s)         { printf("#%02d ", ++tests); printf((s)); fflush(stdout); }
#ifdef _WIN32
#define NATS_INVALID_PID  (NULL)
#define testCond(c)     if(c) { printf("PASSED\n"); fflush(stdout); } else { printf("FAILED\n"); fflush(stdout); fails++; }
#else
#define NATS_INVALID_PID  (-1)
#define testCond(c)     if(c) { printf("\033[0;32mPASSED\033[0;0m\n"); fflush(stdout); } else { printf("\033[0;31mFAILED\033[0;0m\n"); nats_PrintLastErrorStack(stdout); fflush(stdout); fails++; }
#endif
#define FAIL(m)         { printf("@@ %s @@\n", (m)); fails++; return; }
#define IFOK(s, c)      if (s == NATS_OK) { s = (c); }

#define CHECK_SERVER_STARTED(p) if ((p) == NATS_INVALID_PID) FAIL("Unable to start or verify that the server was started!")

static const char *testServers[] = {"nats://localhost:1222",
                                    "nats://localhost:1223",
                                    "nats://localhost:1224",
                                    "nats://localhost:1225",
                                    "nats://localhost:1226",
                                    "nats://localhost:1227",
                                    "nats://localhost:1228"};

struct threadArg
{
    natsMutex       *m;
    natsThread      *t;
    natsCondition   *c;
    natsCondition   *b;
    int             control;
    bool            current;
    int             sum;
    int             timerFired;
    int             timerStopped;
    natsStrHash     *inboxes;
    natsStatus      status;
    const char*     string;
    bool            disconnected;
    int64_t         disconnectedAt[4];
    int64_t         disconnects;
    bool            closed;
    bool            reconnected;
    int64_t         reconnectedAt[4];
    int             reconnects;
    bool            msgReceived;
    bool            done;
    int             results[10];

    natsSubscription *sub;
    natsOptions      *opts;

};

static natsStatus
_createDefaultThreadArgsForCbTests(
    struct threadArg    *arg)
{
    natsStatus s;

    memset(arg, 0, sizeof(struct threadArg));

    s = natsMutex_Create(&(arg->m));
    if (s == NATS_OK)
        s = natsCondition_Create(&(arg->c));

    return s;
}

void
_destroyDefaultThreadArgs(struct threadArg *args)
{
    natsMutex_Destroy(args->m);
    natsCondition_Destroy(args->c);
}

static void
test_natsNowAndSleep(void)
{
    int64_t start;
    int64_t end;

    test("Check now and sleep: ")
    start = nats_Now();
    nats_Sleep(1000);
    end = nats_Now();
    testCond(((end - start) >= 990) && ((end - start) <= 1010));
}

static void
test_natsAllocSprintf(void)
{
    char smallStr[20];
    char mediumStr[256]; // This is the size of the temp buffer in nats_asprintf
    char largeStr[1024];
    char *ptr = NULL;
    int ret;

    memset(smallStr, 'A', sizeof(smallStr) - 1);
    smallStr[sizeof(smallStr) - 1] = '\0';

    memset(mediumStr, 'B', sizeof(mediumStr) - 1);
    mediumStr[sizeof(mediumStr) - 1] = '\0';

    memset(largeStr, 'C', sizeof(largeStr) - 1);
    largeStr[sizeof(largeStr) - 1] = '\0';

    test("Check alloc sprintf with small string: ");
    ret = nats_asprintf(&ptr, "%s", smallStr);
    testCond((ret >= 0)
             && (strcmp(ptr, smallStr) == 0));

    free(ptr);
    ptr = NULL;

    test("Check alloc sprintf with medium string: ");
    ret = nats_asprintf(&ptr, "%s", mediumStr);
    testCond((ret >= 0)
             && (strcmp(ptr, mediumStr) == 0));

    free(ptr);
    ptr = NULL;

    test("Check alloc sprintf with large string: ");
    ret = nats_asprintf(&ptr, "%s", largeStr);
    testCond((ret >= 0)
             && (strcmp(ptr, largeStr) == 0));

    free(ptr);
    ptr = NULL;
}

static void
test_natsStrCaseStr(void)
{
    const char *s1 = "Hello World!";
    const char *s2 = "wo";
    const char *res = NULL;

    test("StrStr case insensitive (equal): ");
    res = nats_strcasestr(s1, s1);
    testCond((res != NULL)
             && (strcmp(res, s1) == 0)
             && (res == s1));

    test("StrStr case insensitive (match): ");
    res = nats_strcasestr(s1, s2);
    testCond((res != NULL)
             && (strcmp(res, "World!") == 0)
             && (res == (s1 + 6)));

    test("StrStr case insensitive (no match): ");
    res = nats_strcasestr(s1, "xx");
    testCond(res == NULL);

}

static void test_natsBuffer(void)
{
    natsStatus  s;
    char        backend[10];
    natsBuffer  *buf = NULL;
    natsBuffer  stackBuf;
    int         oldCapacity = 0;

    printf("== Buffer without data ==\n");

    test("Create buffer owning its data: ");
    s = natsBuf_Create(&buf, 1);
    testCond((s == NATS_OK)
             && (natsBuf_Len(buf) == 0)
             && (natsBuf_Capacity(buf) == 1));

    test("Append less than capacity does not expand buffer: ");
    IFOK(s, natsBuf_Append(buf, "a", 1));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 1)
            && (natsBuf_Capacity(buf) == 1)
            && (natsBuf_Available(buf) == 0));

    test("Appending one more (AppendByte) increases capacity: ");
    if (s == NATS_OK)
    {
        oldCapacity = natsBuf_Capacity(buf);

        s = natsBuf_AppendByte(buf, 'b');
    }
    testCond((s == NATS_OK)
             && (natsBuf_Len(buf) == 2)
             && (natsBuf_Capacity(buf) > oldCapacity)
             && (natsBuf_Available(buf) > 0));

    test("Checking content: ");
    testCond((s == NATS_OK)
             && (natsBuf_Data(buf) != NULL)
             && (strncmp(natsBuf_Data(buf), "ab", 2) == 0));

    natsBuf_Destroy(buf);
    buf = NULL;

    oldCapacity = 0;
    test("Appending one more byte increases capacity: ");
    s = natsBuf_Create(&buf, 1);
    if (s == NATS_OK)
        s = natsBuf_Append(buf, "a", 1);
    if (s == NATS_OK)
    {
        oldCapacity = natsBuf_Capacity(buf);

        // Add one more!
        s = natsBuf_Append(buf, "b", 1);
    }
    testCond((s == NATS_OK)
             && (natsBuf_Len(buf) == 2)
             && (natsBuf_Capacity(buf) > oldCapacity)
             && (natsBuf_Available(buf) > 0));

    natsBuf_Destroy(buf);
    buf = NULL;

    printf("\n== Buffer with data ==\n");

    memset(backend, 0, sizeof(backend));

    test("Create buffer with backend: ");
    s = natsBuf_CreateWithBackend(&buf, backend, 0, 5);
    testCond((s == NATS_OK)
             && (natsBuf_Len(buf) == 0)
             && (natsBuf_Capacity(buf) == 5));

    test("Check that changes are reflected in backend")
    IFOK(s, natsBuf_Append(buf, "abcd", 4));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 4)
            && (natsBuf_Capacity(buf) == 5)
            && (natsBuf_Available(buf) > 0)
            && (strcmp(backend, "abcd") == 0));

    test("Changing backend is reflected in buffer: ");
    testCond((s == NATS_OK)
             && (backend[1] = 'x')
             && (natsBuf_Data(buf)[1] == 'x'));

    test("Append less than capacity does not expand buffer: ");
    IFOK(s, natsBuf_AppendByte(buf, 'e'));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 5)
            && (natsBuf_Capacity(buf) == 5)
            && (natsBuf_Available(buf) == 0));

    test("Check natsBuf_Expand returns error for invalid arguments: ");
    if (s == NATS_OK)
    {
        natsStatus ls;

        ls = natsBuf_Expand(buf, -10);
        if (ls != NATS_OK)
            ls = natsBuf_Expand(buf, 0);
        if (ls != NATS_OK)
            ls = natsBuf_Expand(buf, natsBuf_Capacity(buf));
        testCond(ls != NATS_OK);
    }

    test("Adding more causes expand: ");
    oldCapacity = natsBuf_Capacity(buf);
    IFOK(s, natsBuf_Append(buf, "fghij", 5));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 10)
            && (natsBuf_Capacity(buf) > oldCapacity));

    test("Check that the backend did not change");
    testCond((s == NATS_OK)
             && (strcmp(backend, "axcde") == 0));

    test("Checking content: ");
    testCond((s == NATS_OK)
             && (natsBuf_Data(buf) != NULL)
             && (strncmp(natsBuf_Data(buf), "axcdefghij", 10) == 0));

    test("Destroying buffer does not affect backend: ");
    if (s == NATS_OK)
    {
        natsBuf_Destroy(buf);
        buf = NULL;

        testCond(strcmp(backend, "axcde") == 0);
    }


    printf("\n== Buffer Init without data ==\n");

    test("Create buffer owning its data: ");
    s = natsBuf_Init(&stackBuf, 10);
    testCond((s == NATS_OK)
             && (buf = &stackBuf)
             && (natsBuf_Len(buf) == 0)
             && (natsBuf_Capacity(buf) == 10));

    test("Append less than capacity does not expand buffer: ");
    IFOK(s, natsBuf_Append(buf, "abcdefghij", 10));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 10)
            && (natsBuf_Capacity(buf) == 10)
            && (natsBuf_Available(buf) == 0));

    test("Appending one more increases capacity: ");
    if (s == NATS_OK)
    {
        oldCapacity = natsBuf_Capacity(buf);

        s = natsBuf_AppendByte(buf, 'k');
        testCond((s == NATS_OK)
                 && (natsBuf_Len(buf) == 11)
                 && (natsBuf_Capacity(buf) > oldCapacity)
                 && (natsBuf_Available(buf) > 0));
    }

    test("Checking content: ");
    testCond((s == NATS_OK)
             && (natsBuf_Data(buf) != NULL)
             && (strncmp(natsBuf_Data(buf), "abcdefghijk", 11) == 0));

    test("Destroying buffer: ");
    if (s == NATS_OK)
    {
        natsBuf_Destroy(buf);
        testCond((natsBuf_Data(buf) == NULL)
                 && (natsBuf_Len(buf) == 0)
                 && (natsBuf_Capacity(buf) == 0)
                 && (natsBuf_Available(buf) == 0));
        buf = NULL;
    }

    printf("\n== Buffer Init with data ==\n");

    memset(backend, 0, sizeof(backend));

    test("Create buffer with backend: ");
    s = natsBuf_InitWithBackend(&stackBuf, backend, 0, 5);
    testCond((s == NATS_OK)
             && (buf = &stackBuf)
             && (natsBuf_Len(buf) == 0)
             && (natsBuf_Capacity(buf) == 5));

    test("Check that changes are reflected in backend")
    IFOK(s, natsBuf_Append(buf, "abcd", 4));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 4)
            && (natsBuf_Capacity(buf) == 5)
            && (natsBuf_Available(buf) > 0)
            && (strcmp(backend, "abcd") == 0));

    test("Changing backend is reflected in buffer: ");
    testCond((s == NATS_OK)
             && (backend[1] = 'x')
             && (natsBuf_Data(buf)[1] == 'x'));

    test("Append less than capacity does not expand buffer: ");
    IFOK(s, natsBuf_AppendByte(buf, 'e'));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 5)
            && (natsBuf_Capacity(buf) == 5)
            && (natsBuf_Available(buf) == 0));

    test("Adding more causes expand: ");
    IFOK(s, natsBuf_Append(buf, "fghij", 5));
    testCond((s == NATS_OK)
            && (natsBuf_Len(buf) == 10)
            && (natsBuf_Capacity(buf) >= 10));

    test("Check that the backend did not change");
    testCond((s == NATS_OK)
             && (strcmp(backend, "axcde") == 0));

    test("Checking content: ");
    testCond((s == NATS_OK)
             && (natsBuf_Data(buf) != NULL)
             && (strncmp(natsBuf_Data(buf), "axcdefghij", 10) == 0));

    test("Destroying buffer does not affect backend: ");
    if (s == NATS_OK)
    {
        natsBuf_Destroy(buf);
        testCond(strcmp(backend, "axcde") == 0);
    }

    test("Destroyed buffer state is clean: ");
    testCond((s == NATS_OK)
             && (natsBuf_Data(buf) == NULL)
             && (natsBuf_Len(buf) == 0)
             && (natsBuf_Capacity(buf) == 0)
             && (natsBuf_Available(buf) == 0));

    buf = NULL;

    test("Check maximum size: ");
    s = natsBuf_Create(&buf, 5);
    if (s == NATS_OK)
        s = natsBuf_Append(buf, "abcd", 4);
    if (s == NATS_OK)
        s = natsBuf_Append(buf, "fake size that goes over int max size", 0x7FFFFFFC);
    testCond(s == NATS_NO_MEMORY);

    test("Check maximum size (append byte): ");
    buf->len = 0x7FFFFFFE;
    s = natsBuf_Append(buf, "e", 1);
    testCond(s == NATS_NO_MEMORY);

    natsBuf_Destroy(buf);
}

static void
test_natsParseInt64(void)
{
    int64_t n;

    test("Parse with non numeric: ");
    n = nats_ParseInt64("a", 1);
    testCond(n == -1);

    test("Parse with NULL buffer: ");
    n = nats_ParseInt64(NULL, 0);
    testCond(n == -1);

    test("Parse with 0 buffer size: ");
    n = nats_ParseInt64("whatever", 0);
    testCond(n == -1);

    test("Parse with '1': ");
    n = nats_ParseInt64("1", 1);
    testCond(n == 1);

    test("Parse with '12': ");
    n = nats_ParseInt64("12", 2);
    testCond(n == 12);

    test("Parse with '12345': ");
    n = nats_ParseInt64("12345", 5);
    testCond(n == 12345);

    test("Parse with '123.45': ");
    n = nats_ParseInt64("123.45", 6);
    testCond(n == -1);
}

static void
test_natsParseControl(void)
{
    natsStatus  s;
    natsControl c;

    c.op   = NULL;
    c.args = NULL;

    test("Test with NULL line: ");
    s = nats_ParseControl(&c, NULL);
    testCond(s == NATS_PROTOCOL_ERROR);

    test("Test line with single op: ");
    s = nats_ParseControl(&c, "op");
    testCond((s == NATS_OK)
             && (c.op != NULL)
             && (strcmp(c.op, "op") == 0)
             && (c.args == NULL));

    free(c.op);
    free(c.args);
    c.op = NULL;
    c.args = NULL;

    test("Test line with trailing spaces: ");
    s = nats_ParseControl(&c, "op   ");
    testCond((s == NATS_OK)
             && (c.op != NULL)
             && (strcmp(c.op, "op") == 0)
             && (c.args == NULL));

    free(c.op);
    free(c.args);
    c.op = NULL;
    c.args = NULL;

    test("Test line with op and args: ");
    s = nats_ParseControl(&c, "op    args");
    testCond((s == NATS_OK)
             && (c.op != NULL)
             && (strcmp(c.op, "op") == 0)
             && (c.args != NULL)
             && (strcmp(c.args, "args") == 0));

    free(c.op);
    free(c.args);
    c.op = NULL;
    c.args = NULL;

    test("Test line with op and args and trailing spaces: ");
    s = nats_ParseControl(&c, "op   args  ");
    testCond((s == NATS_OK)
             && (c.op != NULL)
             && (strcmp(c.op, "op") == 0)
             && (c.args != NULL)
             && (strcmp(c.args, "args") == 0));

    free(c.op);
    free(c.args);
    c.op = NULL;
    c.args = NULL;

    test("Test line with op and args args: ");
    s = nats_ParseControl(&c, "op   args  args   ");
    testCond((s == NATS_OK)
             && (c.op != NULL)
             && (strcmp(c.op, "op") == 0)
             && (c.args != NULL)
             && (strcmp(c.args, "args  args") == 0));

    free(c.op);
    free(c.args);
    c.op = NULL;
    c.args = NULL;
}

static void
test_natsMutex(void)
{
    natsStatus  s;
    natsMutex   *m = NULL;
    bool        locked = false;

    test("Create mutex: ");
    s = natsMutex_Create(&m);
    testCond(s == NATS_OK);

    test("Lock: ");
    natsMutex_Lock(m);
    testCond(1);

    test("Recursive locking: ");
    locked = natsMutex_TryLock(m);
    testCond(locked);

    test("Release recursive lock: ");
    natsMutex_Unlock(m);
    testCond(1);

    test("Unlock: ");
    natsMutex_Unlock(m);
    testCond(1);

    test("Destroy: ");
    natsMutex_Destroy(m);
    testCond(1);
}

static void
testThread(void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->control = 1;
    tArg->current = natsThread_IsCurrent(tArg->t);

    natsMutex_Unlock(tArg->m);
}

static void
sumThread(void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->sum++;

    natsMutex_Unlock(tArg->m);
}

static int NUM_THREADS = 1000;

static void
test_natsThread(void)
{
    natsStatus          s  = NATS_OK;
    natsMutex           *m = NULL;
    natsThread          *t = NULL;
    bool                current = false;
    struct threadArg    tArgs;
    natsThread          **threads = NULL;
    int                 i,j;

    if (valgrind)
        NUM_THREADS = 100;

    threads = (natsThread**) calloc(NUM_THREADS, sizeof(natsThread*));
    if (threads == NULL)
        s = NATS_NO_MEMORY;

    if (s == NATS_OK)
        s = natsMutex_Create(&m);
    if (s != NATS_OK)
        FAIL("Unable to setup test");

    natsMutex_Lock(m);

    tArgs.m         = m;
    tArgs.control   = 0;
    tArgs.current   = false;

    test("Create thread: ");
    s = natsThread_Create(&t, testThread, &tArgs);
    testCond(s == NATS_OK);

    tArgs.t = t;

    test("Check if thread current from other thread: ");
    current = natsThread_IsCurrent(t);
    testCond(!current);

    natsMutex_Unlock(m);

    test("Joining thread: ")
    natsThread_Join(t);
    testCond(1);

    natsMutex_Lock(m);

    test("Control updated: ");
    testCond(tArgs.control == 1);

    test("Check thread current works from current thread: ");
    testCond(tArgs.current);

    test("Destroy thread: ");
    natsThread_Destroy(t);
    testCond(1);

    tArgs.sum = 0;

    test("Creating multiple threads: ");
    for (i=0; (s == NATS_OK) && (i<NUM_THREADS); i++)
        s = natsThread_Create(&(threads[i]), sumThread, &tArgs);
    testCond(s == NATS_OK);

    if (s != NATS_OK)
        i--;

    natsMutex_Unlock(m);

    test("Waiting all done: ");
    for (j=0; j<i; j++)
    {
        natsThread_Join(threads[j]);
        natsThread_Destroy(threads[j]);
    }
    testCond(s == NATS_OK);

    test("Checking sum: ");
    testCond((s == NATS_OK) && (tArgs.sum == NUM_THREADS));

    natsMutex_Destroy(m);

    free(threads);
}

static void
testSignal(void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->control = 1;
    natsCondition_Signal(tArg->c);

    natsMutex_Unlock(tArg->m);
}

static void
testBroadcast(void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->sum++;
    natsCondition_Signal(tArg->c);

    while (tArg->control == 0)
        natsCondition_Wait(tArg->b, tArg->m);

    tArg->sum--;

    natsMutex_Unlock(tArg->m);
}

static void
test_natsCondition(void)
{
    natsStatus          s;
    natsMutex           *m  = NULL;
    natsThread          *t1 = NULL;
    natsThread          *t2 = NULL;
    natsCondition       *c1 = NULL;
    natsCondition       *c2 = NULL;
    struct threadArg    tArgs;
    int64_t             before = 0;
    int64_t             diff   = 0;
    int64_t             target = 0;

    s = natsMutex_Create(&m);
    if (s != NATS_OK)
        FAIL("Unable to run test_natsCondition because got an error while creating mutex!");

    test("Create condition variables: ");
    s = natsCondition_Create(&c1);
    if (s == NATS_OK)
        s = natsCondition_Create(&c2);
    testCond(s == NATS_OK);

    natsMutex_Lock(m);

    tArgs.m       = m;
    tArgs.c       = c1;
    tArgs.control = 0;

    s = natsThread_Create(&t1, testSignal, &tArgs);
    if (s != NATS_OK)
    {
        natsMutex_Unlock(m);
        FAIL("Unable to run test_natsCondition because got an error while creating thread!");
    }

    test("Wait for signal: ");
    while (tArgs.control != 1)
        natsCondition_Wait(c1, m);

    natsThread_Join(t1);
    natsThread_Destroy(t1);
    t1 = NULL;
    testCond(tArgs.control == 1);

    test("Wait timeout: ");
    before = nats_Now();
    s = natsCondition_TimedWait(c1, m, 1000);
    diff = (nats_Now() - before);
    testCond((s == NATS_TIMEOUT)
             && (diff >= 985) && (diff <= 1015));

    test("Wait timeout with 0: ");
    before = nats_Now();
    s = natsCondition_TimedWait(c1, m, 0);
    diff = (nats_Now() - before);
    testCond((s == NATS_TIMEOUT)
             && (diff >= 0) && (diff <= 10));

    test("Wait timeout with negative: ");
    before = nats_Now();
    s = natsCondition_TimedWait(c1, m, -10);
    diff = (nats_Now() - before);
    testCond((s == NATS_TIMEOUT)
             && (diff >= 0) && (diff <= 10));

    test("Wait absolute time: ");
    before = nats_Now();
    target = nats_Now() + 1000;
    s = natsCondition_AbsoluteTimedWait(c1, m, target);
    diff = (nats_Now() - before);
    testCond((s == NATS_TIMEOUT)
             && (diff >= 985) && (diff <= 1015));

    test("Wait absolute time in the past: ");
    before = nats_Now();
    target = nats_Now() - 1000;
    s = natsCondition_AbsoluteTimedWait(c1, m, target);
    diff = (nats_Now() - before);
    testCond((s == NATS_TIMEOUT)
             && (diff >= 0) && (diff <= 10));

    test("Signal before wait: ");
    tArgs.control = 0;

    s = natsThread_Create(&t1, testSignal, &tArgs);
    if (s != NATS_OK)
    {
        natsMutex_Unlock(m);
        FAIL("Unable to run test_natsCondition because got an error while creating thread!");
    }

    while (tArgs.control == 0)
    {
        natsMutex_Unlock(m);
        nats_Sleep(1000);
        natsMutex_Lock(m);
    }

    s = natsCondition_TimedWait(c1, m, 1000);
    testCond(s == NATS_TIMEOUT);

    natsThread_Join(t1);
    natsThread_Destroy(t1);
    t1 = NULL;

    test("Broadcast: ");
    tArgs.control = 0;
    tArgs.sum     = 0;
    tArgs.b       = c2;

    s = natsThread_Create(&t1, testBroadcast, &tArgs);
    if (s == NATS_OK)
        s = natsThread_Create(&t2, testBroadcast, &tArgs);
    if (s != NATS_OK)
    {
        natsMutex_Unlock(m);
        FAIL("Unable to run test_natsCondition because got an error while creating thread!");
    }

    while (tArgs.sum != 2)
        natsCondition_Wait(c1, m);

    natsMutex_Unlock(m);

    nats_Sleep(1000);

    natsMutex_Lock(m);

    tArgs.control = 1;
    natsCondition_Broadcast(c2);

    natsMutex_Unlock(m);

    natsThread_Join(t1);
    natsThread_Destroy(t1);
    t1 = NULL;

    natsThread_Join(t2);
    natsThread_Destroy(t2);
    t2 = NULL;

    testCond(tArgs.sum == 0);

    test("Destroy condition: ");
    natsCondition_Destroy(c1);
    natsCondition_Destroy(c2);
    testCond(1);

    natsMutex_Destroy(m);
}

static void
testTimerCb(natsTimer *timer, void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->timerFired++;
    natsCondition_Signal(tArg->c);

    natsMutex_Unlock(tArg->m);

    if (tArg->control == 1)
        natsTimer_Reset(timer, 500);
    else if (tArg->control == 2)
        natsTimer_Stop(timer);
    else if (tArg->control == 3)
        nats_Sleep(500);

    natsMutex_Lock(tArg->m);

    natsCondition_Signal(tArg->c);

    natsMutex_Unlock(tArg->m);
}

static void
stopTimerCb(natsTimer *timer, void *arg)
{
    struct threadArg *tArg = (struct threadArg*) arg;

    natsMutex_Lock(tArg->m);

    tArg->timerStopped++;
    natsCondition_Signal(tArg->c);

    natsMutex_Unlock(tArg->m);
}

#define STOP_TIMER_AND_WAIT_STOPPED \
        natsTimer_Stop(t); \
        natsMutex_Lock(tArg.m); \
        while (tArg.timerStopped == 0) \
            natsCondition_Wait(tArg.c, tArg.m); \
        natsMutex_Unlock(tArg.m)

static void
test_natsTimer(void)
{
    natsStatus          s;
    natsTimer           *t = NULL;
    struct threadArg    tArg;

    s = _createDefaultThreadArgsForCbTests(&tArg);
    if (s != NATS_OK)
        FAIL("Unable to setup natsTimer test!");

    tArg.control      = 0;
    tArg.timerFired   = 0;
    tArg.timerStopped = 0;

    test("Create timer: ");
    s = natsTimer_Create(&t, testTimerCb, stopTimerCb, 400, &tArg);
    testCond(s == NATS_OK);

    test("Stop timer: ");
    tArg.control = 0;
    natsTimer_Stop(t);
    nats_Sleep(600);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired == 0)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    test("Firing of timer: ")
    tArg.control      = 0;
    tArg.timerStopped = 0;
    natsTimer_Reset(t, 200);
    nats_Sleep(1100);
    natsTimer_Stop(t);
    nats_Sleep(600);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired > 0)
             && (tArg.timerFired <= 5)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    test("Stop stopped timer: ");
    tArg.control      = 0;
    tArg.timerFired   = 0;
    tArg.timerStopped = 0;
    natsTimer_Reset(t, 100);
    nats_Sleep(300);
    natsTimer_Stop(t);
    nats_Sleep(100);
    natsTimer_Stop(t);
    nats_Sleep(100);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired > 0)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    tArg.control      = 1;
    tArg.timerFired   = 0;
    tArg.timerStopped = 0;
    test("Reset from callback: ");
    natsTimer_Reset(t, 250);
    nats_Sleep(900);
    natsTimer_Stop(t);
    nats_Sleep(600);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired == 2)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && nats_getTimersCount() == 0);
    natsMutex_Unlock(tArg.m);

    tArg.control      = 0;
    tArg.timerFired   = 0;
    tArg.timerStopped = 0;
    test("Multiple Reset: ");
    natsTimer_Reset(t, 1000);
    natsTimer_Reset(t, 800);
    natsTimer_Reset(t, 200);
    natsTimer_Reset(t, 500);
    nats_Sleep(600);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired == 1)
             && (tArg.timerStopped == 0)
             && (t->refs == 1)
             && nats_getTimersCount() == 1);
    natsMutex_Unlock(tArg.m);

    STOP_TIMER_AND_WAIT_STOPPED;

    tArg.control      = 3;
    tArg.timerFired   = 0;
    tArg.timerStopped = 0;
    test("Check refs while in callback: ");
    natsTimer_Reset(t, 1);

    // Wait that it is in callback
    natsMutex_Lock(tArg.m);
    while (tArg.timerFired != 1)
        natsCondition_Wait(tArg.c, tArg.m);
    natsMutex_Unlock(tArg.m);

    testCond((t->refs == 2)
             && nats_getTimersCountInList() == 0
             && nats_getTimersCount() == 1);

    STOP_TIMER_AND_WAIT_STOPPED;

    tArg.control        = 2;
    tArg.timerFired     = 0;
    tArg.timerStopped   = 0;
    test("Stop from callback: ");
    natsTimer_Reset(t, 250);
    nats_Sleep(500);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired == 1)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    tArg.control        = 3;
    tArg.timerFired     = 0;
    tArg.timerStopped   = 0;
    test("Slow callback: ");
    natsTimer_Reset(t, 100);
    nats_Sleep(800);
    natsTimer_Stop(t);
    nats_Sleep(500);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired <= 3)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    tArg.control        = 3;
    tArg.timerFired     = 0;
    tArg.timerStopped   = 0;
    test("Stopped while in callback: ");
    natsTimer_Reset(t, 100);
    nats_Sleep(200);
    natsTimer_Stop(t);
    nats_Sleep(700);
    natsMutex_Lock(tArg.m);
    testCond((tArg.timerFired == 1)
             && (tArg.timerStopped == 1)
             && (t->refs == 1)
             && (nats_getTimersCount() == 0));
    natsMutex_Unlock(tArg.m);

    test("Destroy timer: ");
    t->refs++;
    natsTimer_Destroy(t);
    testCond(t->refs == 1);
    natsTimer_Release(t);

    _destroyDefaultThreadArgs(&tArg);
}


#define RANDOM_ITER         (10000)
#define RANDOM_ARRAY_SIZE   (10)

static void
test_natsRandomize(void)
{
    int *array = (int*) calloc(RANDOM_ARRAY_SIZE, sizeof(int));
    int i, j;
    int sameTotal = 0;
    int same;

    if (array == NULL)
        FAIL("@@ Unable to setup test!");

    test("Randomization of array: ");
    for (i = 0; i < RANDOM_ITER; i++)
    {
        for (j = 0; j < RANDOM_ARRAY_SIZE; j++)
            array[j] = j;

        nats_Randomize(array, RANDOM_ARRAY_SIZE);

        same = 0;
        for (j = 0; j < RANDOM_ARRAY_SIZE; j++)
        {
            if (array[j] == j)
                same++;
        }
        if (same == RANDOM_ARRAY_SIZE)
            sameTotal++;
    }
    testCond(sameTotal <= (RANDOM_ITER * 0.1));

    free(array);
}

static void
test_natsUrl(void)
{
    natsStatus  s;
    natsUrl     *u = NULL;

    test("NULL: ");
    s = natsUrl_Create(&u, NULL);
    testCond((s != NATS_OK) && (u == NULL));

    test("EMPTY: ");
    s = natsUrl_Create(&u, NULL);
    testCond((s != NATS_OK) && (u == NULL));

    test("localhost:4222 ");
    s = natsUrl_Create(&u, "localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host == NULL)
              && (u->username == NULL)
              && (u->password == NULL));
    natsUrl_Destroy(u);

    test("tcp:// ");
    s = natsUrl_Create(&u, "tcp://");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host == NULL)
              && (u->username == NULL)
              && (u->password == NULL));
    natsUrl_Destroy(u);

    test("tcp://: ");
    s = natsUrl_Create(&u, "tcp://:");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host == NULL)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 0));
    natsUrl_Destroy(u);

    test("tcp://localhost ");
    s = natsUrl_Create(&u, "tcp://localhost");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 0))
    natsUrl_Destroy(u);

    test("tcp://localhost ");
    s = natsUrl_Create(&u, "tcp://localhost");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 0))
    natsUrl_Destroy(u);

    test("tcp://localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 4222))
    natsUrl_Destroy(u);

    test("tcp://ivan:localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://ivan:localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "ivan:localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://ivan:pwd:localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://ivan:pwd:localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "ivan:pwd:localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://ivan@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://ivan@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username != NULL)
              && (strcmp(u->username, "ivan") == 0)
              && (u->password == NULL)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://ivan:pwd@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://ivan:pwd@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username != NULL)
              && (strcmp(u->username, "ivan") == 0)
              && (u->password != NULL)
              && (strcmp(u->password, "pwd") == 0)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username == NULL)
              && (u->password == NULL)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://@@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://@@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username != NULL)
              && (strcmp(u->username, "@") == 0)
              && (u->password == NULL)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://a:b:c@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://a:b:c@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username != NULL)
              && (strcmp(u->username, "a") == 0)
              && (u->password != NULL)
              && (strcmp(u->password, "b:c") == 0)
              && (u->port == 4222));
    natsUrl_Destroy(u);

    test("tcp://::a:b:c@localhost:4222 ");
    s = natsUrl_Create(&u, "tcp://::a:b:c@localhost:4222");
    testCond((s == NATS_OK)
              && (u != NULL)
              && (u->host != NULL)
              && (strcmp(u->host, "localhost") == 0)
              && (u->username == NULL)
              && (u->password != NULL)
              && (strcmp(u->password, ":a:b:c") == 0)
              && (u->port == 4222));
    natsUrl_Destroy(u);
}

static void
test_natsCreateStringFromBuffer(void)
{
    natsStatus  s = NATS_OK;
    natsBuffer  buf;
    char        *str = NULL;

    test("NULL buffer: ");
    s = nats_CreateStringFromBuffer(&str, NULL);
    testCond((s == NATS_OK)
             && (str == NULL))

    s = natsBuf_Init(&buf, 10);
    if (s != NATS_OK)
        FAIL("Unable to test createStringFromBuffer due to error creating buffer!");

    test("Empty buffer: ");
    s = nats_CreateStringFromBuffer(&str, &buf);
    testCond((s == NATS_OK)
             && (str == NULL))

    s = natsBuf_Append(&buf, "123", 3);
    if (s != NATS_OK)
        FAIL("Unable to test createStringFromBuffer due to error creating buffer!");

    test("Buffer containing '123': ");
    s = nats_CreateStringFromBuffer(&str, &buf);
    testCond((s == NATS_OK)
             && (str != NULL)
             && (strlen(str) == 3)
             && (strcmp(str, "123") == 0));

    test("Destroying the buffer does not affect the created string: ");
    natsBuf_Cleanup(&buf);
    testCond((str != NULL)
             && (strlen(str) == 3)
             && (strcmp(str, "123") == 0));

    free(str);
}

#define INBOX_THREADS_COUNT     (10)
#define INBOX_COUNT_PER_THREAD  (10000)
#define INBOX_TOTAL             (INBOX_THREADS_COUNT * INBOX_COUNT_PER_THREAD)

static void
_testInbox(void *closure)
{
    struct threadArg    *args = (struct threadArg*) closure;
    natsStatus          s     = NATS_OK;
    natsInbox           *inbox, *oldInbox;

    for (int i=0; (s == NATS_OK) && (i<INBOX_COUNT_PER_THREAD); i++)
    {
        inbox    = NULL;
        oldInbox = NULL;

        s = natsInbox_Create(&inbox);
        if (s == NATS_OK)
            s = natsStrHash_Set(args->inboxes, inbox, true, (void*) 1, (void**) &oldInbox);
        if ((s == NATS_OK) && (oldInbox != NULL))
        {
            printf("Duplicate inbox: %s\n", oldInbox);
            s = NATS_ERR;
        }

        natsInbox_Destroy(inbox);
    }

    args->status = s;
}

static void
test_natsInbox(void)
{
    natsStatus          s      = NATS_OK;
    natsThread          *threads[INBOX_THREADS_COUNT];
    struct threadArg    args[INBOX_THREADS_COUNT];
    int                 i, j;
    natsInbox           *key, *oldInbox;
    natsStrHash         *inboxes = NULL;
    natsStrHashIter     iter;

    test("Test inboxes are unique: ");
    for (i=0; i<INBOX_THREADS_COUNT; i++)
    {
        args[i].status = NATS_OK;
        args[i].inboxes    = NULL;
        threads[i]         = NULL;
    }

    s = natsStrHash_Create(&inboxes, 16);

    for (i=0; (s == NATS_OK) && (i<INBOX_THREADS_COUNT); i++)
    {
        s = natsStrHash_Create(&(args[i].inboxes), 16);
        if (s == NATS_OK)
            s = natsThread_Create(&(threads[i]), _testInbox, &(args[i]));
    }

    for (i=0; (s == NATS_OK) && (i<INBOX_THREADS_COUNT); i++)
    {
        natsThread_Join(threads[i]);

        s = args[i].status;
        if (s == NATS_OK)
        {
            j = 0;

            natsStrHashIter_Init(&iter, args[i].inboxes);
            while ((s == NATS_OK)
                    && natsStrHashIter_Next(&iter, &key, NULL))
            {
                j++;

                s = natsStrHash_Set(inboxes, key, true, (void*) 1, (void**) &oldInbox);

                natsStrHashIter_RemoveCurrent(&iter);
            }

            if (j != INBOX_COUNT_PER_THREAD)
                s = NATS_ERR;

            natsStrHashIter_Done(&iter);
        }

        natsThread_Destroy(threads[i]);
    }
    testCond(s == NATS_OK);

    for (i=0; i<INBOX_THREADS_COUNT; i++)
        natsStrHash_Destroy(args[i].inboxes);

    natsStrHash_Destroy(inboxes);
}

static int HASH_ITER = 10000000;

static void
test_natsHashing(void)
{
    const char *keys[] = {"foo",
                          "bar",
                          "apcera.continuum.router.foo.bar",
                          "apcera.continuum.router.foo.bar.baz"};
    const char *longKey = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@$#%^&*()";

    uint32_t results[] = {1058908168, 1061739001, 4242539713, 3332038527};
    uint32_t r, lr;
    natsStatus s = NATS_OK;
    int64_t start, end;
    int sizeLongKey = (int) strlen(longKey);

    if (valgrind)
        HASH_ITER = 10000;

    test("Test hashing algo: ");
    for (int i=0; i<(int)(sizeof(keys)/sizeof(char*)); i++)
    {
        r = natsStrHash_Hash(keys[i], (int) strlen(keys[i]));
        if (r != results[i])
        {
            printf("Expected: %u got: %u\n", results[i], r);
            s = NATS_ERR;
            break;
        }
    }
    testCond(s == NATS_OK);

    test("Hashing performance: ");
    s = NATS_OK;
    start = nats_Now();
    for (int i=0; i<HASH_ITER; i++)
    {
        r = natsStrHash_Hash(longKey, sizeLongKey);
        if ((i > 0) && (r != lr))
        {
            s = NATS_ERR;
            break;
        }
        lr = r;
    }
    end = nats_Now();
    testCond((s == NATS_OK) && ((end - start) < 1000));
}

static void
test_natsHash(void)
{
    natsStatus  s;
    natsHash    *hash = NULL;
    const char  *t1 = "this is a test";
    const char  *t2 = "this is another test";
    void        *oldval = NULL;
    int         lastNumBkts = 0;
    int         i;
    int64_t     key;
    int         values[40];
    natsHashIter iter;

    for (int i=0; i<40; i++)
        values[i] = (i+1);

    test("Create hash with invalid 0 size: ");
    s = natsHash_Create(&hash, 0);
    testCond((s != NATS_OK) && (hash == NULL));

    test("Create hash with invalid negative size: ");
    s = natsHash_Create(&hash, -2);
    testCond((s != NATS_OK) && (hash == NULL));

    test("Create hash ok: ");
    s = natsHash_Create(&hash, 8);
    testCond((s == NATS_OK) && (hash != NULL) && (hash->used == 0));

    test("Set: ");
    s = natsHash_Set(hash, 1234, (void*) t1, &oldval);
    testCond((s == NATS_OK) && (oldval == NULL) && (hash->used == 1));

    test("Set, get old value: ");
    s = natsHash_Set(hash, 1234, (void*) t2, &oldval);
    testCond((s == NATS_OK) && (oldval == t1) && (hash->used == 1))

    test("Get, not found: ");
    oldval = NULL;
    oldval = natsHash_Get(hash, 3456);
    testCond(oldval == NULL);

    test("Get, found: ");
    oldval = NULL;
    oldval = natsHash_Get(hash, 1234);
    testCond(oldval == t2);

    test("Remove, not found: ");
    oldval = NULL;
    oldval = natsHash_Remove(hash, 3456);
    testCond(oldval == NULL);

    test("Remove, found: ");
    oldval = NULL;
    oldval = natsHash_Remove(hash, 1234);
    testCond((oldval == t2) && (hash->used == 0));

    test("Test collision: ");
    oldval = NULL;
    s = natsHash_Set(hash, 2, (void*) t1, &oldval);
    if ((s == NATS_OK) && (oldval == NULL))
        s = natsHash_Set(hash, 10, (void*) t2, &oldval);
    testCond((s == NATS_OK)
             && (oldval == NULL)
             && (hash->used == 2)
             && (hash->bkts[2] != NULL)
             && (hash->bkts[2]->key == 10)
             && (hash->bkts[2]->next != NULL)
             && (hash->bkts[2]->next->key == 2));

    test("Remove from collisions (front to back): ");
    oldval = NULL;
    oldval = natsHash_Remove(hash, 10);
    if (oldval != t2)
        s = NATS_ERR;
    if (s == NATS_OK)
    {
        oldval = natsHash_Remove(hash, 2);
        if (oldval != t1)
            s = NATS_ERR;
    }
    testCond((s == NATS_OK) && (hash->used == 0));

    test("Remove from collisions (back to front): ");
    oldval = NULL;
    s = natsHash_Set(hash, 2, (void*) t1, &oldval);
    if ((s == NATS_OK) && (oldval == NULL))
        s = natsHash_Set(hash, 10, (void*) t2, &oldval);
    if (s == NATS_OK)
    {
        oldval = natsHash_Remove(hash, 2);
        if (oldval != t1)
            s = NATS_ERR;
    }
    if (s == NATS_OK)
    {
        oldval = natsHash_Remove(hash, 10);
        if (oldval != t2)
            s = NATS_ERR;
    }
    testCond((s == NATS_OK) && (hash->used == 0));

    test("Grow: ");
    for (int i=0; i<40; i++)
    {
        s = natsHash_Set(hash, (i+1), &(values[i]), &oldval);
        if (oldval != NULL)
            s = NATS_ERR;
        if (s != NATS_OK)
            break;
    }
    if (s == NATS_OK)
    {
        for (int i=0; i<40; i++)
        {
            oldval = natsHash_Get(hash, (i+1));
            if ((oldval == NULL)
                || ((*(int*)oldval) != values[i]))
            {
                s = NATS_ERR;
                break;
            }
        }
    }
    testCond((s == NATS_OK)
             && (hash->used == 40)
             && (hash->numBkts > 8));
    lastNumBkts = hash->numBkts;

    test("Shrink: ");
    for (int i=0; i<31; i++)
    {
        oldval = natsHash_Remove(hash, (i+1));
        if ((oldval == NULL)
            || ((*(int*)oldval) != values[i]))
        {
            s = NATS_ERR;
            break;
        }
    }
    testCond((s == NATS_OK)
             && (hash->used == 9)
             && (hash->numBkts < lastNumBkts));

    test("Iterator: ");
    natsHashIter_Init(&iter, hash);
    i = 0;
    while (natsHashIter_Next(&iter, &key, &oldval))
    {
        i++;
        if (((key < 31) || (key > 40))
            || (oldval == NULL)
            || ((*(int*)oldval) != values[key-1]))
        {
            s = NATS_ERR;
            break;
        }
    }
    natsHashIter_Done(&iter);
    testCond((s == NATS_OK) && (i == natsHash_Count(hash)));

    test("Iterator, remove current: ");
    natsHashIter_Init(&iter, hash);
    while (natsHashIter_Next(&iter, &key, NULL))
    {
        s = natsHashIter_RemoveCurrent(&iter);
        if (s != NATS_OK)
            break;
    }
    testCond((s == NATS_OK)
             && (natsHash_Count(hash) == 0)
             && (hash->canResize == false)
             && (hash->numBkts > 8));

    natsHashIter_Done(&iter);

    test("Grow again: ");
    oldval = NULL;
    for (int i=0; i<40; i++)
    {
        s = natsHash_Set(hash, (i+1), &(values[i]), &oldval);
        if (oldval != NULL)
            s = NATS_ERR;
        if (s != NATS_OK)
            break;
    }
    testCond((s == NATS_OK)
             && (hash->used == 40)
             && (hash->numBkts > 8));
    lastNumBkts = hash->numBkts;

    test("Iterator, remove current, hash does not shrink: ");
    natsHashIter_Init(&iter, hash);
    i = 0;
    while (natsHashIter_Next(&iter, &key, NULL))
    {
        s = natsHashIter_RemoveCurrent(&iter);
        if ((s != NATS_OK) || (++i == 31))
            break;
    }
    testCond((s == NATS_OK)
             && (natsHash_Count(hash) == 9)
             && (hash->canResize == false)
             && (hash->numBkts == lastNumBkts));

    natsHashIter_Done(&iter);

    test("After iterator done, shrink works: ");
    oldval = NULL;
    s = natsHash_Set(hash, 100, (void*) "last", &oldval);
    if ((s == NATS_OK) && (oldval == NULL))
    {
        oldval = natsHash_Remove(hash, 100);
        if ((oldval == NULL)
            || (strcmp((const char*) oldval, "last") != 0))
        {
            s = NATS_ERR;
        }
    }
    testCond((s == NATS_OK)
             && hash->canResize
             && (hash->numBkts != lastNumBkts));

    test("Destroy: ");
    natsHash_Destroy(hash);
    testCond(1);
}

static void
test_natsStrHash(void)
{
    natsStatus  s;
    natsStrHash *hash = NULL;
    const char  *t1 = "this is a test";
    const char  *t2 = "this is another test";
    void        *oldval = NULL;
    int         lastNumBkts = 0;
    int         i;
    char        *key;
    int         values[40];
    char        k[64];
    uint32_t    hk;
    natsStrHashIter iter;

    for (int i=0; i<40; i++)
        values[i] = (i+1);

    test("Create hash with invalid 0 size: ");
    s = natsStrHash_Create(&hash, 0);
    testCond((s != NATS_OK) && (hash == NULL));

    test("Create hash with invalid negative size: ");
    s = natsStrHash_Create(&hash, -2);
    testCond((s != NATS_OK) && (hash == NULL));

    test("Create hash ok: ");
    s = natsStrHash_Create(&hash, 8);
    testCond((s == NATS_OK) && (hash != NULL) && (hash->used == 0));

    test("Set: ");
    s = natsStrHash_Set(hash, (char*) "1234", false, (void*) t1, &oldval);
    testCond((s == NATS_OK) && (oldval == NULL) && (hash->used == 1));

    test("Set, get old value: ");
    s = natsStrHash_Set(hash, (char*) "1234", false, (void*) t2, &oldval);
    testCond((s == NATS_OK) && (oldval == t1) && (hash->used == 1))

    test("Get, not found: ");
    oldval = NULL;
    oldval = natsStrHash_Get(hash, (char*) "3456");
    testCond(oldval == NULL);

    test("Get, found: ");
    oldval = NULL;
    oldval = natsStrHash_Get(hash, (char*) "1234");
    testCond(oldval == t2);

    test("Remove, not found: ");
    oldval = NULL;
    oldval = natsStrHash_Remove(hash, (char*) "3456");
    testCond(oldval == NULL);

    test("Remove, found: ");
    oldval = NULL;
    oldval = natsStrHash_Remove(hash, (char*) "1234");
    testCond((oldval == t2) && (hash->used == 0));

    test("Grow: ");
    for (int i=0; i<40; i++)
    {
        snprintf(k, sizeof(k), "%d", (i+1));
        s = natsStrHash_Set(hash, k, true, &(values[i]), &oldval);
        if (oldval != NULL)
            s = NATS_ERR;
        if (s != NATS_OK)
            break;
    }
    if (s == NATS_OK)
    {
        for (int i=0; i<40; i++)
        {
            snprintf(k, sizeof(k), "%d", (i+1));
            oldval = natsStrHash_Get(hash, k);
            if ((oldval == NULL)
                || ((*(int*)oldval) != values[i]))
            {
                s = NATS_ERR;
                break;
            }
        }
    }
    testCond((s == NATS_OK)
             && (hash->used == 40)
             && (hash->numBkts > 8));
    lastNumBkts = hash->numBkts;

    test("Shrink: ");
    for (int i=0; i<31; i++)
    {
        snprintf(k, sizeof(k), "%d", (i+1));
        oldval = natsStrHash_Remove(hash, k);
        if ((oldval == NULL)
            || ((*(int*)oldval) != values[i]))
        {
            s = NATS_ERR;
            break;
        }
    }
    testCond((s == NATS_OK)
             && (hash->used == 9)
             && (hash->numBkts < lastNumBkts));

    test("Iterator: ");
    natsStrHashIter_Init(&iter, hash);
    i = 0;
    while (natsStrHashIter_Next(&iter, &key, &oldval))
    {
        i++;
        if (((atoi(key) < 31) || (atoi(key) > 40))
            || (oldval == NULL)
            || ((*(int*)oldval) != values[atoi(key)-1]))
        {
            s = NATS_ERR;
            break;
        }
    }
    natsStrHashIter_Done(&iter);
    testCond((s == NATS_OK) && (i == natsStrHash_Count(hash)));

    test("Iterator, remove current: ");
    natsStrHashIter_Init(&iter, hash);
    while (natsStrHashIter_Next(&iter, &key, NULL))
    {
        s = natsStrHashIter_RemoveCurrent(&iter);
        if (s != NATS_OK)
            break;
    }
    testCond((s == NATS_OK)
             && (natsStrHash_Count(hash) == 0)
             && (hash->canResize == false)
             && (hash->numBkts > 8));

    natsStrHashIter_Done(&iter);

    test("Grow again: ");
    oldval = NULL;
    for (int i=0; i<40; i++)
    {
        snprintf(k, sizeof(k), "%d", (i+1));
        s = natsStrHash_Set(hash, k, true, &(values[i]), &oldval);
        if (oldval != NULL)
            s = NATS_ERR;
        if (s != NATS_OK)
            break;
    }
    testCond((s == NATS_OK)
             && (hash->used == 40)
             && (hash->numBkts > 8));
    lastNumBkts = hash->numBkts;

    test("Iterator, remove current, hash does not shrink: ");
    natsStrHashIter_Init(&iter, hash);
    i = 0;
    while (natsStrHashIter_Next(&iter, &key, NULL))
    {
        s = natsStrHashIter_RemoveCurrent(&iter);
        if ((s != NATS_OK) || (++i == 31))
            break;
    }
    testCond((s == NATS_OK)
             && (natsStrHash_Count(hash) == 9)
             && (hash->canResize == false)
             && (hash->numBkts == lastNumBkts));

    natsStrHashIter_Done(&iter);

    test("After iterator done, shrink works: ");
    oldval = NULL;
    s = natsStrHash_Set(hash, (char*) "100", true, (void*) "last", &oldval);
    if ((s == NATS_OK) && (oldval == NULL))
    {
        oldval = natsStrHash_Remove(hash, (char*) "100");
        if ((oldval == NULL)
            || (strcmp((const char*) oldval, "last") != 0))
        {
            s = NATS_ERR;
        }
    }
    testCond((s == NATS_OK)
             && hash->canResize
             && (hash->numBkts != lastNumBkts));

    test("Copy key: ");
    snprintf(k, sizeof(k), "%s", "keycopied");
    hk = natsStrHash_Hash(k, (int) strlen(k));
    s = natsStrHash_Set(hash, k, true, (void*) t1, &oldval);
    if (s == NATS_OK)
    {
        // Changing the key does not affect the hash
        snprintf(k, sizeof(k), "%s", "keychanged");
        if (natsStrHash_Get(hash, (char*) "keycopied") != t1)
            s = NATS_ERR;
    }
    testCond((s == NATS_OK)
              && (oldval == NULL)
              && (hash->bkts[hk & hash->mask]->hk == hk)
              && (hash->bkts[hk & hash->mask]->freeKey == true));

    test("Key referenced: ");
    snprintf(k, sizeof(k), "%s", "keyreferenced");
    hk = natsStrHash_Hash(k, (int) strlen(k));
    s = natsStrHash_Set(hash, k, false, (void*) t2, &oldval);
    if (s == NATS_OK)
    {
        // Changing the key affects the hash
        snprintf(k, sizeof(k), "%s", "keychanged");
        if (natsStrHash_Get(hash, (char*) "keyreferenced") == t2)
            s = NATS_ERR;
    }
    testCond((s == NATS_OK)
              && (oldval == NULL)
              && (hash->bkts[hk & hash->mask]->hk == hk)
              && (hash->bkts[hk & hash->mask]->freeKey == false)
              && (strcmp(hash->bkts[hk & hash->mask]->key, "keychanged") == 0));

    test("Destroy: ");
    natsStrHash_Destroy(hash);
    testCond(1);
}

static void
_dummyErrHandler(natsConnection *nc, natsSubscription *sub, natsStatus err,
                 void *closure)
{
    // do nothing
}

static void
_dummyConnHandler(natsConnection *nc, void *closure)
{
    // do nothing
}

static void
test_natsOptions(void)
{
    natsStatus  s;
    natsOptions *opts = NULL;
    natsOptions *cloned = NULL;
    const char  *servers[] = {"1", "2", "3"};
    const char  *servers2[] = {"1", "2", "3", "4"};

    test("Create options: ");
    s = natsOptions_Create(&opts);
    testCond(s == NATS_OK);

    test("Test defaults: ");
    testCond((opts->allowReconnect == true)
             && (opts->maxReconnect == 60)
             && (opts->reconnectWait == 2 * 1000)
             && (opts->timeout == 2 * 1000)
             && (opts->pingInterval == 2 * 60 *1000)
             && (opts->maxPingsOut == 2)
             && (opts->maxPendingMsgs == 65536));

    test("Add URL: ");
    s = natsOptions_SetURL(opts, "test");
    testCond((s == NATS_OK)
              && (opts->url != NULL)
              && (strcmp(opts->url, "test") == 0));

    test("Replace URL: ");
    s = natsOptions_SetURL(opts, "test2");
    testCond((s == NATS_OK)
              && (opts->url != NULL)
              && (strcmp(opts->url, "test2") == 0));

    test("Remove URL: ");
    s = natsOptions_SetURL(opts, NULL);
    testCond((s == NATS_OK)
              && (opts->url == NULL));

    test("Set Servers (invalid args): ");
    s = natsOptions_SetServers(opts, servers, -2);
    if (s != NATS_OK)
        s = natsOptions_SetServers(opts, servers, 0);
    testCond(s != NATS_OK);

    test("Set Servers: ");
    s = natsOptions_SetServers(opts, servers, 3);
    testCond((s == NATS_OK)
             && (opts->servers != NULL)
             && (opts->serversCount == 3));

    test("Replace Servers: ");
    s = natsOptions_SetServers(opts, servers2, 4);
    if ((s == NATS_OK) && (opts->servers != NULL) && (opts->serversCount == 4))
    {
        for (int i=0; i<4; i++)
        {
            if (strcmp(opts->servers[i], servers2[i]) != 0)
            {
                s = NATS_ERR;
                break;
            }
        }
    }
    testCond(s == NATS_OK);

    test("Remove servers: ");
    s = natsOptions_SetServers(opts, NULL, 0);
    testCond((s == NATS_OK)
             && (opts->servers == NULL)
             && (opts->serversCount == 0));

    test("Set NoRandomize: ");
    s = natsOptions_SetNoRandomize(opts, true);
    testCond((s == NATS_OK) && (opts->noRandomize == true));

    test("Remove NoRandomize: ");
    s = natsOptions_SetNoRandomize(opts, false);
    testCond((s == NATS_OK) && (opts->noRandomize == false));

    test("Set Timeout (invalid args): ");
    s = natsOptions_SetTimeout(opts, -10);
    testCond(s != NATS_OK);

    test("Set Timeout to zero: ");
    s = natsOptions_SetTimeout(opts, 0);
    testCond((s == NATS_OK) && (opts->timeout == 0));

    test("Set Timeout: ");
    s = natsOptions_SetTimeout(opts, 2000);
    testCond((s == NATS_OK) && (opts->timeout == 2000));

    test("Set Name: ");
    s = natsOptions_SetName(opts, "test");
    testCond((s == NATS_OK) && (opts->name != NULL)
             && (strcmp(opts->name, "test") == 0));

    test("Remove Name: ");
    s = natsOptions_SetName(opts, NULL);
    testCond((s == NATS_OK) && (opts->name == NULL));

    test("Set Verbose: ");
    s = natsOptions_SetVerbose(opts, true);
    testCond((s == NATS_OK) && (opts->verbose == true));

    test("Remove Verbose: ");
    s = natsOptions_SetVerbose(opts, false);
    testCond((s == NATS_OK) && (opts->verbose == false));

    test("Set Secure: ");
    s = natsOptions_SetSecure(opts, true);
    testCond((s == NATS_OK) && (opts->secure == true));

    test("Remove Secure: ");
    s = natsOptions_SetSecure(opts, false);
    testCond((s == NATS_OK) && (opts->secure == false));

    test("Set Pedantic: ");
    s = natsOptions_SetPedantic(opts, true);
    testCond((s == NATS_OK) && (opts->pedantic == true));

    test("Remove Pedantic: ");
    s = natsOptions_SetPedantic(opts, false);
    testCond((s == NATS_OK) && (opts->pedantic == false));

    test("Set Ping Interval (negative or 0 ok): ");
    s = natsOptions_SetPingInterval(opts, -1000);
    if ((s == NATS_OK) && (opts->pingInterval != -1000))
        s = NATS_ERR;
    if (s == NATS_OK)
        s = natsOptions_SetPingInterval(opts, 0);
    if ((s == NATS_OK) && (opts->pingInterval != 0))
        s = NATS_ERR;
    if (s == NATS_OK)
        s = natsOptions_SetPingInterval(opts, 1000);
    testCond((s == NATS_OK) && (opts->pingInterval == 1000));

    test("Set MaxPingsOut: ");
    s = natsOptions_SetMaxPingsOut(opts, -2);
    if (s == NATS_OK)
        s = natsOptions_SetMaxPingsOut(opts, 0);
    if (s == NATS_OK)
        s = natsOptions_SetMaxPingsOut(opts, 1);
    if (s == NATS_OK)
        s = natsOptions_SetMaxPingsOut(opts, 10);
    testCond((s == NATS_OK) && (opts->maxPingsOut == 10));

    test("Set AllowReconnect: ");
    s = natsOptions_SetAllowReconnect(opts, true);
    testCond((s == NATS_OK) && (opts->allowReconnect == true));

    test("Remove AllowReconnect: ");
    s = natsOptions_SetAllowReconnect(opts, false);
    testCond((s == NATS_OK) && (opts->allowReconnect == false));

    test("Set MaxReconnect (negative ok): ");
    s = natsOptions_SetMaxReconnect(opts, -10);
    if ((s == NATS_OK) && (opts->maxReconnect != -10))
        s = NATS_ERR;
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 0);
    if ((s == NATS_OK) && (opts->maxReconnect != 0))
        s = NATS_ERR;
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 10);
    testCond((s == NATS_OK) && (opts->maxReconnect == 10));

    test("Set Reconnect Wait (invalid args: ");
    s = natsOptions_SetReconnectWait(opts, -1000);
    testCond(s != NATS_OK);

    test("Set Reconnect Wait: ");
    s = natsOptions_SetReconnectWait(opts, 1000);
    testCond((s == NATS_OK) && (opts->reconnectWait == 1000));

    test("Remove Reconnect Wait: ");
    s = natsOptions_SetReconnectWait(opts, 0);
    testCond((s == NATS_OK) && (opts->reconnectWait == 0));

    test("Set Max Pending Msgs (invalid args: ");
    s = natsOptions_SetMaxPendingMsgs(opts, -1000);
    if (s != NATS_OK)
        s = natsOptions_SetMaxPendingMsgs(opts, 0);
    testCond(s != NATS_OK);

    test("Set Max Pending Msgs : ");
    s = natsOptions_SetMaxPendingMsgs(opts, 10000);
    testCond((s == NATS_OK) && (opts->maxPendingMsgs == 10000));

    test("Set Error Handler: ");
    s = natsOptions_SetErrorHandler(opts, _dummyErrHandler, NULL);
    testCond((s == NATS_OK) && (opts->asyncErrCb == _dummyErrHandler));

    test("Remove Error Handler: ");
    s = natsOptions_SetErrorHandler(opts, NULL, NULL);
    testCond((s == NATS_OK) && (opts->asyncErrCb == NULL));

    test("Set ClosedCB: ");
    s = natsOptions_SetClosedCB(opts, _dummyConnHandler, NULL);
    testCond((s == NATS_OK) && (opts->closedCb == _dummyConnHandler));

    test("Remove ClosedCB: ");
    s = natsOptions_SetClosedCB(opts, NULL, NULL);
    testCond((s == NATS_OK) && (opts->closedCb == NULL));

    test("Set DisconnectedCB: ");
    s = natsOptions_SetDisconnectedCB(opts, _dummyConnHandler, NULL);
    testCond((s == NATS_OK) && (opts->disconnectedCb == _dummyConnHandler));

    test("Remove DisconnectedCB: ");
    s = natsOptions_SetDisconnectedCB(opts, NULL, NULL);
    testCond((s == NATS_OK) && (opts->disconnectedCb == NULL));

    test("Set ReconnectedCB: ");
    s = natsOptions_SetReconnectedCB(opts, _dummyConnHandler, NULL);
    testCond((s == NATS_OK) && (opts->reconnectedCb == _dummyConnHandler));

    test("Remove ReconnectedCB: ");
    s = natsOptions_SetReconnectedCB(opts, NULL, NULL);
    testCond((s == NATS_OK) && (opts->reconnectedCb == NULL));

    // Prepare some values for the clone check
    s = natsOptions_SetURL(opts, "url");
    IFOK(s, natsOptions_SetServers(opts, servers, 3));
    IFOK(s, natsOptions_SetName(opts, "name"));
    IFOK(s, natsOptions_SetPingInterval(opts, 3000));
    IFOK(s, natsOptions_SetErrorHandler(opts, _dummyErrHandler, NULL));
    if (s != NATS_OK)
        FAIL("Unable to test natsOptions_clone() because of failure while setting");

    test("Cloning: ");
    s = NATS_OK;
    cloned = natsOptions_clone(opts);
    if (cloned == NULL)
        s = NATS_NO_MEMORY;
    else if ((cloned->pingInterval != 3000)
             || (cloned->asyncErrCb != _dummyErrHandler)
             || (cloned->name == NULL)
             || (strcmp(cloned->name, "name") != 0)
             || (cloned->url == NULL)
             || (strcmp(cloned->url, "url") != 0)
             || (cloned->servers == NULL)
             || (cloned->serversCount != 3))
    {
        s = NATS_ERR;
    }
    if (s == NATS_OK)
    {
        for (int i=0; i<3; i++)
        {
            if (strcmp(cloned->servers[i], servers[i]) != 0)
            {
                s = NATS_ERR;
                break;
            }
        }
    }
    testCond(s == NATS_OK);

    test("Destroy original does not affect clone: ");
    natsOptions_Destroy(opts);
    testCond((cloned != NULL)
             && (cloned->url != NULL)
             && (strcmp(cloned->url, "url") == 0));

    natsOptions_Destroy(cloned);
}

static void
test_natsSock_ReadLine(void)
{
    char        buffer[20];
    natsStatus  s;
    natsSockCtx ctx;

    memset(&ctx, 0, sizeof(natsSockCtx));

    snprintf(buffer, sizeof(buffer), "%s", "+OK\r\nPONG\r\nFOO\r\nxxx");
    buffer[3] = '\0';

    test("Read second line from buffer: ");
    s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
    testCond((s == NATS_OK) && (strcmp(buffer, "PONG") == 0));

    test("Read third line from buffer: ");
    s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
    testCond((s == NATS_OK) && (strcmp(buffer, "FOO") == 0));

    test("Next call should trigger recv, which is expected to fail: ");
    s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
    testCond(s != NATS_OK);
}

natsStatus
_checkStart(const char *url)
{
    natsStatus      s        = NATS_OK;
    natsUrl         *nUrl    = NULL;
    int             attempts = 0;
    natsSockCtx     ctx;
    fd_set          fdSet;

    memset(&ctx, 0, sizeof(natsSockCtx));
    ctx.fdSet = &fdSet;

    FD_ZERO(ctx.fdSet);
    natsDeadline_Init(&(ctx.deadline), 2000);

    s = natsUrl_Create(&nUrl, url);
    if (s == NATS_OK)
    {
        while (((s = natsSock_ConnectTcp(&ctx,
                                         nUrl->host, nUrl->port)) != NATS_OK)
               && (attempts++ < 10))
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
_startServer(const char *url, const char *cmdLineOpts, bool checkStart)
{
    SECURITY_ATTRIBUTES     sa;
    STARTUPINFO             si;
    HANDLE                  h;
    PROCESS_INFORMATION     *pid;
    DWORD                   flags = 0;
    BOOL                    createdOk = FALSE;
    BOOL                    hInheritance = FALSE;
    char                    *exeAndCmdLine = NULL;
    int                     ret;

    pid = calloc(1, sizeof(PROCESS_INFORMATION));
    if (pid == NULL)
        return NATS_INVALID_PID;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    ret = nats_asprintf(&exeAndCmdLine, "%s%s%s", natsServerExe,
                        (cmdLineOpts != NULL ? " " : ""),
                        (cmdLineOpts != NULL ? cmdLineOpts : ""));
    if (ret < 0)
    {
        printf("No memory allocating command line string!\n");
        return NATS_INVALID_PID;
    }

    if (!keepServerOutput)
    {
        ZeroMemory(&sa, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        h = CreateFile("server.log",
                       GENERIC_WRITE,
                       FILE_SHARE_WRITE | FILE_SHARE_READ,
                       &sa,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);

        si.dwFlags   |= STARTF_USESTDHANDLES;
        si.hStdInput  = NULL;
        si.hStdError  = h;
        si.hStdOutput = h;

        hInheritance = TRUE;
        flags        = CREATE_NO_WINDOW;
    }

    // Start the child process.
    if (!CreateProcess(NULL,
                       (LPSTR) exeAndCmdLine,
                       NULL,         // Process handle not inheritable
                       NULL,         // Thread handle not inheritable
                       hInheritance, // Set handle inheritance
                       flags,        // Creation flags
                       NULL,         // Use parent's environment block
                       NULL,         // Use parent's starting directory
                       &si,          // Pointer to STARTUPINFO structure
                       pid))        // Pointer to PROCESS_INFORMATION structure
    {

        printf("Unable to start '%s': error (%d).\n",
               exeAndCmdLine, GetLastError());
        free(exeAndCmdLine);
        return NATS_INVALID_PID;
    }

    free(exeAndCmdLine);

    if (checkStart && (_checkStart(url) != NATS_OK))
    {
        _stopServer(pid);
        return NATS_INVALID_PID;
    }

    return (natsPid) pid;
}

#else

typedef pid_t               natsPid;

static void
_stopServer(natsPid pid)
{
    int status = 0;

    if (pid == NATS_INVALID_PID)
        return;

    if (kill(pid, SIGTERM) < 0)
    {
        perror("kill with SIGTERM");
        if (kill(pid, SIGKILL) < 0)
        {
            perror("kill with SIGKILL");
        }
    }

    waitpid(pid, &status, 0);
}

static natsPid
_startServer(const char *url, const char *cmdLineOpts, bool checkStart)
{
    natsPid pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return NATS_INVALID_PID;
    }

    if (pid == 0)
    {
        char copyLine[1024];
        char *argvPtrs[64];
        char *line = NULL;
        int index = 0;

        snprintf(copyLine, sizeof(copyLine), "%s %s", natsServerExe,
                 (cmdLineOpts == NULL ? "" : cmdLineOpts));

        memset(argvPtrs, 0, sizeof(argvPtrs));
        line = copyLine;

        while (*line != '\0')
        {
            while ((*line == ' ') || (*line == '\t') || (*line == '\n'))
                *line++ = '\0';

            argvPtrs[index++] = line;
            while ((*line != '\0') && (*line != ' ')
                   && (*line != '\t') && (*line != '\n'))
            {
                line++;
            }
        }
        argvPtrs[index++] = NULL;

        // Child process. Replace with NATS server
        if (!keepServerOutput)
        {
            // Close the stdout/stderr so that the server's output
            // does not mingle with the test suite output.
            close(1);
            close(2);
        }
        execvp(argvPtrs[0], argvPtrs);
        perror("Exec failed: ");
        exit(1);
    }
    else if (checkStart
             && (_checkStart(url) != NATS_OK))
    {
        _stopServer(pid);
        return NATS_INVALID_PID;
    }

    // parent, return the child's PID back.
    return pid;
}
#endif

static natsOptions*
_createReconnectOptions(void)
{
    natsStatus  s;
    natsOptions *opts = NULL;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, "nats://localhost:22222");
    if (s == NATS_OK)
        s = natsOptions_SetAllowReconnect(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);
    if (s == NATS_OK)
        s = natsOptions_SetTimeout(opts, NATS_OPTS_DEFAULT_TIMEOUT);

    if (s != NATS_OK)
    {
        natsOptions_Destroy(opts);
        opts = NULL;
    }

    return opts;
}

static void
test_ReconnectServerStats(void)
{
    natsStatus      s;
    natsConnection  *nc       = NULL;
    natsOptions     *opts     = NULL;
    natsSrv         *srv      = NULL;
    natsPid         serverPid = NATS_INVALID_PID;

    test("Reconnect Server Stats: ");

    opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to create reconnect options!");

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    if (s == NATS_OK)
    {
        serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
        CHECK_SERVER_STARTED(serverPid);

        s = natsConnection_FlushTimeout(nc, 5000);
    }

    if (s == NATS_OK)
    {
        srv = natsSrvPool_GetCurrentServer(nc->srvPool, nc->url, NULL);
        if (srv == NULL)
            s = NATS_ILLEGAL_STATE;
    }

    testCond((s == NATS_OK) && (srv->reconnects == 0));

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _stopServer(serverPid);
}

static void
_disconnectedCb(natsConnection *nc, void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;
    int64_t             now  = nats_Now();

    natsMutex_Lock(arg->m);
    arg->disconnected = true;
    arg->disconnects++;
    if ((arg->control == 9) && (arg->disconnects > 1))
    {
        if (arg->disconnects <= 5)
            arg->disconnectedAt[arg->disconnects-2] = now;
    }
    natsCondition_Signal(arg->c);
    natsMutex_Unlock(arg->m);
}

static void
_reconnectedCb(natsConnection *nc, void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;
    int64_t             now  = nats_Now();

    natsMutex_Lock(arg->m);
    arg->reconnected = true;
    arg->reconnects++;
    if (arg->control == 9)
    {
        if (arg->reconnects <= 4)
            arg->reconnectedAt[arg->reconnects-1] = now;
    }
    natsCondition_Signal(arg->c);
    natsMutex_Unlock(arg->m);
}

static void
_recvTestString(natsConnection *nc, natsSubscription *sub, natsMsg *msg,
                void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;
    bool                doSignal = true;

    natsMutex_Lock(arg->m);

    switch (arg->control)
    {
        case 0:
        {
            if (strncmp(arg->string,
                        natsMsg_GetData(msg),
                        natsMsg_GetDataLength(msg)) != 0)
            {
                arg->status = NATS_ERR;
            }
            break;
        }
        case 1:
        {
            if (sub == NULL)
                arg->status = NATS_ERR;
            else if (strncmp(arg->string,
                    natsMsg_GetData(msg),
                    natsMsg_GetDataLength(msg)) != 0)
            {
                arg->status = NATS_ERR;
            }
            break;
        }
        case 2:
        {
            if (strcmp(arg->string, natsMsg_GetReply(msg)) != 0)
            {
                arg->status = NATS_ERR;
            }
            break;
        }
        case 3:
        case 9:
        {
            doSignal = false;
            arg->sum++;

            if ((arg->control != 9) && (arg->sum == 10))
            {
                arg->status = natsSubscription_Unsubscribe(sub);
                doSignal = true;
            }
            break;
        }
        case 4:
        {
            arg->status = natsConnection_PublishString(nc,
                                                       natsMsg_GetReply(msg),
                                                       arg->string);
            if (arg->status == NATS_OK)
                arg->status = natsConnection_Flush(nc);
            break;
        }
        case 5:
        {
            arg->status = natsConnection_Flush(nc);
            break;
        }
        case 6:
        {
            char seqnoStr[10];
            int seqno = 0;

            doSignal = false;

            snprintf(seqnoStr, sizeof(seqnoStr), "%.*s", natsMsg_GetDataLength(msg),
                     natsMsg_GetData(msg));

            seqno = atoi(seqnoStr);
            if (seqno >= 10)
                arg->status = NATS_ERR;
            else
                arg->results[seqno] = (arg->results[seqno] + 1);

            break;
        }
        case 7:
        {
            while (!arg->closed)
                natsCondition_Wait(arg->c, arg->m);

            break;
        }
        case 8:
        {
            arg->sum++;

            while (!arg->closed)
                natsCondition_Wait(arg->c, arg->m);

            break;
        }
    }

    if (doSignal)
    {
        arg->msgReceived = true;
        natsCondition_Signal(arg->c);
    }
    natsMutex_Unlock(arg->m);

    natsMsg_Destroy(msg);
}

static void
test_ParseStateReconnectFunctionality(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    test("Parse State Reconnect Functionality: ");

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
    {
        arg.string = "bar";
        arg.status = NATS_OK;
    }
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetDisconnectedCB(opts, _disconnectedCb, &arg) != NATS_OK))
    {
        FAIL("Unable to create reconnect options!");
    }

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    if (s == NATS_OK)
    {
        // Simulate partialState, this needs to be cleared
        natsConn_Lock(nc);
        nc->ps->state = OP_PON;
        natsConn_Unlock(nc);
    }

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.disconnected)
            s = natsCondition_TimedWait(arg.c, arg.m, 500);
        natsMutex_Unlock(arg.m);
    }

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);

    if (s == NATS_OK)
    {
        serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
        CHECK_SERVER_STARTED(serverPid);
    }

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(nc, 5000);

    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.msgReceived)
            s = natsCondition_TimedWait(arg.c, arg.m, 1500);
        natsMutex_Unlock(arg.m);

        if (s == NATS_OK)
            s = arg.status;
    }

    testCond((s == NATS_OK) && (nc->stats.reconnects == 1));

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ServersRandomize(void)
{
    natsStatus      s;
    natsOptions     *opts   = NULL;
    natsConnection  *nc     = NULL;
    int serversCount;

    serversCount = sizeof(testServers) / sizeof(char *);

    test("Server Pool with Randomize: ");

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
    {
        int same = 0;
        int allSame = 0;

        for (int iter=0; (s == NATS_OK) && (iter<1000); iter++)
        {
            s = natsConn_create(&nc, natsOptions_clone(opts));
            if (s == NATS_OK)
            {
                // In theory, this could happen...
                for (int i=0; i<serversCount; i++)
                {
                    if (strcmp(testServers[i],
                               nc->srvPool->srvrs[i]->url->fullUrl) == 0)
                    {
                        same++;
                    }
                }
                if (same == serversCount)
                    allSame++;
            }
            natsConn_release(nc);
            nc = NULL;
        }

        if (allSame > 10)
            s = NATS_ERR;
    }
    testCond(s == NATS_OK);

    // Now test that we do not randomize if proper flag is set.
    test("Server Pool With NoRandomize: ")
    s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsConn_create(&nc, natsOptions_clone(opts));
    if (s == NATS_OK)
    {
        for (int i=0; i<serversCount; i++)
            if (strcmp(testServers[i],
                       nc->srvPool->srvrs[i]->url->fullUrl) != 0)
            {
                s = NATS_ERR;
                break;
            }
    }
    testCond(s == NATS_OK);

    natsConn_release(nc);
    natsOptions_Destroy(opts);
}

static void
test_SelectNextServer(void)
{
    natsStatus      s;
    natsOptions     *opts   = NULL;
    natsConnection  *nc     = NULL;
    natsSrv         *srv    = NULL;
    int             serversCount;

    serversCount = sizeof(testServers) / sizeof(char *);

    test("Test default server pool selection: ");
    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsConn_create(&nc, natsOptions_clone(opts));
    testCond((s == NATS_OK)
             && (nc->url == nc->srvPool->srvrs[0]->url));

    test("Get next server: ");
    if (s == NATS_OK)
    {
        srv = natsSrvPool_GetNextServer(nc->srvPool, nc->opts, nc->url);
        if (srv != NULL)
            nc->url = srv->url;
    }
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->url != NULL));

    test("Check list size: ");
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->srvPool != NULL)
             && (nc->srvPool->size == serversCount));

    test("Check selection: ");
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->url != NULL)
             && (nc->url->fullUrl != NULL)
             && (strcmp(nc->url->fullUrl, testServers[1]) == 0));

    test("Check old was pushed to last position: ");
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->srvPool != NULL)
             && (nc->srvPool->srvrs != NULL)
             && (nc->srvPool->size > 0)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1] != NULL)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1]->url != NULL)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1]->url->fullUrl != NULL)
             && (strcmp(nc->srvPool->srvrs[nc->srvPool->size - 1]->url->fullUrl,
                        testServers[0]) == 0));

    test("Got correct server: ");
    testCond((s == NATS_OK)
             && (srv != NULL)
             && (nc != NULL)
             && (nc->srvPool != NULL)
             && (nc->srvPool->srvrs != NULL)
             && (nc->srvPool->size > 0)
             && (srv == (nc->srvPool->srvrs[0])));

    // Test that we do not keep servers where we have tried to reconnect past our limit.
    if (s == NATS_OK)
    {
        test("Get next server: ");
        if ((nc == NULL)
            || (nc->srvPool == NULL)
            || (nc->srvPool->srvrs == NULL)
            || (nc->srvPool->srvrs[0] == NULL))
        {
            s = NATS_ERR;
        }
        else
        {
            nc->srvPool->srvrs[0]->reconnects = nc->opts->maxReconnect;
        }
        if (s == NATS_OK)
        {
            srv = natsSrvPool_GetNextServer(nc->srvPool, nc->opts, nc->url);
            if (srv != NULL)
                nc->url = srv->url;
        }
        testCond((s == NATS_OK) && (nc->url != NULL));
    }

    // Check that we are now looking at #3, and current is not in the list.
    test("Check list size: ");
    testCond((s == NATS_OK)
             && (nc->srvPool->size == (serversCount - 1)));

    test("Check selection: ");
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->url != NULL)
             && (nc->url->fullUrl != NULL)
             && (strcmp(nc->url->fullUrl, testServers[2]) == 0));

    test("Check last server was discarded: ");
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (nc->srvPool != NULL)
             && (nc->srvPool->srvrs != NULL)
             && (nc->srvPool->size > 0)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1] != NULL)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1]->url != NULL)
             && (nc->srvPool->srvrs[nc->srvPool->size - 1]->url->fullUrl != NULL)
             && (strcmp(nc->srvPool->srvrs[nc->srvPool->size - 1]->url->fullUrl,
                        testServers[1]) != 0));

    natsConn_release(nc);
    natsOptions_Destroy(opts);
}

static void
test_DefaultConnection(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    natsOptions         *opts     = NULL;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetTimeout(opts, 500);
    if (s != NATS_OK)
        FAIL("Unable to setup test");

    test("Check connection fails without running server: ");
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s != NATS_OK)
        s = natsConnection_Connect(&nc, NULL);
    if (s != NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_NO_SERVER);

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test default connection: ");
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _stopServer(serverPid);
}

static void
test_UseDefaultURLIfNoServerSpecified(void)
{
    natsStatus          s;
    natsOptions         *opts     = NULL;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    s = natsOptions_Create(&opts);
    if (s != NATS_OK)
        FAIL("Unable to create options!");

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check we can connect even if no server is specified: ");
    s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_OK);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_ConnectionWithNullOptions(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check connect with NULL options is allowed: ");
    s = natsConnection_Connect(&nc, NULL);
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_ConnectionStatus(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    test("Test connection status should be CONNECTED: ");
    testCond((s == NATS_OK)
             && (natsConnection_Status(nc) == CONNECTED));

    if (s == NATS_OK)
    {
        natsConnection_Close(nc);
        test("Test connection status should be CLOSED: ");
        testCond(natsConnection_Status(nc) == CLOSED);
    }

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
_closedCb(natsConnection *nc, void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;

    natsMutex_Lock(arg->m);
    arg->closed = true;
    natsCondition_Signal(arg->c);
    natsMutex_Unlock(arg->m);
}

static void
test_ConnClosedCB(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetURL(opts, NATS_DEFAULT_URL) != NATS_OK)
        || (natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg) != NATS_OK))
    {
        FAIL("Unable to setup test for ConnClosedCB!");
    }

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        natsConnection_Close(nc);

    test("Test connection closed CB invoked: ");

    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);

    testCond((s == NATS_OK) && arg.closed);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_CloseDisconnectedCB(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetURL(opts, NATS_DEFAULT_URL) != NATS_OK)
        || (natsOptions_SetAllowReconnect(opts, false) != NATS_OK)
        || (natsOptions_SetDisconnectedCB(opts, _closedCb, (void*) &arg) != NATS_OK))
    {
        FAIL("Unable to setup test for ConnClosedCB!");
    }

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        natsConnection_Close(nc);

    test("Test connection disconnected CB invoked: ");

    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);

    testCond((s == NATS_OK) && arg.closed);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ServerStopDisconnectedCB(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetURL(opts, NATS_DEFAULT_URL) != NATS_OK)
        || (natsOptions_SetAllowReconnect(opts, false) != NATS_OK)
        || (natsOptions_SetDisconnectedCB(opts, _closedCb, (void*) &arg) != NATS_OK))
    {
        FAIL("Unable to setup test for ConnClosedCB!");
    }

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    test("Test connection disconnected CB invoked on server shutdown: ");

    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);

    testCond((s == NATS_OK) && arg.closed);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);
}

static void
_dummyMsgHandler(natsConnection *nc, natsSubscription *sub, natsMsg *msg,
                void *closure)
{
    // do nothing

    natsMsg_Destroy(msg);
}

static void
test_ClosedConnections(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *goodsub  = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&goodsub, nc, "foo");
    if (s == NATS_OK)
        natsConnection_Close(nc);

    // Test all API endpoints do the right thing with a closed connection.

    test("Publish on closed should fail: ")
    s = natsConnection_Publish(nc, "foo", NULL, 0);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("PublishMsg on closed should fail: ")
    s = natsMsg_Create(&msg, "foo", NULL, NULL, 0);
    if (s == NATS_OK)
        s = natsConnection_PublishMsg(nc, msg);
    testCond(s == NATS_CONNECTION_CLOSED);

    natsMsg_Destroy(msg);
    msg = NULL;

    test("Flush on closed should fail: ")
    s = natsConnection_Flush(nc);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("Subscribe on closed should fail: ")
    s = natsConnection_Subscribe(&sub, nc, "foo", _dummyMsgHandler, NULL);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("SubscribeSync on closed should fail: ")
    s = natsConnection_SubscribeSync(&sub, nc, "foo");
    testCond(s == NATS_CONNECTION_CLOSED);

    test("QueueSubscribe on closed should fail: ")
    s = natsConnection_QueueSubscribe(&sub, nc, "foo", "bar", _dummyMsgHandler, NULL);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("QueueSubscribeSync on closed should fail: ")
    s = natsConnection_QueueSubscribeSync(&sub, nc, "foo", "bar");
    testCond(s == NATS_CONNECTION_CLOSED);

    test("Request on closed should fail: ")
    s = natsConnection_Request(&msg, nc, "foo", NULL, 0, 10);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("NextMsg on closed should fail: ")
    s = natsSubscription_NextMsg(&msg, goodsub, 10);
    testCond(s == NATS_CONNECTION_CLOSED);

    test("Unsubscribe on closed should fail: ")
    s = natsSubscription_Unsubscribe(goodsub);
    testCond(s == NATS_CONNECTION_CLOSED);

    natsSubscription_Destroy(goodsub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_ConnectVerboseOption(void)
{
    natsStatus          s         = NATS_OK;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
    {
        opts = _createReconnectOptions();
        if (opts == NULL)
            s = NATS_ERR;
    }
    if (s == NATS_OK)
        s = natsOptions_SetVerbose(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (s != NATS_OK)
        FAIL("Unable to setup test");

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check connect OK with Verbose option: ");

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    testCond(s == NATS_OK)

    _stopServer(serverPid);
    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check reconnect OK with Verbose option: ");

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !(args.reconnected))
        s = natsCondition_TimedWait(args.c, args.m, 5000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
test_ReconnectTotalTime(void)
{
    natsStatus  s;
    natsOptions *opts = NULL;

    test("Check reconnect time: ");
    s = natsOptions_Create(&opts);
    testCond((s == NATS_OK)
             && ((opts->maxReconnect * opts->reconnectWait) >= (2 * 60 * 1000)));

    natsOptions_Destroy(opts);
}

static void
test_ReconnectDisallowedFlags(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, "nats://localhost:22222");
    if (s == NATS_OK)
        s = natsOptions_SetAllowReconnect(opts, false);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    test("Test connection closed CB invoked: ");

    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);

    testCond((s == NATS_OK) && arg.closed);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);
}

static void
test_ReconnectAllowedFlags(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, "nats://localhost:22222");
    if (s == NATS_OK)
        s = natsOptions_SetAllowReconnect(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 2);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 1000);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    test("Test reconnecting in progress: ");

    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 500);
    natsMutex_Unlock(arg.m);

    testCond((s == NATS_TIMEOUT)
             && !arg.disconnected
             && natsConnection_IsReconnecting(nc));

    natsConnection_Close(nc);
    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 500);
    natsMutex_Unlock(arg.m);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);
}

static void
test_BasicReconnectFunctionality(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
    {
        arg.string = "bar";
        arg.status = NATS_OK;
    }
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetDisconnectedCB(opts, _disconnectedCb, &arg) != NATS_OK))
    {
        FAIL("Unable to create reconnect options!");
    }

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    test("Disconnected CB invoked: ");
    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.disconnected)
            s = natsCondition_TimedWait(arg.c, arg.m, 500);
        natsMutex_Unlock(arg.m);
    }
    testCond((s == NATS_OK) && arg.disconnected);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);

    if (s == NATS_OK)
    {
        serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
        CHECK_SERVER_STARTED(serverPid);
    }

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(nc, 5000);

    test("Check message received after reconnect: ");
    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.msgReceived)
            s = natsCondition_TimedWait(arg.c, arg.m, 1500);
        natsMutex_Unlock(arg.m);

        if (s == NATS_OK)
            s = arg.status;
    }
    testCond((s == NATS_OK) && (nc->stats.reconnects == 1));

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    if (valgrind)
        nats_Sleep(1000);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
_doneCb(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    struct threadArg *arg = (struct threadArg*) closure;

    natsMutex_Lock(arg->m);
    arg->done = true;
    natsCondition_Signal(arg->c);
    natsMutex_Unlock(arg->m);

    natsMsg_Destroy(msg);
}

static void
test_ExtendedReconnectFunctionality(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsSubscription    *sub2     = NULL;
    natsSubscription    *sub3     = NULL;
    natsSubscription    *sub4     = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
    {
        arg.string = "bar";
        arg.status = NATS_OK;
        arg.control=3;
    }
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetReconnectedCB(opts, _reconnectedCb, &arg) != NATS_OK)
        || (natsOptions_SetDisconnectedCB(opts, _disconnectedCb, &arg) != NATS_OK))
    {
        FAIL("Unable to create reconnect options!");
    }

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub2, nc, "foobar", _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    test("Disconnected CB invoked: ");
    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.disconnected)
            s = natsCondition_TimedWait(arg.c, arg.m, 2000);
        natsMutex_Unlock(arg.m);
    }
    testCond((s == NATS_OK) && arg.disconnected);

    // Sub while disconnected
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub3, nc, "bar", _recvTestString, &arg);

    // Unsubscribe foobar while disconnected
    if (s == NATS_OK)
        s = natsSubscription_Unsubscribe(sub2);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "bar", arg.string);

    if (s == NATS_OK)
    {
        serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
        CHECK_SERVER_STARTED(serverPid);
    }

    // Server is restarted, wait for reconnect
    test("Check reconnected: ");
    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.reconnected)
            s = natsCondition_TimedWait(arg.c, arg.m, 2000);
        natsMutex_Unlock(arg.m);
    }
    testCond((s == NATS_OK) && arg.reconnected);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foobar", arg.string);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);

    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub4, nc, "done", _doneCb, &arg);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "done", "done");

    test("Done msg received: ")
    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);
        while ((s == NATS_OK) && !arg.done)
            s = natsCondition_TimedWait(arg.c, arg.m, 2000);
        natsMutex_Unlock(arg.m);
    }
    testCond((s == NATS_OK) && arg.done);

    nats_Sleep(50);

    test("All msgs were received: ");
    testCond(arg.sum == 4);

    natsSubscription_Destroy(sub);
    natsSubscription_Destroy(sub2);
    natsSubscription_Destroy(sub3);
    natsSubscription_Destroy(sub4);;
    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_QueueSubsOnReconnect(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub1     = NULL;
    natsSubscription    *sub2     = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
    {
        arg.string = "bar";
        arg.status = NATS_OK;
        arg.control= 6;
    }
    if (s == NATS_OK)
        opts = _createReconnectOptions();

    if ((opts == NULL)
        || (natsOptions_SetReconnectedCB(opts, _reconnectedCb, &arg) != NATS_OK))
    {
        FAIL("Unable to create reconnect options!");
    }

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&sub1, nc, "foo.bar", "workers",
                                          _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribe(&sub2, nc, "foo.bar", "workers",
                                          _recvTestString, &arg);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    for (int i=0; (s == NATS_OK) && (i < 10); i++)
    {
        char seq[10];

        snprintf(seq, sizeof(seq), "%d", i);
        s = natsConnection_PublishString(nc, "foo.bar", seq);
    }

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    nats_Sleep(50);

    natsMutex_Lock(arg.m);
    for (int i=0; (s == NATS_OK) && (i<10); i++)
    {
        if (arg.results[i] != 1)
            s = NATS_ERR;
    }
    if (s == NATS_OK)
        s = arg.status;

    memset(&arg.results, 0, sizeof(arg.results));
    natsMutex_Unlock(arg.m);

    test("Base results: ");
    testCond(s == NATS_OK);

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Reconnects: ")
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.reconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.reconnected);

    for (int i=0; (s == NATS_OK) && (i < 10); i++)
    {
        char seq[10];

        snprintf(seq, sizeof(seq), "%d", i);
        s = natsConnection_PublishString(nc, "foo.bar", seq);
    }

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    nats_Sleep(50);

    natsMutex_Lock(arg.m);
    for (int i=0; (s == NATS_OK) && (i<10); i++)
    {
        if (arg.results[i] != 1)
            s = NATS_ERR;
    }
    if (s == NATS_OK)
        s = arg.status;

    memset(&arg.results, 0, sizeof(arg.results));
    natsMutex_Unlock(arg.m);

    test("Reconnect results: ");
    testCond(s == NATS_OK);

    natsSubscription_Destroy(sub1);
    natsSubscription_Destroy(sub2);
    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_IsClosed(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, "nats://localhost:22222");
    test("Check IsClosed is correct: ");
    testCond((s == NATS_OK) && !natsConnection_IsClosed(nc));

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    test("Check IsClosed after server shutdown: ");
    testCond((s == NATS_OK) && !natsConnection_IsClosed(nc));

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check IsClosed after server restart: ");
    testCond((s == NATS_OK) && !natsConnection_IsClosed(nc));

    natsConnection_Close(nc);
    test("Check IsClosed after connection closed: ");
    testCond((s == NATS_OK) && natsConnection_IsClosed(nc));

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_IsReconnectingAndStatus(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s == NATS_OK)
        s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, "nats://localhost:22222");
    if (s == NATS_OK)
        s = natsOptions_SetAllowReconnect(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 10000);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, (void*) &arg);

    // Connect, verify initial reconnecting state check, then stop the server
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);

    test("Check reconnecting state: ");
    testCond((s == NATS_OK) && !natsConnection_IsReconnecting(nc));

    test("Check status: ");
    testCond((s == NATS_OK) && (natsConnection_Status(nc) == CONNECTED));

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    // Wait until we get the disconnected callback
    test("Check we are disconnected: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.disconnected);

    test("Check IsReconnecting is correct: ");
    testCond(natsConnection_IsReconnecting(nc));

    test("Check Status is correct: ");
    testCond(natsConnection_Status(nc) == RECONNECTING);

    serverPid = _startServer("nats://localhost:22222", "-p 22222", true);
    CHECK_SERVER_STARTED(serverPid);

    // Wait until we get the reconnect callback
    test("Check we are reconnected: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.reconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.reconnected);

    test("Check IsReconnecting is correct: ");
    testCond(!natsConnection_IsReconnecting(nc));

    test("Check Status is correct: ");
    testCond(natsConnection_Status(nc) == CONNECTED);

    // Close the connection, reconnecting should still be false
    natsConnection_Close(nc);

    test("Check IsReconnecting is correct: ");
    testCond(!natsConnection_IsReconnecting(nc));

    test("Check Status is correct: ");
    testCond(natsConnection_Status(nc) == CLOSED);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
_closeConnWithDelay(void *arg)
{
    natsConnection *nc = (natsConnection*) arg;

    nats_Sleep(200);
    natsConnection_Close(nc);
}

static void
_connectToMockupServer(void *closure)
{
    struct threadArg    *arg = (struct threadArg *) closure;
    natsConnection      *nc = NULL;
    natsOptions         *opts = NULL;
    natsStatus          s;
    int                 control;

    // Make sure that the server is ready to accept our connection.
    nats_Sleep(100);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetAllowReconnect(opts, false);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);

    natsOptions_Destroy(opts);

    natsMutex_Lock(arg->m);
    control = arg->control;
    natsMutex_Unlock(arg->m);

    if (control == 2)
    {
        int64_t payload = 0;

        if (s == NATS_OK)
        {
            test("Check expected max payload: ")
            payload = natsConnection_GetMaxPayload(nc);
            if (payload != 10)
                s = NATS_ERR;
            testCond(s == NATS_OK);
        }
        if (s == NATS_OK)
        {
            test("Expect getting an error when publish more than max payload: ");
            s = natsConnection_PublishString(nc, "hello", "Hello World!");
            testCond(s != NATS_OK);

            // reset status
            s = NATS_OK;
        }
        if (s == NATS_OK)
        {
            test("Expect success if publishing less than max payload: ");
            s = natsConnection_PublishString(nc, "hello", "a");
            testCond(s == NATS_OK);
        }

        natsMutex_Lock(arg->m);
        arg->closed = true;
        natsCondition_Signal(arg->c);
        natsMutex_Unlock(arg->m);
    }
    else if (control == 3)
    {
        natsThread *t = NULL;

        s = natsThread_Create(&t, _closeConnWithDelay, (void*) nc);
        if (s == NATS_OK)
        {
            s = natsConnection_Flush(nc);

            natsThread_Join(t);
            natsThread_Destroy(t);
        }
    }
    else if (control == 4)
    {
        s = natsConnection_Flush(nc);
    }

    natsConnection_Destroy(nc);

    natsMutex_Lock(arg->m);
    arg->status = s;
    natsCondition_Signal(arg->c);
    natsMutex_Unlock(arg->m);
}

static void
test_ErrOnConnectAndDeadlock(void)
{
    struct addrinfo     hints;
    struct addrinfo     *servinfo = NULL;
    int                 res;
    natsStatus          s = NATS_OK;
    natsSock            sock = NATS_SOCK_INVALID;
    natsThread          *t = NULL;
    struct threadArg    arg;
    natsSockCtx         ctx;

    memset(&ctx, 0, sizeof(natsSockCtx));

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("@@ Unable to setup test!");

    arg.control = 1;

    test("Verify that bad INFO does not cause deadlock in client: ");

    // We will hand run a fake server that will timeout and not return a proper
    // INFO proto. This is to test that we do not deadlock.

    memset(&hints,0,sizeof(hints));

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
    {
         hints.ai_family = AF_INET;

         if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
             s = NATS_SYS_ERROR;
    }
    if (s == NATS_OK)
    {
        sock = socket(servinfo->ai_family, servinfo->ai_socktype,
                      servinfo->ai_protocol);
        if (sock == NATS_SOCK_INVALID)
            s = NATS_SYS_ERROR;

        if (s == NATS_OK)
            s = natsSock_SetCommonTcpOptions(sock);
        if (s == NATS_OK)
            s = natsSock_SetBlocking(sock, true);
    }
    if ((s == NATS_OK)
        && (bind(sock, servinfo->ai_addr, (natsSockLen) servinfo->ai_addrlen) == NATS_SOCK_ERROR))
    {
        s = NATS_SYS_ERROR;
    }

    if ((s == NATS_OK) && (listen(sock, 100) == NATS_SOCK_ERROR))
        s = NATS_SYS_ERROR;

    // Start the thread that will try to connect to our server...
    if (s == NATS_OK)
        s = natsThread_Create(&t, _connectToMockupServer, (void*) &arg);

    if ((s == NATS_OK)
        && (((ctx.fd = accept(sock, NULL, NULL)) == NATS_SOCK_INVALID)
            || (natsSock_SetCommonTcpOptions(ctx.fd) != NATS_OK)))
    {
        s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        const char* badInfo = "INFOZ \r\n";

        // Send back a mal-formed INFO.
        s = natsSock_WriteFully(&ctx, badInfo, (int) strlen(badInfo));
    }

    if (s == NATS_OK)
    {
        natsMutex_Lock(arg.m);

        while ((s == NATS_OK) && (arg.status == NATS_OK))
            s = natsCondition_TimedWait(arg.c, arg.m, 3000);

        natsMutex_Unlock(arg.m);
    }

    if (t != NULL)
    {
        natsThread_Join(t);
        natsThread_Destroy(t);
    }

    testCond((s == NATS_OK) && (arg.status != NATS_OK));

    _destroyDefaultThreadArgs(&arg);

    natsSock_Close(ctx.fd);
    natsSock_Close(sock);

    freeaddrinfo(servinfo);
}

static void
test_ErrOnMaxPayloadLimit(void)
{
    struct addrinfo     hints;
    struct addrinfo     *servinfo = NULL;
    int                 res;
    natsStatus          s = NATS_OK;
    natsSock            sock = NATS_SOCK_INVALID;
    natsThread          *t = NULL;
    int                 expectedMaxPayLoad = 10;
    struct threadArg    arg;
    natsSockCtx         ctx;

    memset(&ctx, 0, sizeof(natsSockCtx));

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("@@ Unable to setup test!");

    arg.control = 2;

    memset(&hints,0,sizeof(hints));

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
    {
         hints.ai_family = AF_INET;

         if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
             s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        sock = socket(servinfo->ai_family, servinfo->ai_socktype,
                      servinfo->ai_protocol);
        if (sock == NATS_SOCK_INVALID)
            s = NATS_SYS_ERROR;

        if (s == NATS_OK)
            s = natsSock_SetCommonTcpOptions(sock);
        if (s == NATS_OK)
            s = natsSock_SetBlocking(sock, true);
    }

    if ((s == NATS_OK)
        && (bind(sock, servinfo->ai_addr, (natsSockLen) servinfo->ai_addrlen) == NATS_SOCK_ERROR))
    {
        s = NATS_SYS_ERROR;
    }

    if ((s == NATS_OK) && (listen(sock, 100) == NATS_SOCK_ERROR))
        s = NATS_SYS_ERROR;

    // Start the thread that will try to connect to our server...
    if (s == NATS_OK)
        s = natsThread_Create(&t, _connectToMockupServer, (void*) &arg);

    if ((s == NATS_OK)
        && (((ctx.fd = accept(sock, NULL, NULL)) == NATS_SOCK_INVALID)
            || (natsSock_SetCommonTcpOptions(ctx.fd) != NATS_OK)))
    {
        s = NATS_SYS_ERROR;
    }
    if (s == NATS_OK)
    {
        char info[1024];

        snprintf(info, sizeof(info),
                 "INFO {\"server_id\":\"foobar\",\"version\":\"0.6.8\",\"go\":\"go1.5\",\"host\":\"localhost\",\"port\":4222,\"auth_required\":false,\"ssl_required\":false,\"max_payload\":%d}\r\n",
                 expectedMaxPayLoad);

        // Send INFO.
        s = natsSock_WriteFully(&ctx, info, (int) strlen(info));
        if (s == NATS_OK)
        {
            char buffer[1024];

            memset(buffer, 0, sizeof(buffer));

            // Read connect and ping commands sent from the client
            s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
            if (s == NATS_OK)
                s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
        }
        // Send PONG
        if (s == NATS_OK)
            s = natsSock_WriteFully(&ctx, _PONG_PROTO_, _PONG_PROTO_LEN_);
    }

    // Wait for the client to be about to close the connection.
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !(arg.closed))
        s = natsCondition_TimedWait(arg.c, arg.m, 3000);
    natsMutex_Unlock(arg.m);

    natsSock_Close(ctx.fd);
    natsSock_Close(sock);

    // Wait for the client to finish.
    if (t != NULL)
    {
        natsThread_Join(t);
        natsThread_Destroy(t);
    }

    test("Test completed ok: ");
    testCond(s == NATS_OK);

    _destroyDefaultThreadArgs(&arg);

    freeaddrinfo(servinfo);
}

static void
test_Auth(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    test("Server with auth on, client without should fail: ");

    serverPid = _startServer("nats://localhost:8232", "--user ivan --pass foo -p 8232", false);
    CHECK_SERVER_STARTED(serverPid);

    nats_Sleep(1000);

    s = natsConnection_ConnectTo(&nc, "nats://localhost:8232");
    testCond(s != NATS_OK);

    test("Server with auth on, client with proper auth should succeed: ");

    s = natsConnection_ConnectTo(&nc, "nats://ivan:foo@localhost:8232");
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_AuthFailNoDisconnectCB(void)
{
    natsStatus          s;
    natsOptions         *opts     = NULL;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serverPid = _startServer("nats://localhost:8232", "--user ivan --pass foo -p 8232", false);
    CHECK_SERVER_STARTED(serverPid);

    nats_Sleep(1000);

    opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to create options!");

    test("Connect should fail: ");
    s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, &arg);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s != NATS_OK);

    test("DisconnectCb should not be invoked on auth failure: ");
    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 1000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_TIMEOUT) && !arg.disconnected);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ConnectedServer(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    char                buffer[128];

    buffer[0] = '\0';

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Verify ConnectedUrl is correct: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));
    testCond((s == NATS_OK)
             && (buffer[0] != '\0')
             && (strcmp(buffer, NATS_DEFAULT_URL) == 0));

    buffer[0] = '\0';

    test("Verify ConnectedServerId is not null: ")
    if (s == NATS_OK)
        s = natsConnection_GetConnectedServerId(nc, buffer, sizeof(buffer));
    testCond((s == NATS_OK)  && (buffer[0] != '\0'));

    buffer[0] = '\0';

    test("Verify ConnectedUrl is empty after disconnect: ")
    if (s == NATS_OK)
    {
        natsConnection_Close(nc);
        s = natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));
    }
    testCond((s == NATS_OK) && (buffer[0] == '\0'));

    buffer[0] = '\0';

    test("Verify ConnectedServerId is empty after disconnect: ")
    if (s == NATS_OK)
        s = natsConnection_GetConnectedServerId(nc, buffer, sizeof(buffer));
    testCond((s == NATS_OK)  && (buffer[0] == '\0'));

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
_closeConn(void *arg)
{
    natsConnection *nc = (natsConnection*) arg;

    natsConnection_Close(nc);
}

static void
test_MultipleClose(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsThread          *threads[10];
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test that multiple Close are fine: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    for (int i=0; (s == NATS_OK) && (i<10); i++)
        s = natsThread_Create(&(threads[i]), _closeConn, (void*) nc);

    for (int i=0; (s == NATS_OK) && (i<10); i++)
    {
        natsThread_Join(threads[i]);
        natsThread_Destroy(threads[i]);
    }
    testCond((s == NATS_OK)
             && (nc->status == CLOSED)
             && (nc->refs == 1));

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_SimplePublish(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test simple publish: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "Hello world!");
    if (s == NATS_OK)
        s = natsConnection_Publish(nc, "foo", (const void*) "Hello world!",
                                   (int) strlen("Hello world!"));
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_SimplePublishNoData(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test simple publish with no data: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", NULL);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "");
    if (s == NATS_OK)
        s = natsConnection_Publish(nc, "foo", NULL, 0);
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_AsyncSubscribe(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.string = "Hello World";
    arg.status = NATS_OK;
    arg.control= 1;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test async subscriber: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString,
                                     (void*) &arg);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", arg.string);

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.msgReceived)
        s = natsCondition_TimedWait(arg.c, arg.m, 1500);
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    testCond(s == NATS_OK);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_SyncSubscribe(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    const char          *string   = "Hello World";

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test sync subscriber: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", string);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 1000);
    testCond((s == NATS_OK)
             && (msg != NULL)
             && (strncmp(string, natsMsg_GetData(msg), natsMsg_GetDataLength(msg)) == 0));

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_PubSubWithReply(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    const char          *string   = "Hello World";

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test PubSub with reply: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsConnection_PublishRequestString(nc, "foo", "bar", string);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 1000);
    testCond((s == NATS_OK)
             && (msg != NULL)
             && (strncmp(string, natsMsg_GetData(msg), natsMsg_GetDataLength(msg)) == 0));

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

struct flushArg
{
    natsConnection      *nc;
    natsStatus          s;
    int                 count;
    int64_t             timeout;
    int64_t             initialSleep;
    int64_t             loopSleep;
};

static void
_doFlush(void *arg)
{
    struct flushArg     *p = (struct flushArg*) arg;
    int                 i;

    nats_Sleep(p->initialSleep);

    for (i = 0; (p->s == NATS_OK) && (i < p->count); i++)
    {
        p->s = natsConnection_FlushTimeout(p->nc, p->timeout);
        if ((p->s == NATS_OK) && (p->loopSleep > 0))
            nats_Sleep(p->loopSleep);
    }
}

static void
test_Flush(void)
{
    natsStatus          s;
    natsOptions         *opts     = NULL;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    const char          *string   = "Hello World";
    natsThread          *threads[3] = { NULL, NULL, NULL };
    struct flushArg     args[3];
    int64_t             start = 0;
    int64_t             elapsed = 0;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);
    if (s == NATS_OK)
        s = natsOptions_SetPingInterval(opts, 100);

    if (s != NATS_OK)
        FAIL("Unable to setup test");

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test Flush empties buffer: ")
    s = natsConnection_Connect(&nc, opts);
    for (int i=0; (s == NATS_OK) && (i < 1000); i++)
        s = natsConnection_PublishString(nc, "flush", string);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond((s == NATS_OK)
             && natsConnection_Buffered(nc) == 0);

    test("Check parallel Flush: ")
    for (int i=0; (s == NATS_OK) && (i < 3); i++)
    {
        args[i].nc           = nc;
        args[i].s            = NATS_OK;
        args[i].timeout      = 5000;
        args[i].count        = 1000;
        args[i].initialSleep = 500;
        args[i].loopSleep    = 1;
        s = natsThread_Create(&(threads[i]), _doFlush, (void*) &(args[i]));
    }

    for (int i=0; (s == NATS_OK) && (i < 10000); i++)
        s = natsConnection_PublishString(nc, "flush", "Hello world");

    for (int i=0; (i < 3); i++)
    {
        if (threads[i] == NULL)
            continue;

        natsThread_Join(threads[i]);
        natsThread_Destroy(threads[i]);

        if (args[i].s != NATS_OK)
            s = args[i].s;
    }
    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);
    nc = NULL;

    test("Check Flush while in doReconnect: ")
    s = natsOptions_SetReconnectWait(opts, 3000);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
    {
        // Capture the moment we connected
        start = nats_Now();

        // Stop the server
        _stopServer(serverPid);
        serverPid = NATS_INVALID_PID;

        // We can restart right away, since the client library will wait 3 sec
        // before reconnecting.
        serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
        CHECK_SERVER_STARTED(serverPid);

        // Attempt to Flush. This should wait for the reconnect to occur, then
        // proceed.
        for (int i=0; (s == NATS_OK) && (i < 3); i++)
        {
            args[i].nc           = nc;
            args[i].s            = NATS_OK;
            args[i].timeout      = 5000;
            args[i].count        = 1;
            args[i].initialSleep = 1000;
            args[i].loopSleep    = 0;
            s = natsThread_Create(&(threads[i]), _doFlush, (void*) &(args[i]));
        }
    }
    for (int i=0; (i < 3); i++)
    {
        if (threads[i] == NULL)
            continue;

        natsThread_Join(threads[i]);
        natsThread_Destroy(threads[i]);

        if (args[i].s != NATS_OK)
            s = args[i].s;
    }
    if (s == NATS_OK)
        elapsed = (nats_Now() - start);

    testCond((s == NATS_OK) && (elapsed >= 2800) && (elapsed <= 3200));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_QueueSubscriber(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *s1       = NULL;
    natsSubscription    *s2       = NULL;
    natsMsg             *msg      = NULL;
    uint64_t            r1        = 0;
    uint64_t            r2        = 0;
    float               v         = 1000.0 * 0.15;
    int64_t             d1;
    int64_t             d2;
    natsPid             serverPid = NATS_INVALID_PID;
    const char          *string   = "Hello World";

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test QueueSubscriber receive correct amount: ");
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribeSync(&s1, nc, "foo", "bar");
    if (s == NATS_OK)
        s = natsConnection_QueueSubscribeSync(&s2, nc, "foo", "bar");
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", string);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    if (s == NATS_OK)
        s = natsSubscription_QueuedMsgs(s1, &r1);
    if (s == NATS_OK)
        s = natsSubscription_QueuedMsgs(s2, &r2);
    testCond((s == NATS_OK)
             && (r1 + r2 == 1));

    if (s == NATS_OK)
    {
        (void)natsSubscription_NextMsg(&msg, s1, 0);
        natsMsg_Destroy(msg);
        msg = NULL;

        (void)natsSubscription_NextMsg(&msg, s2, 0);
        natsMsg_Destroy(msg);
        msg = NULL;
    }

    test("Test correct amount when more messages are sent: ");
    for (int i=0; (s == NATS_OK) && (i<1000); i++)
        s = natsConnection_PublishString(nc, "foo", string);

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    r1 = r2 = 0;

    if (s == NATS_OK)
        s = natsSubscription_QueuedMsgs(s1, &r1);
    if (s == NATS_OK)
        s = natsSubscription_QueuedMsgs(s2, &r2);

    testCond((s == NATS_OK) && (r1 + r2 == 1000));

    test("Variance acceptable: ");
    d1 = 500 - r1;
    if (d1 < 0)
        d1 *= -1;

    d2 = 500 - r1;
    if (d2 < 0)
        d2 *= -1;

    testCond((d1 <= v) && (d2 <= v));

    natsSubscription_Destroy(s1);
    natsSubscription_Destroy(s2);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_ReplyArg(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.string = "bar";
    arg.status = NATS_OK;
    arg.control= 2;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test for correct Reply arg in callback: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString,
                                     (void*) &arg);
    if (s == NATS_OK)
        s = natsConnection_PublishRequestString(nc, "foo", "bar", "hello");

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.msgReceived)
        s = natsCondition_TimedWait(arg.c, arg.m, 1500);
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    testCond(s == NATS_OK);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_SyncReplyArg(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test for correct Reply arg in msg: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsConnection_PublishRequestString(nc, "foo", "bar", "hello");
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 1000);

    testCond((s == NATS_OK)
             && (msg != NULL)
             && (natsMsg_GetReply(msg) != NULL)
             && (strcmp("bar", natsMsg_GetReply(msg)) == 0));

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_Unsubscribe(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.string = "bar";
    arg.status = NATS_OK;
    arg.control= 3;
    arg.sum    = 0;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test for Unsubscribe in callback: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString,
                                     (void*) &arg);
    for (int i=0; (s == NATS_OK) && (i<20); i++)
        s = natsConnection_PublishString(nc, "foo", "hello");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK)
           && !arg.msgReceived)
    {
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    }
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    // See if the callback still fires
    nats_Sleep(500);

    testCond((s == NATS_OK)
             && (arg.sum == 10));

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_DoubleUnsubscribe(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test Double Unsubscribe should report an error: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsSubscription_Unsubscribe(sub);
    if (s != NATS_OK)
        FAIL("Unable to test Double Unsubscribe!");

    s = natsSubscription_Unsubscribe(sub);

    testCond(s != NATS_OK);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_RequestTimeout(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    test("Test Request should timeout: ")
    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_RequestString(&msg, nc, "foo", "bar", 10);
    testCond((s == NATS_TIMEOUT) && (msg == NULL));

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_Request(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.string = "I will help you";
    arg.status = NATS_OK;
    arg.control= 4;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);

    test("Test Request: ")
    if (s == NATS_OK)
        s = natsConnection_RequestString(&msg, nc, "foo", "help", 50);

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK)
           && !arg.msgReceived)
    {
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    }
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    testCond((s == NATS_OK)
             && (msg != NULL)
             && (strncmp(arg.string,
                         natsMsg_GetData(msg),
                         natsMsg_GetDataLength(msg)) == 0));

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_RequestNoBody(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.string = "I will help you";
    arg.status = NATS_OK;
    arg.control= 4;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);

    test("Test Request with no body content: ")
    if (s == NATS_OK)
        s = natsConnection_RequestString(&msg, nc, "foo", NULL, 50);

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK)
           && !arg.msgReceived)
    {
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    }
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    testCond((s == NATS_OK)
             && (msg != NULL)
             && (strncmp(arg.string,
                         natsMsg_GetData(msg),
                         natsMsg_GetDataLength(msg)) == 0));

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_FlushInCb(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 5;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);

    test("Test Flush in callback: ")
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "hello");

    natsMutex_Lock(arg.m);
    while ((s == NATS_OK)
           && !arg.msgReceived)
    {
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    }
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = arg.status;

    testCond(s == NATS_OK);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ReleaseFlush(void)
{
    struct addrinfo     hints;
    struct addrinfo     *servinfo = NULL;
    int                 res;
    natsStatus          s = NATS_OK;
    natsSock            sock = NATS_SOCK_INVALID;
    natsThread          *t = NULL;
    struct threadArg    arg;
    natsSockCtx         ctx;

    memset(&ctx, 0, sizeof(natsSockCtx));

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("@@ Unable to setup test!");

    test("Check that Flush() release on connection close: ")

    arg.control = 3;

    // We will hand run a fake server that will to reply to the second PING

    memset(&hints,0,sizeof(hints));

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
    {
         hints.ai_family = AF_INET;

         if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
             s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        sock = socket(servinfo->ai_family, servinfo->ai_socktype,
                      servinfo->ai_protocol);
        if (sock == NATS_SOCK_INVALID)
            s = NATS_SYS_ERROR;

        if (s == NATS_OK)
            s = natsSock_SetCommonTcpOptions(sock);
        if (s == NATS_OK)
            s = natsSock_SetBlocking(sock, true);
    }

    if ((s == NATS_OK)
        && (bind(sock, servinfo->ai_addr, (natsSockLen) servinfo->ai_addrlen) == NATS_SOCK_ERROR))
    {
        s = NATS_SYS_ERROR;
    }

    if ((s == NATS_OK) && (listen(sock, 100) == NATS_SOCK_ERROR))
        s = NATS_SYS_ERROR;

    // Start the thread that will try to connect to our server...
    if (s == NATS_OK)
        s = natsThread_Create(&t, _connectToMockupServer, (void*) &arg);

    if ((s == NATS_OK)
        && (((ctx.fd = accept(sock, NULL, NULL)) == NATS_SOCK_INVALID)
            || natsSock_SetCommonTcpOptions(ctx.fd) != NATS_OK))
    {
        s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        char buffer[1024];
        char info[1024];

        strncpy(info,
                "INFO {\"server_id\":\"foobar\",\"version\":\"0.6.8\",\"go\":\"go1.5\",\"host\":\"localhost\",\"port\":4222,\"auth_required\":false,\"ssl_required\":false,\"max_payload\":1048576}\r\n",
                sizeof(info));

        // Send INFO.
        s = natsSock_WriteFully(&ctx, info, (int) strlen(info));
        if (s == NATS_OK)
        {
            memset(buffer, 0, sizeof(buffer));

            // Read connect and ping commands sent from the client
            s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
            if (s == NATS_OK)
                s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
        }
        // Send PONG
        if (s == NATS_OK)
            s = natsSock_WriteFully(&ctx,
                                    _PONG_PROTO_, _PONG_PROTO_LEN_);

        // Get the PING
        if (s == NATS_OK)
            s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
    }

    // Now wait for the client to close the connection...
    nats_Sleep(500);

    // Need to close those for the client side to unblock.
    natsSock_Close(ctx.fd);
    natsSock_Close(sock);

    // Wait for the client to finish.
    if (t != NULL)
    {
        natsThread_Join(t);
        natsThread_Destroy(t);
    }

    testCond((s == NATS_OK) && (arg.status != NATS_OK));

    _destroyDefaultThreadArgs(&arg);

    freeaddrinfo(servinfo);
}


static void
test_FlushErrOnDisconnect(void)
{
    struct addrinfo     hints;
    struct addrinfo     *servinfo = NULL;
    int                 res;
    natsStatus          s = NATS_OK;
    natsSock            sock = NATS_SOCK_INVALID;
    natsThread          *t = NULL;
    struct threadArg    arg;
    natsSockCtx         ctx;

    memset(&ctx, 0, sizeof(natsSockCtx));

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("@@ Unable to setup test!");

    test("Check that Flush() returns an error during a disconnect: ");

    arg.control = 4;

    // We will hand run a fake server that will to reply to the second PING

    memset(&hints,0,sizeof(hints));

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
    {
         hints.ai_family = AF_INET;

         if ((res = getaddrinfo("localhost", "4222", &hints, &servinfo)) != 0)
             s = NATS_SYS_ERROR;
    }

    if (s == NATS_OK)
    {
        sock = socket(servinfo->ai_family, servinfo->ai_socktype,
                      servinfo->ai_protocol);
        if (sock == NATS_SOCK_INVALID)
            s = NATS_SYS_ERROR;

        if (s == NATS_OK)
            s = natsSock_SetCommonTcpOptions(sock);
        if (s == NATS_OK)
            s = natsSock_SetBlocking(sock, true);
    }

    if ((s == NATS_OK)
        && (bind(sock, servinfo->ai_addr, (natsSockLen) servinfo->ai_addrlen) == NATS_SOCK_ERROR))
    {
        s = NATS_SYS_ERROR;
    }

    if ((s == NATS_OK) && (listen(sock, 100) == NATS_SOCK_ERROR))
        s = NATS_SYS_ERROR;

    // Start the thread that will try to connect to our server...
    if (s == NATS_OK)
        s = natsThread_Create(&t, _connectToMockupServer, (void*) &arg);

    if ((s == NATS_OK)
        && (((ctx.fd = accept(sock, NULL, NULL)) == NATS_SOCK_INVALID)
            || (natsSock_SetCommonTcpOptions(ctx.fd) != NATS_OK)))
    {
        s = NATS_SYS_ERROR;
    }
    if (s == NATS_OK)
    {
        char info[1024];

        strncpy(info,
                "INFO {\"server_id\":\"foobar\",\"version\":\"0.6.8\",\"go\":\"go1.5\",\"host\":\"localhost\",\"port\":4222,\"auth_required\":false,\"ssl_required\":false,\"max_payload\":1048576}\r\n",
                sizeof(info));

        // Send INFO.
        s = natsSock_WriteFully(&ctx, info, (int) strlen(info));
        if (s == NATS_OK)
        {
            char buffer[1024];

            memset(buffer, 0, sizeof(buffer));

            // Read connect and ping commands sent from the client
            s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
            if (s == NATS_OK)
                s = natsSock_ReadLine(&ctx, buffer, sizeof(buffer));
        }
        // Send PONG
        if (s == NATS_OK)
            s = natsSock_WriteFully(&ctx,
                                    _PONG_PROTO_, _PONG_PROTO_LEN_);
    }

    // Wait a bit and kill the server.
    nats_Sleep(500);
    natsSock_Close(ctx.fd);
    natsSock_Close(sock);

    // Wait for the client to finish.
    if (t != NULL)
    {
        natsThread_Join(t);
        natsThread_Destroy(t);
    }

    testCond(arg.status != NATS_OK);

    _destroyDefaultThreadArgs(&arg);

    freeaddrinfo(servinfo);

    if (valgrind)
        nats_Sleep(1000);
}

static void
test_Inbox(void)
{
    natsStatus  s;
    natsInbox   *inbox = NULL;

    test("Inbox starts with correct prefix: ");
    s = natsInbox_Create(&inbox);
    testCond((s == NATS_OK)
             && (inbox != NULL)
             && (strncmp(inbox, "_INBOX.", 7) == 0));

    natsInbox_Destroy(inbox);
}

static void
test_Stats(void)
{
    natsStatus          s;
    natsConnection      *nc     = NULL;
    natsStatistics      *stats  = NULL;
    natsSubscription    *s1     = NULL;
    natsSubscription    *s2     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    const char          *data = "The quick brown fox jumped over the lazy dog";
    int                 iter  = 10;
    uint64_t            outMsgs = 0;
    uint64_t            outBytes = 0;
    uint64_t            inMsgs = 0;
    uint64_t            inBytes = 0;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);

    for (int i=0; (s == NATS_OK) && (i<iter); i++)
        s = natsConnection_PublishString(nc, "foo", data);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);
    if (s == NATS_OK)
        s = natsConnection_GetStats(nc, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, NULL, NULL, &outMsgs, &outBytes, NULL);

    test("Tracking OutMsgs properly: ");
    testCond((s == NATS_OK) && (outMsgs == (uint64_t) iter));

    test("Tracking OutBytes properly: ");
    testCond((s == NATS_OK) && (outBytes == (uint64_t) (iter * strlen(data))));

    // Test both sync and async versions of subscribe.
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&s1, nc, "foo", _dummyMsgHandler, NULL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&s2, nc, "foo");

    for (int i=0; (s == NATS_OK) && (i<iter); i++)
        s = natsConnection_PublishString(nc, "foo", data);

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    if (s == NATS_OK)
        s = natsConnection_GetStats(nc, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, &inMsgs, &inBytes, NULL, NULL, NULL);

    test("Tracking inMsgs properly: ");
    testCond((s == NATS_OK) && (inMsgs == (uint64_t)(2 * iter)));

    test("Tracking inBytes properly: ");
    testCond((s == NATS_OK) && (inBytes == (uint64_t)(2 * (iter * strlen(data)))));

    natsStatistics_Destroy(stats);
    natsSubscription_Destroy(s1);
    natsSubscription_Destroy(s2);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_BadSubject(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);

    test("Should get an error with empty subject: ");
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "", "hello");
    testCond(s != NATS_OK);

    test("Error should be NATS_INVALID_SUBJECT: ");
    testCond(s == NATS_INVALID_SUBJECT);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_ClientAsyncAutoUnsub(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 9;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 10);

    for (int i=0; (s == NATS_OK) && (i<100); i++)
        s = natsConnection_PublishString(nc, "foo", "hello");

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    nats_Sleep(10);

    test("Received no more than max: ");
    testCond(arg.sum == 10);

    test("IsValid should be false: ");
    testCond((sub != NULL) && !natsSubscription_IsValid(sub));

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ClientSyncAutoUnsub(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 received  = 0;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsSubscription_AutoUnsubscribe(sub, 10);

    for (int i=0; (s == NATS_OK) && (i<100); i++)
        s = natsConnection_PublishString(nc, "foo", "hello");

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    test("Get correct error: ");
    for (int i=0; (s == NATS_OK) && (i<100); i++)
    {
        s = natsSubscription_NextMsg(&msg, sub, 10);
        if (s == NATS_OK)
        {
            received++;
            natsMsg_Destroy(msg);
        }
    }
    testCond(s == NATS_MAX_DELIVERED_MSGS);

    test("Received no more than max: ");
    testCond(received == 10);

    test("IsValid should be false: ");
    testCond((sub != NULL) && !natsSubscription_IsValid(sub));

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_NextMsgOnClosedSub(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");
    if (s == NATS_OK)
        s = natsSubscription_Unsubscribe(sub);

    test("Get correct error: ");
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 1000);
    testCond(s == NATS_INVALID_SUBSCRIPTION);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_CloseSubRelease(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsThread          *t        = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int64_t             start, end;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");

    start = nats_Now();

    if (s == NATS_OK)
        s = natsThread_Create(&t, _closeConnWithDelay, (void*) nc);
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 10000);

    end = nats_Now();

    test("Test that NexMsg was kicked out properly: ");
    testCond((s != NATS_TIMEOUT)
             && ((end - start) <= 1000));

    natsThread_Join(t);
    natsThread_Destroy(t);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_IsValidSubscriber(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");

    test("Sub is valid: ");
    testCond((s == NATS_OK)
             && natsSubscription_IsValid(sub));

    for (int i=0; (s == NATS_OK) && (i<10); i++)
        s = natsConnection_PublishString(nc, "foo", "hello");

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    test("Received msg ok: ")
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 200);
    testCond((s == NATS_OK) && (msg != NULL));

    natsMsg_Destroy(msg);

    if (s == NATS_OK)
        s = natsSubscription_Unsubscribe(sub);

    test("Received msg should fail after unsubscribe: ")
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 200);
    testCond(s != NATS_OK);

    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_SlowSubscriber(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int64_t             start, end;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_SubscribeSync(&sub, nc, "foo");

    for (int i=0;
        (s == NATS_OK) && (i < (NATS_OPTS_DEFAULT_MAX_PENDING_MSGS + 100)); i++)
    {
        s = natsConnection_PublishString(nc, "foo", "hello");
    }

    test("Check flush returns before timeout: ");
    start = nats_Now();

    (void) natsConnection_FlushTimeout(nc, 5000);

    end = nats_Now();

    testCond((end - start) < 5000);

    // Make sure NextMsg returns an error to indicate slow consumer
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 200);

    test("NextMsg should report error: ");
    testCond(s != NATS_OK);

    natsMsg_Destroy(msg);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_SlowAsyncSubscriber(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int64_t             start, end;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if ( s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 7;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);

    for (int i=0;
        (s == NATS_OK) && (i < (NATS_OPTS_DEFAULT_MAX_PENDING_MSGS + 100)); i++)
    {
        s = natsConnection_PublishString(nc, "foo", "hello");
    }

    test("Check flush returns before timeout: ");
    start = nats_Now();

    s = natsConnection_FlushTimeout(nc, 5000);

    end = nats_Now();

    testCond((end - start) < 5000);

    test("Flush should report an error: ");
    testCond(s != NATS_OK);

    // Release the sub
    natsMutex_Lock(arg.m);

    // Unblock the wait
    arg.closed = true;

    // And destroy the subscription here so that the next msg callback
    // is not invoked.
    natsSubscription_Destroy(sub);

    natsCondition_Signal(arg.c);
    natsMutex_Unlock(arg.m);

    natsConnection_Destroy(nc);

    if (valgrind)
        nats_Sleep(1000);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
_asyncErrCb(natsConnection *nc, natsSubscription *sub, natsStatus err, void* closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;

    natsMutex_Lock(arg->m);

    if (arg->sum == 1)
    {
        natsMutex_Unlock(arg->m);
        return;
    }

    arg->sum = 1;

    if (sub != arg->sub)
        arg->status = NATS_ERR;

    if ((arg->status == NATS_OK) && (err != NATS_SLOW_CONSUMER))
        arg->status = NATS_ERR;

    arg->closed = true;
    arg->done = true;
    natsCondition_Signal(arg->c);

    natsMutex_Unlock(arg->m);
}

static void
test_AsyncErrHandler(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsSubscription    *sub      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 7;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetURL(opts, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsOptions_SetMaxPendingMsgs(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetErrorHandler(opts, _asyncErrCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test AsyncErrHandler");

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "async_test", _recvTestString, (void*) &arg);

    natsMutex_Lock(arg.m);
    arg.sub = sub;
    natsMutex_Unlock(arg.m);

    for (int i=0;
        (s == NATS_OK) && (i < (opts->maxPendingMsgs + 100)); i++)
    {
        s = natsConnection_PublishString(nc, "async_test", "hello");
    }
    if (s == NATS_OK)
        (void) natsConnection_Flush(nc);

    // Wait for async err callback
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.done)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);

    test("Aync fired properly, and all checks are good: ");
    testCond((s == NATS_OK)
             && arg.done
             && arg.closed
             && (arg.status == NATS_OK));

    natsOptions_Destroy(opts);
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
_responseCb(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;

    natsMutex_Lock(arg->m);

    arg->closed = true;
    arg->done = true;
    natsCondition_Signal(arg->c);

    natsMutex_Unlock(arg->m);

    natsMsg_Destroy(msg);
}

static void
_startCb(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    struct threadArg    *arg = (struct threadArg*) closure;
    natsInbox           *response = NULL;
    natsStatus          s;

    natsMutex_Lock(arg->m);

    s = natsInbox_Create(&response);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&(arg->sub), nc, response, _responseCb, (void*) arg);
    if (s == NATS_OK)
        s = natsConnection_PublishRequestString(nc, "helper", response, "Help Me!");

    if (s != NATS_OK)
        arg->status = s;

    // We need to destroy the inbox. It has been copied by the
    // natsConnection_Subscribe() call.
    natsInbox_Destroy(response);

    natsMutex_Unlock(arg->m);

    natsMsg_Destroy(msg);
}

static void
test_AsyncSubscriberStarvation(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsSubscription    *sub2     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 4;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "helper",
                                     _recvTestString, (void*) &arg);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub2, nc, "start",
                                     _startCb, (void*) &arg);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "start", "Begin");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    // Wait for end of test
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.done)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);

    test("Test not stalled in cb waiting for other cb: ");
    testCond((s == NATS_OK)
             && arg.done
             && (arg.status == NATS_OK));

    natsSubscription_Destroy(arg.sub);
    natsSubscription_Destroy(sub);
    natsSubscription_Destroy(sub2);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_AsyncSubscriberOnClose(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsSubscription    *sub2     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 seen      = 0;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.status = NATS_OK;
    arg.control= 8;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo",
                                     _recvTestString, (void*) &arg);

    for (int i=0; (s == NATS_OK) && (i < 10); i++)
        s = natsConnection_PublishString(nc, "foo", "Hello World");

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    if (s == NATS_OK)
    {
        nats_Sleep(10);
        natsConnection_Close(nc);
    }

    // Release callbacks
    natsMutex_Lock(arg.m);
    arg.closed = true;
    natsCondition_Broadcast(arg.c);
    natsMutex_Unlock(arg.m);

    // Wait for some time
    nats_Sleep(100);

    natsMutex_Lock(arg.m);
    seen = arg.sum;
    natsMutex_Unlock(arg.m);

    test("Make sure only one callback fired: ");
    testCond(seen == 1);

    natsSubscription_Destroy(sub);
    natsSubscription_Destroy(sub2);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_NextMsgCallOnAsyncSub(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, NULL);

    test("NextMsg should fail for async sub: ");
    if (s == NATS_OK)
        s = natsSubscription_NextMsg(&msg, sub, 1000);
    testCond((s != NATS_OK) && (msg == NULL));

    natsSubscription_Destroy(sub);

    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_NoDelay(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsSubscription    *sub      = NULL;
    natsMsg             *msg      = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    arg;
    int                 i;
    int64_t             start;
    int64_t             withDelay;
    int64_t             withNoDelay;
    int                 count = 3000;

    if (valgrind)
    {
        test("Skipped when running with valgrind: ");
        testCond(true);
        return;
    }

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test");

    serverPid = _startServer(NATS_DEFAULT_URL, NULL, true);
    CHECK_SERVER_STARTED(serverPid);

    arg.control = 4;
    arg.string  = "reply";

    s = natsConnection_ConnectTo(&nc, NATS_DEFAULT_URL);
    if (s == NATS_OK)
        s = natsConnection_Subscribe(&sub, nc, "foo", _recvTestString, (void*) &arg);

    test("With delay: ");
    start = nats_Now();
    for (i=0; (s == NATS_OK) && (i<count); i++)
    {
        s = natsConnection_Request(&msg, nc, "foo", "help", 4, 1000);
        if (s == NATS_OK)
            natsMsg_Destroy(msg);
    }
    withDelay = nats_Now() - start;

    natsMutex_Lock(arg.m);
    testCond((s == NATS_OK) && (arg.status == NATS_OK));
    natsMutex_Unlock(arg.m);

    if (s == NATS_OK)
        s = natsSubscription_NoDeliveryDelay(sub);

    test("With no delay faster for req-reply: ");
    start = nats_Now();
    for (i=0; (s == NATS_OK) && (i<count); i++)
    {
        s = natsConnection_Request(&msg, nc, "foo", "help", 4, 1000);
        if (s == NATS_OK)
            natsMsg_Destroy(msg);
    }
    withNoDelay = nats_Now() - start;

    natsMutex_Lock(arg.m);
    testCond((s == NATS_OK) && (arg.status == NATS_OK) && (withNoDelay < withDelay));
    natsMutex_Unlock(arg.m);

    natsSubscription_Destroy(sub);

    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);
}

static void
test_ServersOption(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    char                buffer[128];
    int                 serversCount;

    serversCount = sizeof(testServers) / sizeof(char *);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    test("Connect should fail with NATS_NO_SERVER: ");
    s = natsConnection_Connect(&nc, opts);
    testCond((nc == NULL) && (s == NATS_NO_SERVER));

    test("Connect with list of servers should fail with NATS_NO_SERVER: ");
    s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond((nc == NULL) && (s == NATS_NO_SERVER));

    serverPid = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid);

    buffer[0] = '\0';
    test("Can connect to first: ")
    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));
    testCond((s == NATS_OK)
             && (buffer[0] != '\0')
             && (strcmp(buffer, testServers[0]) == 0));

    natsConnection_Destroy(nc);
    nc = NULL;

    _stopServer(serverPid);
    serverPid = NATS_INVALID_PID;

    // Make sure we can connect to a non first server if running
    serverPid = _startServer("nats://localhost:1223", "-p 1223", true);
    CHECK_SERVER_STARTED(serverPid);

    buffer[0] = '\0';
    test("Can connect to second: ")
    s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));
    testCond((s == NATS_OK)
             && (buffer[0] != '\0')
             && (strcmp(buffer, testServers[1]) == 0));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _stopServer(serverPid);
}

static void
test_AuthServers(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid1= NATS_INVALID_PID;
    natsPid             serverPid2= NATS_INVALID_PID;
    char                buffer[128];
    const char          *plainServers[] = {"nats://localhost:1222",
                                           "nats://localhost:1224"};
    const char          *authServers[] = {"nats://localhost:1222",
                                          "nats://ivan:foo@localhost:1224"};
    int serversCount = 2;

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, plainServers, serversCount);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid1 = _startServer("nats://localhost:1222", "-p 1222 --user ivan --pass foo", false);
    CHECK_SERVER_STARTED(serverPid1);

    serverPid2 = _startServer("nats://localhost:1224", "-p 1224 --user ivan --pass foo", false);
    if (serverPid2 == NATS_INVALID_PID)
    {
        _stopServer(serverPid1);
        FAIL("Unable to start or verify that the server was started!");
    }

    nats_Sleep(500);

    test("Connect fails due to auth error: ");
    s = natsConnection_Connect(&nc, opts);
    testCond((s == NATS_CONNECTION_AUTH_FAILED) && (nc == NULL));

    buffer[0] = '\0';
    test("Connect succeeds with correct servers list: ")
    s = natsOptions_SetServers(opts, authServers, serversCount);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond((s == NATS_OK)
             && (nc != NULL)
             && (natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer)) == NATS_OK)
             && (strcmp(buffer, authServers[1]) == 0));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _stopServer(serverPid1);
    _stopServer(serverPid2);
}

static void
test_AuthFailToReconnect(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid1= NATS_INVALID_PID;
    natsPid             serverPid2= NATS_INVALID_PID;
    natsPid             serverPid3= NATS_INVALID_PID;
    char                buffer[64];
    const char          *servers[] = {"nats://localhost:22222",
                                      "nats://localhost:22223",
                                      "nats://localhost:22224"};
    struct threadArg    args;

    int serversCount = 3;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, servers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, (void*) &args);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);

    if (s != NATS_OK)
        FAIL("Unable to setup test");

    serverPid1 = _startServer("nats://localhost:22222", "-p 22222", false);
    CHECK_SERVER_STARTED(serverPid1);

    serverPid2 = _startServer("nats://localhost:22223", "-p 22223 --user ivan --pass foo", false);
    if (serverPid2 == NATS_INVALID_PID)
    {
        _stopServer(serverPid1);
        FAIL("Unable to start or verify that the server was started!");
    }

    serverPid3 = _startServer("nats://localhost:22224", "-p 22224", false);
    if (serverPid3 == NATS_INVALID_PID)
    {
        _stopServer(serverPid1);
        _stopServer(serverPid2);
        FAIL("Unable to start or verify that the server was started!");
    }

    nats_Sleep(1000);

    test("Connect should succeed: ");
    s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_OK);

    // Stop the server which will trigger the reconnect
    _stopServer(serverPid1);
    serverPid1 = NATS_INVALID_PID;


    // The client will try to connect to the second server, and that
    // should fail. It should then try to connect to the third and succeed.

    // Wait for the reconnect CB.
    test("Reconnect callback should be triggered: ")
    natsMutex_Lock(args.m);
    while ((s == NATS_OK)
           && !(args.reconnected))
    {
        s = natsCondition_TimedWait(args.c, args.m, 5000);
    }
    natsMutex_Unlock(args.m);
    testCond((s == NATS_OK) && args.reconnected);

    test("Connection should not be closed: ");
    testCond(natsConnection_IsClosed(nc) == false);

    buffer[0] = '\0';
    s = natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer));

    test("Should have connected to third server: ");
    testCond((s == NATS_OK) && (buffer[0] != '\0')
             && (strcmp(buffer, servers[2]) == 0));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid2);
    _stopServer(serverPid3);
}

static void
test_BasicClusterReconnect(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid1= NATS_INVALID_PID;
    natsPid             serverPid2= NATS_INVALID_PID;
    char                buffer[128];
    int                 serversCount;
    int64_t             reconnectTimeStart, reconnectTime;
    struct threadArg    arg;

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serversCount = sizeof(testServers) / sizeof(char*);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid1 = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid1);

    serverPid2 = _startServer("nats://localhost:1224", "-p 1224", true);
    if (serverPid2 == NATS_INVALID_PID)
    {
        _stopServer(serverPid1);
        FAIL("Unable to start or verify that the server was started!");
    }

    s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid1);

    // wait for disconnect
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);

    reconnectTimeStart = nats_Now();

    // wait for reconnect
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.reconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 3000);
    natsMutex_Unlock(arg.m);

    test("Check connected to the right server: ");
    testCond((s == NATS_OK)
             && (natsConnection_GetConnectedUrl(nc, buffer, sizeof(buffer)) == NATS_OK)
             && (strcmp(buffer, testServers[2]) == 0));

    // Make sure we did not wait on reconnect for default time.
    // Reconnect should be fast since it will be a switch to the
    // second server and not be dependent on server restart time.
    reconnectTime = nats_Now() - reconnectTimeStart;

    test("Check reconnect time did not take too long: ");
#if _WIN32
    testCond(reconnectTime <= 2100);
#else
    testCond(reconnectTime <= 100);
#endif

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    if (valgrind)
        nats_Sleep(1000);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid2);
}

#define NUM_CLIENTS (100)

struct hashCount
{
    int count;

};

static void
test_HotSpotReconnect(void)
{
    natsStatus          s;
    natsConnection      *nc[NUM_CLIENTS];
    natsOptions         *opts     = NULL;
    natsPid             serverPid1= NATS_INVALID_PID;
    natsPid             serverPid2= NATS_INVALID_PID;
    natsPid             serverPid3= NATS_INVALID_PID;
    char                buffer[128];
    int                 serversCount;
    natsStrHash         *cs = NULL;
    struct threadArg    arg;
    struct hashCount    *count = NULL;

#if _WIN32
    test("Skip when running on Windows: ");
    testCond(true);
    return;
#endif

    memset(nc, 0, sizeof(nc));

    s = natsStrHash_Create(&cs, 4);
    if (s == NATS_OK)
        s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serversCount = sizeof(testServers) / sizeof(char*);

    serverPid1 = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid1);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, (void*) &arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    for (int i=0; (s == NATS_OK) && (i<NUM_CLIENTS); i++)
    {
        s = natsConnection_Connect(&(nc[i]), opts);
        if (s == NATS_OK)
            s = natsConnection_GetConnectedUrl(nc[i], buffer, sizeof(buffer));
        if ((s == NATS_OK)
            && (strcmp(buffer, testServers[0]) != 0))
        {
            s = NATS_ERR;
        }
    }
    if (s == NATS_OK)
    {
        serverPid2 = _startServer("nats://localhost:1224", "-p 1224", true);
        serverPid3 = _startServer("nats://localhost:1226", "-p 1226", true);

        if ((serverPid2 == NATS_INVALID_PID)
            || (serverPid3 == NATS_INVALID_PID))
        {
            _stopServer(serverPid1);
            _stopServer(serverPid2);
            _stopServer(serverPid3);
            FAIL("Unable to start or verify that the server was started!");
        }
    }

    _stopServer(serverPid1);

    // Wait on all reconnects
    test("Check all reconnected: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && (arg.reconnects != NUM_CLIENTS))
        s = natsCondition_TimedWait(arg.c, arg.m, 10000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.reconnects == NUM_CLIENTS);

    // Walk the clients and calculate how many of each..
    for (int i=0; (s == NATS_OK) && (i<NUM_CLIENTS); i++)
    {
        if (nc[i] == NULL)
        {
            s = NATS_ERR;
            break;
        }

        buffer[0] = '\0';
        s = natsConnection_GetConnectedUrl(nc[i], buffer, sizeof(buffer));
        if (s == NATS_OK)
        {
            count = (struct hashCount*) natsStrHash_Get(cs, buffer);
            if (count == NULL)
            {
                count = (struct hashCount*) calloc(1, sizeof(struct hashCount));
                if (count == NULL)
                    s = NATS_NO_MEMORY;
            }

            if (s == NATS_OK)
            {
                count->count++;
                s = natsStrHash_Set(cs, buffer, true, (void*) count, NULL);
            }
        }

        natsConnection_Close(nc[i]);
    }

    test("Check correct number of servers: ");
    testCond((s == NATS_OK) && (natsStrHash_Count(cs) == 2));

    if (s == NATS_OK)
    {
        // numClients  = 100
        // numServers  = 2
        // expected    = numClients / numServers
        // v           = expected * 0.3

        natsStrHashIter     iter;
        struct hashCount    *val;
        int                 total;
        int                 delta;
        int                 v = (int) (((float)NUM_CLIENTS / 2) * 0.30);

        natsStrHashIter_Init(&iter, cs);
        while (natsStrHashIter_Next(&iter, NULL, (void**)&val))
        {
            total = val->count;

            delta = ((NUM_CLIENTS / 2) - total);
            if (delta < 0)
                delta *= -1;

            if (delta > v)
                s = NATS_ERR;

            free(val);
        }

        test("Check variance: ");
        testCond(s == NATS_OK);
    }

    for (int i=0; i<NUM_CLIENTS; i++)
        natsConnection_Destroy(nc[i]);

    natsStrHash_Destroy(cs);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid2);
    _stopServer(serverPid3);
}

static void
test_ProperReconnectDelay(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 serversCount;
    struct threadArg    arg;

#if _WIN32
    test("Skip when running on Windows: ");
    testCond(true);
    return;
#endif

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serversCount = sizeof(testServers) / sizeof(char*);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    // wait for disconnect
    test("Wait for disconnect: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.disconnected);

    // Wait, want to make sure we don't spin on reconnect to non-existent servers.
    nats_Sleep(1000);

    // Make sure we are still reconnecting..
    test("ClosedCB should not be invoked: ")
    natsMutex_Lock(arg.m);
    testCond(arg.closed == false);
    natsMutex_Unlock(arg.m);

    test("Should still be reconnecting: ");
    testCond(natsConnection_Status(nc) == RECONNECTING);

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    // Now that the connection is destroyed, the callback will be invoked.
    // Make sure that we wait until then before destorying 'arg'.
    natsMutex_Lock(arg.m);
    s = NATS_OK;
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);

    _destroyDefaultThreadArgs(&arg);
}

static void
test_ProperFalloutAfterMaxAttempts(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 serversCount;
    struct threadArg    arg;

#if _WIN32
    test("Skip when running on Windows: ");
    testCond(true);
    return;
#endif

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serversCount = sizeof(testServers) / sizeof(char*);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 5);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 25);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    // wait for disconnect
    test("Wait for disconnected: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.disconnected);

    // wait for closed
    test("Wait for closed: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.closed);

    test("Connection should be closed: ")
    testCond((s == NATS_OK)
             && natsConnection_IsClosed(nc));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);
}

static void
test_TimeoutOnNoServer(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 serversCount;
    int64_t             startWait, timedWait;
    struct threadArg    arg;

#if _WIN32
    test("Skip when running on Windows: ");
    testCond(true);
    return;
#endif

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    serversCount = sizeof(testServers) / sizeof(char*);

    // 1000 milliseconds total time wait
    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetMaxReconnect(opts, 10);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 100);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetClosedCB(opts, _closedCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);

    _stopServer(serverPid);

    // wait for disconnect
    test("Wait for disconnected: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.disconnected)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.disconnected);

    startWait = nats_Now();

    // wait for closed
    test("Wait for closed: ");
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && !arg.closed)
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && arg.closed);

    timedWait = nats_Now() - startWait;

    // Use 500ms as variable time delta
    test("Check wait time for closed cb: ");
    testCond(timedWait <= ((opts->maxReconnect * opts->reconnectWait) + 500));

    natsOptions_Destroy(opts);
    natsConnection_Destroy(nc);

    _destroyDefaultThreadArgs(&arg);
}

static void
test_PingReconnect(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    int                 serversCount;
    int64_t             disconnectedAt, reconnectedAt, pingCycle;
    struct threadArg    arg;

#if _WIN32
    test("Skip when running on Windows: ");
    testCond(true);
    return;
#endif

    s = _createDefaultThreadArgsForCbTests(&arg);
    if (s != NATS_OK)
        FAIL("Unable to setup test!");

    arg.control = 9;

    serversCount = sizeof(testServers) / sizeof(char*);

    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_SetNoRandomize(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectWait(opts, 200);
    if (s == NATS_OK)
        s = natsOptions_SetPingInterval(opts, 50);
    if (s == NATS_OK)
        s = natsOptions_SetMaxPingsOut(opts, -1);
    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, testServers, serversCount);
    if (s == NATS_OK)
        s = natsOptions_SetDisconnectedCB(opts, _disconnectedCb, (void*) &arg);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, (void*) &arg);

    if (s != NATS_OK)
        FAIL("Unable to create options for test ServerOptions");

    serverPid = _startServer("nats://localhost:1222", "-p 1222", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsConnection_Connect(&nc, opts);

    test("Pings cause reconnects: ")
    natsMutex_Lock(arg.m);
    while ((s == NATS_OK) && (arg.reconnects != 4))
        s = natsCondition_TimedWait(arg.c, arg.m, 2000);
    natsMutex_Unlock(arg.m);
    testCond((s == NATS_OK) && (arg.reconnects == 4));

    natsConnection_Destroy(nc);

    for (int i=0; i<(4-1); i++)
    {
        disconnectedAt = arg.disconnectedAt[i];
        reconnectedAt = arg.reconnectedAt[i];

        pingCycle = reconnectedAt - disconnectedAt;
        if (pingCycle > 2 * opts->pingInterval)
        {
            s = NATS_ERR;
            break;
        }
    }

    test("Reconnect due to ping cycle correct: ");
    testCond(s == NATS_OK);

    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&arg);

    _stopServer(serverPid);

}

static void
test_Version(void)
{
    const char *str = NULL;

    test("Compatibility: ");
    testCond(nats_CheckCompatibility() == true);

    test("Version string: ");
    str = nats_GetVersion();
    testCond((str != NULL)
             && (strcmp(str, LIB_NATS_VERSION_STRING) == 0));

    test("Version number: ");
    testCond(nats_GetVersionNumber() == LIB_NATS_VERSION_NUMBER);
}

static void
_testGetLastErrInThread(void *arg)
{
    natsStatus  getLastErrSts;
    natsOptions *opts = NULL;
    const char  *getLastErr = NULL;

    test("Check that new thread has get last err clear: ");
    getLastErr = nats_GetLastError(&getLastErrSts);
    testCond((getLastErr == NULL) && (getLastErrSts == NATS_OK));

    natsOptions_Destroy(opts);
}

static void
test_GetLastError(void)
{
    natsStatus  s, getLastErrSts;
    natsOptions *opts = NULL;
    const char  *getLastErr = NULL;
    natsThread  *t = NULL;
    char        stackBuf[256];
    FILE        *stackFile = NULL;

    stackBuf[0] = '\0';

    test("Check GetLastError returns proper status: ");
    s = natsOptions_Create(&opts);
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "");

    getLastErr = nats_GetLastError(&getLastErrSts);

    testCond((s == getLastErrSts)
             && (getLastErr != NULL)
             && (strstr(getLastErr, "Invalid") != NULL));

    natsOptions_Destroy(opts);

    test("Check GetLastErrorStack with invalid args: ");
    s = nats_GetLastErrorStack(NULL, 10);
    if (s != NATS_OK)
        s = nats_GetLastErrorStack(stackBuf, 0);
    testCond(s == NATS_INVALID_ARG);

    test("Check GetLastErrorStack returns proper insufficient buffer: ");
    s = nats_GetLastErrorStack(stackBuf, 10);
    testCond(s == NATS_INSUFFICIENT_BUFFER);

    test("Check GetLastErrorStack: ");
    s = nats_GetLastErrorStack(stackBuf, sizeof(stackBuf));
    testCond((s == NATS_OK)
             && (strlen(stackBuf) > 0)
             && (strstr(stackBuf, "natsOptions_LoadCATrustedCertificates") != NULL));

    test("Check PrintStack: ");
    stackBuf[0] = '\0';
    stackFile = fopen("stack.txt", "w");
    if (stackFile == NULL)
        FAIL("Unable to create a file for print stack test");

    s = NATS_OK;
    nats_PrintLastErrorStack(stackFile);
    fclose(stackFile);
    stackFile = fopen("stack.txt", "r");
    s = (fgets(stackBuf, sizeof(stackBuf), stackFile) == NULL ? NATS_ERR : NATS_OK);
    if ((s == NATS_OK)
        && ((strlen(stackBuf) == 0)
            || (strstr(stackBuf, "Invalid Argument") == NULL)))
    {
        s = NATS_ERR;
    }
    s = (fgets(stackBuf, sizeof(stackBuf), stackFile) == NULL ? NATS_ERR : NATS_OK);
    if (s == NATS_OK)
        s = (fgets(stackBuf, sizeof(stackBuf), stackFile) == NULL ? NATS_ERR : NATS_OK);
    if ((s == NATS_OK)
        && ((strlen(stackBuf) == 0)
            || (strstr(stackBuf, "natsOptions_LoadCATrustedCertificates") == NULL)))
    {
        s = NATS_ERR;
    }
    testCond(s == NATS_OK);
    fclose(stackFile);
    remove("stack.txt");

    test("Check the error not cleared until next error occurs: ");
    s = natsOptions_Create(&opts);

    getLastErr = nats_GetLastError(&getLastErrSts);

    testCond((s == NATS_OK)
             && (getLastErrSts != NATS_OK)
             && (getLastErr != NULL)
             && (strstr(getLastErr, "Invalid") != NULL));

    s = natsThread_Create(&t, _testGetLastErrInThread, NULL);
    if (s == NATS_OK)
    {
        natsThread_Join(t);
        natsThread_Destroy(t);
    }

    natsOptions_Destroy(opts);
}

static void
test_SSLBasic(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to setup test!");

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check that connect fails if no SSL options: ");
    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_SECURE_CONNECTION_REQUIRED);

    test("Check connects OK with SSL options: ");
    s = natsOptions_SetSecure(opts, true);

    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    test("Check reconnects OK: ");
    _stopServer(serverPid);

    nats_Sleep(100);

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !(args.reconnected))
        s = natsCondition_TimedWait(args.c, args.m, 2000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
test_SSLVerify(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to create reconnect options!");

    serverPid = _startServer("nats://localhost:4443", "-config tlsverify.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check that connect fails if no SSL certs: ");
    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_SSL_ERROR);

    test("Check that connect succeeds with proper certs: ");
    s = natsOptions_LoadCertificatesChain(opts,
                                          "certs/client-cert.pem",
                                          "certs/client-key.pem");
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK);
    printf("@@IK: LastError: %s\n", nats_GetLastError(NULL));

    test("Check reconnects OK: ");
    _stopServer(serverPid);

    nats_Sleep(100);

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !(args.reconnected))
        s = natsCondition_TimedWait(args.c, args.m, 2000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
test_SSLVerifyHostname(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to create reconnect options!");

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check that connect fails if no wrong hostname: ");
    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetExpectedHostname(opts, "foo");
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s == NATS_SSL_ERROR);

    test("Check that connect succeeds with correct hostname: ");
    s = natsOptions_SetExpectedHostname(opts, "localhost");
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK);

    test("Check reconnects OK: ");
    _stopServer(serverPid);

    nats_Sleep(100);

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !(args.reconnected))
        s = natsCondition_TimedWait(args.c, args.m, 2000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
test_SSLCiphers(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        opts = _createReconnectOptions();
    if (opts == NULL)
        FAIL("Unable to setup test!");

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check that connect fails if no improper ciphers: ");
    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetCiphers(opts, "-ALL:RSA");
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    testCond(s != NATS_OK);

    test("Check connects OK with proper ciphers: ");
    s = natsOptions_SetSecure(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetCiphers(opts, "-ALL:HIGH");
    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    test("Check reconnects OK: ");
    _stopServer(serverPid);

    nats_Sleep(100);

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !(args.reconnected))
        s = natsCondition_TimedWait(args.c, args.m, 2000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_PublishString(nc, "foo", "test");
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);
    testCond(s == NATS_OK)

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
_sslMT(void *closure)
{
    struct threadArg    *args = (struct threadArg*) closure;
    natsStatus          s = NATS_OK;
    natsConnection      *nc = NULL;
    natsSubscription    *sub = NULL;
    natsMsg             *msg = NULL;
    char                subj[64];
    int                 i;
    const char          *msgPayload = "this is a test payload";

    natsMutex_Lock(args->m);
    snprintf(subj, sizeof(subj), "foo.%d", ++(args->sum));
    while (!(args->current) && (s == NATS_OK))
        s = natsCondition_TimedWait(args->c, args->m, 2000);
    natsMutex_Unlock(args->m);

    for (i=0; (s == NATS_OK) && (i < 100); i++)
    {
        s = natsConnection_Connect(&nc, args->opts);
        if (s == NATS_OK)
            s = natsConnection_SubscribeSync(&sub, nc, subj);
        if (s == NATS_OK)
            s = natsConnection_PublishString(nc, subj, msgPayload);
        if (s == NATS_OK)
            s = natsSubscription_NextMsg(&msg, sub, 2000);
        if (s == NATS_OK)
        {
            if (strcmp(natsMsg_GetData(msg), msgPayload) != 0)
                s = NATS_ERR;
        }

        natsMsg_Destroy(msg);
        msg = NULL;
        natsSubscription_Destroy(sub);
        sub = NULL;
        natsConnection_Destroy(nc);
        nc = NULL;
    }

    if (s != NATS_OK)
    {
        natsMutex_Lock(args->m);
        if (args->status == NATS_OK)
            args->status = s;
        natsMutex_Unlock(args->m);
    }
}

#define SSL_THREADS (3)

static void
test_SSLMultithreads(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    natsThread          *t[SSL_THREADS];
    int                 i;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
        s = natsOptions_Create(&opts);
    if (opts == NULL)
        FAIL("Unable to setup test!");

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetExpectedHostname(opts, "localhost");

    args.opts = opts;

    for (i=0; (s == NATS_OK) && (i<SSL_THREADS); i++)
        s = natsThread_Create(&t[i], _sslMT, &args);

    test("Create connections from multiple threads using same ssl ctx: ");
    natsMutex_Lock(args.m);
    args.current = true;
    natsCondition_Broadcast(args.c);
    natsMutex_Unlock(args.m);

    for (i=0; i<SSL_THREADS; i++)
    {
        if (t[i] == NULL)
            continue;

        natsThread_Join(t[i]);
        natsThread_Destroy(t[i]);
    }

    natsMutex_Lock(args.m);
    s = args.status;
    natsMutex_Unlock(args.m);

    testCond(s == NATS_OK);

    natsOptions_Destroy(opts);

    if (valgrind)
        nats_Sleep(1000);

    _destroyDefaultThreadArgs(&args);

    _stopServer(serverPid);
}

static void
test_SSLConnectVerboseOption(void)
{
    natsStatus          s;
    natsConnection      *nc       = NULL;
    natsOptions         *opts     = NULL;
    natsPid             serverPid = NATS_INVALID_PID;
    struct threadArg    args;

    s = _createDefaultThreadArgsForCbTests(&args);
    if (s == NATS_OK)
    {
        opts = _createReconnectOptions();
        if (opts == NULL)
            s = NATS_ERR;
    }
    if (s == NATS_OK)
        s = natsOptions_SetVerbose(opts, true);
    if (s == NATS_OK)
        s = natsOptions_SetReconnectedCB(opts, _reconnectedCb, &args);
    if (opts == NULL)
        FAIL("Unable to setup test!");

    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    s = natsOptions_SetURL(opts, "nats://localhost:4443");
    if (s == NATS_OK)
        s = natsOptions_SetSecure(opts, true);
    // For test purposes, we provide the CA trusted certs
    if (s == NATS_OK)
        s = natsOptions_LoadCATrustedCertificates(opts, "certs/ca.pem");
    if (s == NATS_OK)
        s = natsOptions_SetExpectedHostname(opts, "localhost");

    test("Check that SSL connect OK when Verbose set: ");

    if (s == NATS_OK)
        s = natsConnection_Connect(&nc, opts);
    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    testCond(s == NATS_OK);

    _stopServer(serverPid);
    serverPid = _startServer("nats://localhost:4443", "-config tls.conf", true);
    CHECK_SERVER_STARTED(serverPid);

    test("Check that SSL reconnect OK when Verbose set: ");

    natsMutex_Lock(args.m);
    while ((s == NATS_OK) && !args.reconnected)
        s = natsCondition_TimedWait(args.c, args.m, 5000);
    natsMutex_Unlock(args.m);

    if (s == NATS_OK)
        s = natsConnection_Flush(nc);

    testCond(s == NATS_OK);

    natsConnection_Destroy(nc);
    natsOptions_Destroy(opts);

    _destroyDefaultThreadArgs(&args);

    if (valgrind)
        nats_Sleep(1000);

    _stopServer(serverPid);
}

typedef void (*testFunc)(void);

typedef struct __testInfo
{
    const char  *name;
    testFunc    func;

} testInfo;

static testInfo allTests[] =
{
    // Building blocks
    {"Version",                         test_Version},
    {"natsNowAndSleep",                 test_natsNowAndSleep},
    {"natsAllocSprintf",                test_natsAllocSprintf},
    {"natsStrCaseStr",                  test_natsStrCaseStr},
    {"natsBuffer",                      test_natsBuffer},
    {"natsParseInt64",                  test_natsParseInt64},
    {"natsParseControl",                test_natsParseControl},
    {"natsMutex",                       test_natsMutex},
    {"natsThread",                      test_natsThread},
    {"natsCondition",                   test_natsCondition},
    {"natsTimer",                       test_natsTimer},
    {"natsRandomize",                   test_natsRandomize},
    {"natsUrl",                         test_natsUrl},
    {"natsCreateStringFromBuffer",      test_natsCreateStringFromBuffer},
    {"natsHash",                        test_natsHash},
    {"natsHashing",                     test_natsHashing},
    {"natsStrHash",                     test_natsStrHash},
    {"natsInbox",                       test_natsInbox},
    {"natsOptions",                     test_natsOptions},
    {"natsSock_ReadLine",               test_natsSock_ReadLine},

    // Package Level Tests

    {"ReconnectServerStats",            test_ReconnectServerStats},
    {"ParseStateReconnectFunctionality",test_ParseStateReconnectFunctionality},
    {"ServersRandomize",                test_ServersRandomize},
    {"SelectNextServer",                test_SelectNextServer},

    // Public API Tests

    {"DefaultConnection",               test_DefaultConnection},
    {"UseDefaultURLIfNoServerSpecified",test_UseDefaultURLIfNoServerSpecified},
    {"ConnectionWithNULLOptions",       test_ConnectionWithNullOptions},
    {"ConnectionStatus",                test_ConnectionStatus},
    {"ConnClosedCB",                    test_ConnClosedCB},
    {"CloseDisconnectedCB",             test_CloseDisconnectedCB},
    {"ServerStopDisconnectedCB",        test_ServerStopDisconnectedCB},
    {"ClosedConnections",               test_ClosedConnections},
    {"ConnectVerboseOption",            test_ConnectVerboseOption},
    {"ReconnectTotalTime",              test_ReconnectTotalTime},
    {"ReconnectDisallowedFlags",        test_ReconnectDisallowedFlags},
    {"ReconnectAllowedFlags",           test_ReconnectAllowedFlags},
    {"BasicReconnectFunctionality",     test_BasicReconnectFunctionality},
    {"ExtendedReconnectFunctionality",  test_ExtendedReconnectFunctionality},
    {"QueueSubsOnReconnect",            test_QueueSubsOnReconnect},
    {"IsClosed",                        test_IsClosed},
    {"IsReconnectingAndStatus",         test_IsReconnectingAndStatus},

    {"ErrOnConnectAndDeadlock",         test_ErrOnConnectAndDeadlock},
    {"ErrOnMaxPayloadLimit",            test_ErrOnMaxPayloadLimit},

    {"Auth",                            test_Auth},
    {"AuthFailNoDisconnectCB",          test_AuthFailNoDisconnectCB},
    {"ConnectedServer",                 test_ConnectedServer},
    {"MultipleClose",                   test_MultipleClose},
    {"SimplePublish",                   test_SimplePublish},
    {"SimplePublishNoData",             test_SimplePublishNoData},
    {"AsyncSubscribe",                  test_AsyncSubscribe},
    {"SyncSubscribe",                   test_SyncSubscribe},
    {"PubSubWithReply",                 test_PubSubWithReply},
    {"Flush",                           test_Flush},
    {"QueueSubscriber",                 test_QueueSubscriber},
    {"ReplyArg",                        test_ReplyArg},
    {"SyncReplyArg",                    test_SyncReplyArg},
    {"Unsubscribe",                     test_Unsubscribe},
    {"DoubleUnsubscribe",               test_DoubleUnsubscribe},
    {"RequestTimeout",                  test_RequestTimeout},
    {"Request",                         test_Request},
    {"RequestNoBody",                   test_RequestNoBody},
    {"FlushInCb",                       test_FlushInCb},
    {"ReleaseFlush",                    test_ReleaseFlush},
    {"FlushErrOnDisconnect",            test_FlushErrOnDisconnect},
    {"Inbox",                           test_Inbox},
    {"Stats",                           test_Stats},
    {"BadSubject",                      test_BadSubject},
    {"ClientAsyncAutoUnsub",            test_ClientAsyncAutoUnsub},
    {"ClientSyncAutoUnsub",             test_ClientSyncAutoUnsub},
    {"NextMsgOnClosedSub",              test_NextMsgOnClosedSub},
    {"CloseSubRelease",                 test_CloseSubRelease},
    {"IsValidSubscriber",               test_IsValidSubscriber},
    {"SlowSubscriber",                  test_SlowSubscriber},
    {"SlowAsyncSubscriber",             test_SlowAsyncSubscriber},
    {"AsyncErrHandler",                 test_AsyncErrHandler},
    {"AsyncSubscriberStarvation",       test_AsyncSubscriberStarvation},
    {"AsyncSubscriberOnClose",          test_AsyncSubscriberOnClose},
    {"NextMsgCallOnAsyncSub",           test_NextMsgCallOnAsyncSub},
    {"NoDelay",                         test_NoDelay},
    {"GetLastError",                    test_GetLastError},
    {"SSLBasic",                        test_SSLBasic},
    {"SSLVerify",                       test_SSLVerify},
    {"SSLVerifyHostname",               test_SSLVerifyHostname},
    {"SSLCiphers",                      test_SSLCiphers},
    {"SSLMultithreads",                 test_SSLMultithreads},
    {"SSLConnectVerboseOption",         test_SSLConnectVerboseOption},

    // Clusters Tests

    {"ServersOption",                   test_ServersOption},
    {"AuthServers",                     test_AuthServers},
    {"AuthFailToReconnect",             test_AuthFailToReconnect},
    {"BasicClusterReconnect",           test_BasicClusterReconnect},
    {"HotSpotReconnect",                test_HotSpotReconnect},
    {"ProperReconnectDelay",            test_ProperReconnectDelay},
    {"ProperFalloutAfterMaxAttempts",   test_ProperFalloutAfterMaxAttempts},
    {"TimeoutOnNoServer",               test_TimeoutOnNoServer},
    {"PingReconnect",                   test_PingReconnect}
};

static int  maxTests = (int) (sizeof(allTests)/sizeof(testInfo));

static void
generateList(void)
{
    FILE    *list = fopen("list.txt", "w");
    int     i;

    if (list == NULL)
    {
        printf("@@ Unable to create file 'list.txt': %d\n", errno);
        return;
    }

    printf("Number of tests: %d\n", maxTests);

    for (i=0; i<maxTests; i++)
        fprintf(list, "%s\n", allTests[i].name);

    fflush(list);
    fclose(list);
}

int main(int argc, char **argv)
{
    const char *envStr;
    int testStart   = 0;
    int testEnd     = 0;
    int i;

    if (argc == 1)
    {
        generateList();
        return 0;
    }

    if (argc == 3)
    {
        testStart = atoi(argv[1]);
        testEnd   = atoi(argv[2]);
    }

    if ((argc != 3)
        || (testStart < 0) || (testStart >= maxTests)
        || (testEnd < 0) || (testEnd >= maxTests)
        || (testStart > testEnd))
    {
        printf("@@ Usage: %s [start] [end] (0 .. %d)\n", argv[0], (maxTests - 1));
        return 1;
    }

    envStr = getenv("NATS_TEST_VALGRIND");
    if ((envStr != NULL) && (envStr[0] != '\0'))
    {
        valgrind = true;
        printf("Test running in VALGRIND mode.\n");
    }

    envStr = getenv("NATS_TEST_KEEP_SERVER_OUTPUT");
    if ((envStr != NULL) && (envStr[0] != '\0'))
    {
        keepServerOutput = true;
        printf("Test prints server's output.\n");
    }

    envStr = getenv("NATS_TEST_SERVER_EXE");
    if ((envStr != NULL) && (envStr[0] != '\0'))
    {
        natsServerExe = envStr;
        printf("Test using server executable: %s\n", natsServerExe);
    }

    if (nats_Open(-1) != NATS_OK)
    {
        printf("@@ Unable to run tests: unable to initialize the library!\n");
        return 1;
    }

    // Execute tests
    for (i=testStart; i<=testEnd; i++)
    {
#ifdef _WIN32
        printf("\n== %s ==\n", allTests[i].name);
#else
        printf("\033[0;34m\n== %s ==\n\033[0;0m", allTests[i].name);
#endif
        (*(allTests[i].func))();
    }

    // Makes valgrind happy
    nats_Close();

    if (fails)
    {
        printf("*** %d TEST%s FAILED ***\n", fails, (fails > 1 ? "S" : ""));
        return 1;
    }

    printf("ALL PASSED\n");
    return 0;
}
