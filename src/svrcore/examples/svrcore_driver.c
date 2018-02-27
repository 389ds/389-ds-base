//
// BEGIN COPYRIGHT BLOCK
// Copyright (C) 2016 Red Hat, Inc.
// All rights reserved.
//
// License: GPL (version 3 or any later version).
// See LICENSE for details. 
// END COPYRIGHT BLOCK
//
// Author: William Brown <wibrown@redhat.com>
//

// Build with gcc -g3 -o svrcore_driver `pkg-config --libs --cflags nspr` `PKG_CONFIG_PATH=/opt/svrcore/lib/pkgconfig pkg-config --cflags --libs svrcore` -L`PKG_CONFIG_PATH=/opt/svrcore/lib/pkgconfig pkg-config --variable=libdir svrcore` svrcore_driver.c

#include <stdio.h>
#include <nspr.h>
#include <svrcore.h>

int
svrcore_systemd_setup()
{
    PRErrorCode errorCode;
    SVRCORESystemdPinObj *StdPinObj;

    StdPinObj = (SVRCORESystemdPinObj *)SVRCORE_GetRegisteredPinObj();

    if (StdPinObj) {
        // This means it's already registered?
        return 0;
    }
    if (SVRCORE_CreateSystemdPinObj(&StdPinObj, 60) != SVRCORE_Success) {
        errorCode = PR_GetError();
        printf("Unable to create pin %d\n", errorCode);
        return -1;
    }
    SVRCORE_RegisterPinObj((SVRCOREPinObj *)StdPinObj);
    return 0;
}

int
svrcore_systemd_get_token()
{
    //Actually get the password
    // Get the pinobj
    SVRCORESystemdPinObj *StdPinObj;
    char *pw = NULL;
    char *token = NULL;
    SVRCOREError err = SVRCORE_Success;

    StdPinObj = (SVRCORESystemdPinObj *)SVRCORE_GetRegisteredPinObj();
    // Are we interactive?
    // SVRCORE_SetStdPinInteractive((SVRCOREStdPinObj *) StdPinObj , PR_TRUE);
    // what is token?
    token = "internal (software)";
    pw = SVRCORE_GetPin( (SVRCOREPinObj *)StdPinObj, token , PR_FALSE);
    if ( err != SVRCORE_Success || pw == NULL) {
        printf("Couldn't get pin %d \n", err);
    } else {
        printf("%s\n", pw);
        free(pw);
    }
    // Should set a password into &pw

    // Cleanup
    SVRCORE_DestroyRegisteredPinObj();

    return 0;
}

int
svrcore_stdsystemd_setup()
{
    PRErrorCode errorCode;
    SVRCOREStdSystemdPinObj *StdSysPinObj;
    char *filename = "/tmp/pin.txt";

    StdSysPinObj = (SVRCOREStdSystemdPinObj *)SVRCORE_GetRegisteredPinObj();

    if (StdSysPinObj) {
        // This means it's already registered?
        return 0;
    }
    if (SVRCORE_CreateStdSystemdPinObj(&StdSysPinObj, filename, PR_FALSE, PR_TRUE, 60) != SVRCORE_Success) {
        errorCode = PR_GetError();
        printf("Unable to create std systemd pin %d\n", errorCode);
        return -1;
    }
    SVRCORE_RegisterPinObj((SVRCOREPinObj *)StdSysPinObj);
    return 0;
}

int
svrcore_stdsystemd_get_token()
{
    //Actually get the password
    // Get the pinobj
    SVRCOREStdSystemdPinObj *StdSysPinObj;
    char *pw = NULL;
    char *token = NULL;
    SVRCOREError err = SVRCORE_Success;

    StdSysPinObj = (SVRCOREStdSystemdPinObj *)SVRCORE_GetRegisteredPinObj();
    // Are we interactive?
    // SVRCORE_SetStdPinInteractive((SVRCOREStdPinObj *) StdPinObj , PR_TRUE);
    // what is token?
    token = "internal (software)";
    pw = SVRCORE_GetPin( (SVRCOREPinObj *)StdSysPinObj, token , PR_FALSE);
    if ( err != SVRCORE_Success || pw == NULL) {
        printf("Couldn't get pin %d \n", err);
    } else {
        printf("%s\n", pw);
        free(pw);
    }
    // Should set a password into &pw

    // Cleanup
    SVRCORE_DestroyRegisteredPinObj();

    return 0;
}

int
main(int argc, char **argv)
{

    int result = 0;

    result = svrcore_systemd_setup();
    if (result == 0) {
        svrcore_systemd_get_token();
    }

    result = svrcore_stdsystemd_setup();
    if (result == 0) {
        svrcore_stdsystemd_get_token();
    }
    return 0;
}


