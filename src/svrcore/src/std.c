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
 * std.c - StandardSVRCORE module for reading a PIN 
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <svrcore.h>

/* ------------------------------------------------------------ */
/*
 * SVRCOREStdPinObj implementation
 */
struct SVRCOREStdPinObj
{
  SVRCOREPinObj base;
  SVRCORECachedPinObj *cache;
  SVRCOREAltPinObj *alt;
  SVRCOREFilePinObj *file;
  SVRCOREUserPinObj *user;

  SVRCOREPinObj *top;
};
static const SVRCOREPinMethods vtable;

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateStdPinObj(
  SVRCOREStdPinObj **out,
  const char *filename,  PRBool cachePINs)
{
  SVRCOREError err = SVRCORE_Success;
  SVRCOREStdPinObj *obj = 0;

  do {
    SVRCOREPinObj *top;

    obj = (SVRCOREStdPinObj *)malloc(sizeof (SVRCOREStdPinObj));
    if (!obj) { err = SVRCORE_NoMemory_Error; break; }

    obj->base.methods = &vtable;

    obj->cache = 0;
    obj->alt = 0;
    obj->file = 0;
    obj->user = 0;

    err = SVRCORE_CreateUserPinObj(&obj->user);
    if (err) break;

    top = (SVRCOREPinObj*)obj->user;

    /* If filename is provided, splice it into the chain */
    if (filename)
    {
      err = SVRCORE_CreateFilePinObj(&obj->file, filename);
      if (err) break;

      err = SVRCORE_CreateAltPinObj(&obj->alt,
              (SVRCOREPinObj*)obj->file, top);
      if (err) break;

      top = (SVRCOREPinObj*)obj->alt;
    }

    /* Create cache object if requested */
    if (cachePINs)
    {
      err = SVRCORE_CreateCachedPinObj(&obj->cache, top);
      if (err) break;

      top = (SVRCOREPinObj*)obj->cache;
    }

    obj->top = top;
  } while(0);

  *out = obj;

  if (err != SVRCORE_Success)
  {
    SVRCORE_DestroyStdPinObj(obj);
    *out = NULL;
  }


  return err;
}

void
SVRCORE_DestroyStdPinObj(
  SVRCOREStdPinObj *obj)
{
  if (!obj) return;

  if (obj->user) SVRCORE_DestroyUserPinObj(obj->user);
  if (obj->file) SVRCORE_DestroyFilePinObj(obj->file);
  if (obj->alt) SVRCORE_DestroyAltPinObj(obj->alt);
  if (obj->cache) SVRCORE_DestroyCachedPinObj(obj->cache);

  free(obj);
}

/* ------------------------------------------------------------ */

void
SVRCORE_SetStdPinInteractive(SVRCOREStdPinObj *obj, PRBool i)
{
  SVRCORE_SetUserPinInteractive(obj->user, i);
}

/* ------------------------------------------------------------ */
/*
 * SVRCORE_StdPinGetPin
 */
SVRCOREError
SVRCORE_StdPinGetPin(char **pin, SVRCOREStdPinObj *obj,
  const char *tokenName)
{
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
  SVRCORE_DestroyStdPinObj((SVRCOREStdPinObj*)obj);
}

static char *
getPin(SVRCOREPinObj *pinObj, const char *tokenName, PRBool retry)
{
  SVRCOREStdPinObj *obj = (SVRCOREStdPinObj*)pinObj;

  /* Just forward call to the top level handler */
  return SVRCORE_GetPin(obj->top, tokenName, retry);
}

/*
 * VTable
 */
static const SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };
