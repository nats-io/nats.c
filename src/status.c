// Copyright 2015 Apcera Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>

// UNIX
//#include <execinfo.h>

#include "status.h"
#include "statusp.h"

static const char *statusText[] = {
    "OK",

    "Error",
    "Protocol Error",
    "IO Error",
    "Line too long",

    "Connection Closed",
    "No server available for connection",
    "State Connection",
    "Secure Connection Required",

    "Not Permitted",
    "Not Found",

    "TCP Address missing",

    "Invalid Subject",
    "Invalid Argument",
    "Invalid Subscription",
    "Invalid Timeout",

    "Illegal State",

    "Slow Consumer, messages dropped",

    "Maximum Payload Exceeded",
    "Maximum Messages Delivered",

    "Insufficient Buffer",

    "No Memory",

    "System Error",

    "Timeout",

    "Initialization Failed",
    "Not Initialized"
};

const char* natsStatus_GetText(natsStatus s) {
    return statusText[(int) s];
}

#if 0
natsStatus
natsSetError(natsStatus s, char* file, int line)
{
    int     i, f;
    void    *frames[100];
    char    **symbolNames = NULL;

    f = backtrace(frames, sizeof(frames));
    if (f > 1)
    {
        symbolNames = backtrace_symbols(frames+1, f);

        for (i=0; i<f-1; i++)
            printf("[%s]\n", symbolNames[i]);

        free(symbolNames);
    }

    return s;

}
#endif
