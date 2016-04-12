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
 * pin.c - SVRCORE module implementing PK11 pin callback support
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <svrcore.h>

#include <string.h>
#include <pk11func.h>
#include <seccomon.h>

/*
 * Global state
 */
static SVRCOREPinObj *pinObj = 0;

/*
 * SVRCORE_Pk11PasswordFunc
 *
 * DEPRECATED public interface.
 */
static char *
SVRCORE_Pk11PasswordFunc(PK11SlotInfo *slot, PRBool retry, void *ctx)
{
  char *passwd;

  /* If the user has not installed a cbk, then return failure (cancel) */
  if (pinObj == 0) return 0;

  /* Invoke the callback function, translating slot into token name */
  passwd = SVRCORE_GetPin(pinObj, PK11_GetTokenName(slot), retry);

  return passwd;
}

/*
 * SVRCORE_RegisterPinObj
 */
void
SVRCORE_RegisterPinObj(SVRCOREPinObj *obj)
{
  /* Set PK11 callback function to call back here */
  PK11_SetPasswordFunc(SVRCORE_Pk11PasswordFunc);

  /* Set object to use for getPin method */
  pinObj = obj;
}

/*
 * SVRCORE_GetRegisteredPinObj
 */
SVRCOREPinObj *
SVRCORE_GetRegisteredPinObj(void)
{
  return pinObj;
}

void
SVRCORE_DestroyRegisteredPinObj(void)
{
  if (pinObj) {
    pinObj->methods->destroyObj(pinObj);
  }
  pinObj = 0;
}
