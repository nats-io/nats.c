#ifndef _NATSTEST_H
#define _NATSTEST_H

extern int __tests;
extern bool __failed;
extern char __namebuf[1024];

#define test(s)                      \
    {                                \
        printf("\n#%02d ", ++__tests); \
        printf("%s\n", (s));           \
        fflush(stdout);              \
    }

#ifdef _WIN32
#define NATS_INVALID_PID (NULL)
#define testCond(c)                       \
    if (c)                                \
    {                                     \
        printf("PASSED\n");               \
        fflush(stdout);                   \
    }                                     \
    else                                  \
    {                                     \
        printf("FAILED\n");               \
        nats_PrintLastErrorStack(stdout); \
        fflush(stdout);                   \
        __failed = true;                  \
        return;                           \
    }
#define testCondNoReturn(c)               \
    if (c)                                \
    {                                     \
        printf("PASSED\n");               \
        fflush(stdout);                   \
    }                                     \
    else                                  \
    {                                     \
        printf("FAILED\n");               \
        nats_PrintLastErrorStack(stdout); \
        fflush(stdout);                   \
        __failed = true;                  \
    }
#define LOGFILE_NAME "wserver.log"
#else
#define NATS_INVALID_PID (-1)
#define testCond(c)                            \
    if (c)                                     \
    {                                          \
        printf("\033[0;32mPASSED\033[0;0m\n"); \
        fflush(stdout);                        \
    }                                          \
    else                                       \
    {                                          \
        printf("\033[0;31mFAILED\033[0;0m\n"); \
        nats_PrintLastErrorStack(stdout);      \
        fflush(stdout);                        \
        __failed = true;                       \
        return;                                \
    }
#define testCondNoReturn(c)                    \
    if (c)                                     \
    {                                          \
        printf("\033[0;32mPASSED\033[0;0m\n"); \
        fflush(stdout);                        \
    }                                          \
    else                                       \
    {                                          \
        printf("\033[0;31mFAILED\033[0;0m\n"); \
        nats_PrintLastErrorStack(stdout);      \
        fflush(stdout);                        \
        __failed = true;                       \
    }
#define LOGFILE_NAME "server.log"
#endif
#define FAIL(m)                    \
    {                              \
        printf("@@ %s @@\n", (m)); \
        __failed = true;           \
        return;                    \
    }

#endif // _TEST_H
