/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *  repl_helper.h - Helper functions (should actually be repl_utils.h)
 *
 *
 *
 */

#ifndef _REPL_HELPER_H
#define _REPL_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nspr.h"
#include "slapi-plugin.h"

/*
 * shamelessly stolen from the xp library
 *
 */

/*
  Linked list manipulation routines

  this is a very standard linked list structure
  used by many many programmers all over the world

  The lists have been modified to be doubly linked.  The
    first element in a list is always the header.  The 'next'
        pointer of the header is the first element in the list.
        The 'prev' pointer of the header is the last element in
        the list.

  The 'prev' pointer of the first real element in the list
    is NULL as is the 'next' pointer of the last real element
        in the list

 */


typedef struct _repl_genericList
{
    void *object;
    struct _repl_genericList *next;
    struct _repl_genericList *prev;
} ReplGenericList;

typedef void *(ReplGenericListObjectDestroyFn)(void *obj);

ReplGenericList *ReplGenericListNew(void);
void ReplGenericListDestroy(ReplGenericList *list, ReplGenericListObjectDestroyFn destroyFn);

void ReplGenericListAddObject(ReplGenericList *list,
                              void *newObject);
ReplGenericList *ReplGenericListFindObject(ReplGenericList *list,
                                           void *obj);


#ifdef __cplusplus
}
#endif

#endif
