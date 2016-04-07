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
 * alt.c - SVRCORE module for reading a PIN from one of two alternate
 *   sources.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <svrcore.h>

/* ------------------------------------------------------------ */
/*
 * SVRCOREAltPinObj implementation
 */
struct SVRCOREAltPinObj
{
  SVRCOREPinObj base;
  SVRCOREPinObj *primary;
  SVRCOREPinObj *alt;
};
static const SVRCOREPinMethods vtable;

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateAltPinObj(
  SVRCOREAltPinObj **out,
  SVRCOREPinObj *primary, SVRCOREPinObj *alt)
{
  SVRCOREError err = SVRCORE_Success;
  SVRCOREAltPinObj *obj = 0;

  do {
    obj = (SVRCOREAltPinObj *)malloc(sizeof (SVRCOREAltPinObj));
    if (!obj) { err = SVRCORE_NoMemory_Error; break; }

    obj->base.methods = &vtable;

    obj->primary = primary;
    obj->alt = alt;
  } while(0);

  if (err != SVRCORE_Success)
  {
    SVRCORE_DestroyAltPinObj(obj);
  }

  *out = obj;

  return err;
}

void
SVRCORE_DestroyAltPinObj(
  SVRCOREAltPinObj *obj)
{
  if (!obj) return;

  free(obj);
}

/* ------------------------------------------------------------ */
/*
 * vtable methods
 */
static void
destroyObject(SVRCOREPinObj *obj)
{
  SVRCORE_DestroyAltPinObj((SVRCOREAltPinObj*)obj);
}

static char *
getPin(SVRCOREPinObj *pinObj, const char *tokenName, PRBool retry)
{
  SVRCOREAltPinObj *obj = (SVRCOREAltPinObj*)pinObj;
  char *res = 0;

  do {
    /* Try primary first */
    res = SVRCORE_GetPin(obj->primary, tokenName, retry);
    if (res) break;

    /* If unsucessful, try alternate source */
    res = SVRCORE_GetPin(obj->alt, tokenName, retry);
  } while(0);

  return res;
}

/*
 * VTable
 */
static const SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };
