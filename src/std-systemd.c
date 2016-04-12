/*
 * Copyright (C) 1998 Netscape Communications Corporation.
 * All Rights Reserved.
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * std-systemd.c - Extension of the STD module to integrate file, tty and systemd
 */


#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <svrcore.h>
#include <unistd.h>

/* ------------------------------------------------------------ */
/*
 * SVRCOREStdSystemdPinObj implementation
 */
struct SVRCOREStdSystemdPinObj
{
    SVRCOREPinObj base;
    SVRCORECachedPinObj *cache;
    SVRCOREAltPinObj *alt;
    SVRCOREFilePinObj *file;
    SVRCOREUserPinObj *user;
    SVRCOREAltPinObj *systemdalt;
    SVRCORESystemdPinObj *systemd;

    SVRCOREPinObj *top;
};
static const SVRCOREPinMethods vtable;

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateStdSystemdPinObj(
    SVRCOREStdSystemdPinObj **out,
    const char *filename,  PRBool cachePINs,
    PRBool systemdPINs, uint64_t timeout)
{
#ifdef HAVE_SYSTEMD
#ifndef _WIN32
    SVRCOREError err = SVRCORE_Success;
    SVRCOREStdSystemdPinObj *obj = 0;

    do {
        SVRCOREPinObj *top;

        obj = (SVRCOREStdSystemdPinObj *)malloc(sizeof (SVRCOREStdSystemdPinObj));
        if (!obj) { err = SVRCORE_NoMemory_Error; break; }

        obj->base.methods = &vtable;

        obj->cache = 0;
        obj->alt = 0;
        obj->file = 0;
        obj->user = 0;
        obj->systemdalt = 0;
        obj->systemd = 0;

        err = SVRCORE_CreateUserPinObj(&obj->user);
        if (err) {
            break;
        }
        // Automatically detect if we are on an interactive session or not

#ifdef DEBUG
        printf("std-systemd:create() -> interactive %d \n", isatty(fileno(stdin)));
#endif

        // During testing, we want to disable this sometimes ...
        //SVRCORE_SetUserPinInteractive(obj->user, isatty(fileno(stdin)));
        SVRCORE_SetUserPinInteractive(obj->user, PR_FALSE);

        top = (SVRCOREPinObj*)obj->user;

        /* If filename is provided, splice it into the chain */
        if (filename)
        {
            err = SVRCORE_CreateFilePinObj(&obj->file, filename);
            if (err) {
                break;
            }

            err = SVRCORE_CreateAltPinObj(&obj->alt,
                  (SVRCOREPinObj*)obj->file, top);
            if (err) {
                break;
            }

            top = (SVRCOREPinObj*)obj->alt;
        }

        if (systemdPINs) {
#ifdef DEBUG
            printf("std-systemd:create() -> Creating systemd pin object \n");
#endif
            err = SVRCORE_CreateSystemdPinObj(&obj->systemd, timeout);
            if (err) {
                break;
            }
            // Now make a second "alt" object. If pin and user fail, we call systemd
#ifdef DEBUG
            printf("std-systemd:create() -> Creating systemd alt pin object \n");
#endif
            err = SVRCORE_CreateAltPinObj(&obj->systemdalt,
                    top, (SVRCOREPinObj*)obj->systemd);
            if (err) {
                break;
            }
            top = (SVRCOREPinObj *)obj->systemdalt;
#ifdef DEBUG
            printf("std-systemd:create() -> Success adding systemd alt pin object \n");
#endif
        }

        /* Create cache object if requested */
        if (cachePINs)
        {
            err = SVRCORE_CreateCachedPinObj(&obj->cache, top);
            if (err) {
                break;
            }

            top = (SVRCOREPinObj*)obj->cache;
        }

        obj->top = top;
    } while(0);

    *out = obj;

    if (err != SVRCORE_Success)
    {
        SVRCORE_DestroyStdSystemdPinObj(obj);
        *out = NULL;
    }

    return err;
#endif // win32
#else // systemd
    return SVRCORE_MissingFeature;
#endif // Systemd
}

void
SVRCORE_DestroyStdSystemdPinObj(
  SVRCOREStdSystemdPinObj *obj)
{
#ifdef HAVE_SYSTEMD
#ifndef _WIN32
    if (!obj) return;

    if (obj->user) SVRCORE_DestroyUserPinObj(obj->user);
    if (obj->file) SVRCORE_DestroyFilePinObj(obj->file);
    if (obj->alt) SVRCORE_DestroyAltPinObj(obj->alt);
    if (obj->cache) SVRCORE_DestroyCachedPinObj(obj->cache);
    if (obj->systemd) SVRCORE_DestroySystemdPinObj(obj->systemd);
    if (obj->systemdalt) SVRCORE_DestroyAltPinObj(obj->systemdalt);

    free(obj);
#endif // win32
#endif // Systemd
}

/* ------------------------------------------------------------ */

void
SVRCORE_SetStdSystemdPinInteractive(SVRCOREStdSystemdPinObj *obj, PRBool i)
{
#ifdef HAVE_SYSTEMD
#ifndef _WIN32
    SVRCORE_SetUserPinInteractive(obj->user, i);
#endif // win32
#endif // Systemd
}

/* ------------------------------------------------------------ */
/*
 * SVRCORE_StdSystemdPinGetPin
 */
SVRCOREError
SVRCORE_StdSystemdPinGetPin(char **pin, SVRCOREStdSystemdPinObj *obj,
  const char *tokenName)
{
#ifdef HAVE_SYSTEMD
#ifndef _WIN32
#ifdef DEBUG
    printf("std-systemd:stdsystem-getpin() -> starting \n");
#endif
    /* Make sure caching is turned on */
    if (!obj->cache)
    {
        *pin = 0;
        return SVRCORE_NoSuchToken_Error;
    }

    return SVRCORE_CachedPinGetPin(pin, obj->cache, tokenName);
#endif // win32
#else // systemd
    return SVRCORE_MissingFeature;
#endif // Systemd
}

/* ------------------------------------------------------------ */
/*
 * vtable methods
 */
static void
destroyObject(SVRCOREPinObj *obj)
{
    SVRCORE_DestroyStdSystemdPinObj((SVRCOREStdSystemdPinObj*)obj);
}

static char *
getPin(SVRCOREPinObj *pinObj, const char *tokenName, PRBool retry)
{
#ifdef DEBUG
    printf("std-systemd:getpin() -> starting \n");
#endif
    SVRCOREStdSystemdPinObj *obj = (SVRCOREStdSystemdPinObj*)pinObj;

    /* Just forward call to the top level handler */
    return SVRCORE_GetPin(obj->top, tokenName, retry);
}

/*
 * VTable
 */
static const SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };

