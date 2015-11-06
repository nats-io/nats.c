// Copyright 2015 Apcera Inc. All rights reserved.

#include "natsp.h"

//#include <stdio.h>
//#include <stdlib.h>

//#include "status.h"

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
    "Connection Disconnected",

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

NATS_EXTERN const char*
natsStatus_GetText(natsStatus s) {
    return statusText[(int) s];
}
