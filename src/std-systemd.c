/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Netscape svrcore library.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * William Brown <wibrown@redhat.com> 2016
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * std-systemd.c - Extension of the STD module to integrate file, tty and systemd
 */

#ifdef WITH_SYSTEMD
#ifndef _WIN32

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

    if (err != SVRCORE_Success)
    {
        SVRCORE_DestroyStdSystemdPinObj(obj);
    }

    *out = obj;

    return err;
}

void
SVRCORE_DestroyStdSystemdPinObj(
  SVRCOREStdSystemdPinObj *obj)
{
    if (!obj) return;

    if (obj->user) SVRCORE_DestroyUserPinObj(obj->user);
    if (obj->file) SVRCORE_DestroyFilePinObj(obj->file);
    if (obj->alt) SVRCORE_DestroyAltPinObj(obj->alt);
    if (obj->cache) SVRCORE_DestroyCachedPinObj(obj->cache);
    if (obj->systemd) SVRCORE_DestroySystemdPinObj(obj->systemd);
    if (obj->systemdalt) SVRCORE_DestroyAltPinObj(obj->systemdalt);

    free(obj);
}

/* ------------------------------------------------------------ */

void
SVRCORE_SetStdSystemdPinInteractive(SVRCOREStdSystemdPinObj *obj, PRBool i)
{
    SVRCORE_SetUserPinInteractive(obj->user, i);
}

/* ------------------------------------------------------------ */
/*
 * SVRCORE_StdSystemdPinGetPin
 */
SVRCOREError
SVRCORE_StdSystemdPinGetPin(char **pin, SVRCOREStdSystemdPinObj *obj,
  const char *tokenName)
{
#ifdef DEBUG
    printf("std-systemd:stdsystem-getpin() -> starting \n", isatty(fileno(stdin)));
#endif
    /* Make sure caching is turned on */
    if (!obj->cache)
    {
        *pin = 0;
        return SVRCORE_NoSuchToken_Error;
    }

    return SVRCORE_CachedPinGetPin(pin, obj->cache, tokenName);
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
    printf("std-systemd:getpin() -> starting \n", isatty(fileno(stdin)));
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

#endif /* End ifndef WIN32 */
#endif /* End ifdef WITH_SYSTEMD */
