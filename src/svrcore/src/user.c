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
 * user.c - SVRCORE module for reading PIN from the terminal
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <svrcore.h>
#ifdef _WIN32
#include <windows.h>
#endif /* _WIN32 */

/* ------------------------------------------------------------ */
/* I18N */
static const char retryWarning[] =
"Warning: Incorrect PIN may result in disabling the token";
static const char prompt[] = "Enter PIN for";

struct SVRCOREUserPinObj
{
  SVRCOREPinObj base;
  PRBool interactive;
};
static const struct SVRCOREPinMethods vtable;

#ifdef _WIN32
extern char* NT_PromptForPin(const char *tokenName);
#else
/* ------------------------------------------------------------ */
/* 
 * Support routines for changing terminal modes on UNIX
 */
#include <termios.h>
#include <unistd.h>
static void echoOff(int fd)
{
    if (isatty(fd)) {
        struct termios tio;
        tcgetattr(fd, &tio);
        tio.c_lflag &= ~ECHO;
        tcsetattr(fd, TCSAFLUSH, &tio);
    }
}
 
static void echoOn(int fd)
{
    if (isatty(fd)) {
        struct termios tio;
        tcgetattr(fd, &tio);
        tio.c_lflag |= ECHO;
        tcsetattr(fd, TCSAFLUSH, &tio);
    }
}
#endif /* _WIN32 */

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateUserPinObj(SVRCOREUserPinObj **out)
{
  SVRCOREError err = 0;
  SVRCOREUserPinObj *obj = 0;

  do {
    obj = (SVRCOREUserPinObj*)malloc(sizeof (SVRCOREUserPinObj));
    if (!obj) { err = 1; break; }

    obj->base.methods = &vtable;

    obj->interactive = PR_TRUE;
  } while(0);

  if (err)
  {
    SVRCORE_DestroyUserPinObj(obj);
    obj = 0;
  }

  *out = obj;
  return err;
}

void
SVRCORE_DestroyUserPinObj(SVRCOREUserPinObj *obj)
{
  if (obj) free(obj);
}

void
SVRCORE_SetUserPinInteractive(SVRCOREUserPinObj *obj, PRBool i)
{
  obj->interactive = i;
}

static void destroyObject(SVRCOREPinObj *obj)
{
  SVRCORE_DestroyUserPinObj((SVRCOREUserPinObj*)obj);
}


static char *getPin(SVRCOREPinObj *obj, const char *tokenName, PRBool retry)
{
  SVRCOREUserPinObj *tty = (SVRCOREUserPinObj*)obj;
  char line[128];
  char *res;

  /* If the program is not interactive then return no result */
  if (!tty->interactive) return 0;

  if (retry)
    fprintf(stdout, "%s\n", retryWarning);

  echoOff(fileno(stdin));

/***
  Please Note: the following printf statement was changed from fprintf(stdout,...) because
  of an odd problem with the Linux build. The issue is that libc.so has a symbol for stdout
  and libstdc++.so which we also reference has a symbol for stdout. Normally the libc.so version
  of stdout is resolved first and writing to stdout is no problem. Unfortunately something happens
  on Linux which allows the "other" stdout from libstdc++.so to get referenced so that when a call
  to fprintf(stdout,...) is made the new stdout which has never been initialized get's written
  to causing a sigsegv. At this point we can not easily remove libstdc++.so from the dependencies
  because other code which slapd uses happens to be C++ code which causes the reference of
  libstdc++.so .

  It was determined that the quickest way to resolve the issue for now was to change the fprintf
  calls to printf thereby fixing the crashes on a temp basis. Using printf seems to work because
  it references stdout internally which means it will use the one from libc.so . 
***/
  printf("%s %s: ", prompt, tokenName);
  fflush(stdout);

  /* Read input */
  res = fgets(line, sizeof line, stdin);

  echoOn(fileno(stdin));
  printf("\n");

  if (!res) return 0;

  /* Find and kill the newline */
  if ((res = strchr(line, '\n')) != NULL) *res = 0;

  /* Return no-response if user typed an empty line */
  if (line[0] == 0) return 0;

  return strdup(line);
}

/*
 * VTable
 */
static const SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };
