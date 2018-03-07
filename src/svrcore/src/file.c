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
 * file.c - SVRCORE module for reading PIN from a file
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <svrcore.h>

/* ------------------------------------------------------------ */
/*
 * Node - for maintaining link list of tokens with bad PINs
 */
typedef struct Node Node;
static void freeList(Node *list);

struct Node
{
  Node *next;
  char *tokenName;
};

/* ------------------------------------------------------------ */
/*
 * SVRCOREFilePinObj implementation
 */
struct SVRCOREFilePinObj
{
  SVRCOREPinObj base;
  char *filename;
  PRBool disabled;
  Node *badPinList;
};
static const struct SVRCOREPinMethods vtable;

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateFilePinObj(
  SVRCOREFilePinObj **out,
  const char *filename)
{
  SVRCOREError err = SVRCORE_Success;
  SVRCOREFilePinObj *obj = 0;

  do {
    obj = (SVRCOREFilePinObj*)malloc(sizeof (SVRCOREFilePinObj));
    if (!obj) { err = SVRCORE_NoMemory_Error; break; }

    obj->base.methods = &vtable;

    obj->filename = 0;
    obj->disabled = PR_FALSE;
    obj->badPinList = 0;

    obj->filename = strdup(filename);
    if (!obj->filename) { err = SVRCORE_NoMemory_Error; break; }

  } while(0);

  if (err)
  {
    SVRCORE_DestroyFilePinObj(obj);
    obj = 0;
  }
  
  *out = obj;
  return err;
}

void
SVRCORE_DestroyFilePinObj(SVRCOREFilePinObj *obj)
{
  if (!obj) return;

  if (obj->filename) free(obj->filename);

  if (obj->badPinList) freeList(obj->badPinList);

  free(obj);
}

/* ------------------------------------------------------------ */
/*
 * vtable functions
 */
static void
destroyObject(SVRCOREPinObj *obj)
{
  SVRCORE_DestroyFilePinObj((SVRCOREFilePinObj*)obj);
}

static char *
getPin(SVRCOREPinObj *ctx, const char *tokenName, PRBool retry)
{
  SVRCOREFilePinObj *obj = (SVRCOREFilePinObj*)ctx;
  PK11SlotInfo *slot;
  int is_internal_token = 0;
  FILE *pinfile = 0;
  char *res = 0;

  /* In really bad situations, where we cannot allocate memory
   * for a bad PIN list, the entire PIN object is disabled.
   */
  if (obj->disabled) return 0;

  /*
   * If the application is retrying the PIN, the one in the file is
   * probably wrong.  It's important that we avoid returning this
   * PIN to the caller each time, since that will result in disabling
   * the token. 
   */
  {
    Node *p;

    for(p = obj->badPinList;p;p = p->next)
      if (strcmp(p->tokenName, tokenName) == 0) break;

    if (p) return 0;
  }

  /* Mark it as bad (in the hash table) so that we remember
   * to never return it again.
   */
  if (retry)
  {
    Node *ent = 0;

    do {
      ent = (Node *)malloc(sizeof (Node));
      if (!ent) break;

      ent->tokenName = strdup(tokenName);

      if (!ent->tokenName)
      {
        free(ent);
        ent = 0;
      }
    } while(0);

    /* If adding fails, disable the whole object */
    if (!ent) {
        obj->disabled = PR_TRUE;
    }

    if (ent) {
        /* Add to list */
        ent->next = obj->badPinList;
        obj->badPinList = ent;
    }

    return 0;
  }

  slot = PK11_FindSlotByName((char *)tokenName);
  if (slot) {
      is_internal_token = PK11_IsInternal(slot);
      PK11_FreeSlot(slot);
  }

  do {
    char line[128];

    pinfile = fopen(obj->filename, "rt");
    if (!pinfile) break; 

    /* Read lines from the file */
    while(fgets(line, sizeof line, pinfile))
    {
      char *pin;
      char *delim;

      /* Find the ":" */
      delim = strchr(line, ':');
      if (!delim) continue;

      /* Terminate name field and skip ";" */
      *delim++ = 0;

      if (strcmp(line, tokenName) == 0 ||
	  (is_internal_token &&
	      (strcmp(line, "Communicator Certificate DB") == 0 ||
	       strcmp(line, "Internal (Software) Token") == 0)))
      {
        pin = delim;
        delim = strchr(pin, '\n');
        if (delim) *delim = 0;

        res = strdup(pin);
        break;
      }
    }

    /* Clear any sensitive data */
    memset(line, 0, sizeof line);
  } while(0);

  if (pinfile) fclose(pinfile);

  return res;
}

static const struct SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };

/* ------------------------------------------------------------ */
/*
 * Node implementation
 */
static void freeList(Node *list)
{
  Node *n;

  while((n = list) != NULL)
  {
    list = n->next;

    free(n->tokenName);
    free(n);
  }
}

