// Copyright 2017 Apcera Inc. All rights reserved.

#include <nats.h>

int main(int argc, char **argv)
{
    // Give a chance for the destructor/DllMain to be registered.
    // Note that nats_Sleep() is not "opening" the library, so
    // the cleanup code would still crash if we were not skipping
    // the cleanup if we detect that library was never oepened.
    nats_Sleep(1000);
    return 0;
}
