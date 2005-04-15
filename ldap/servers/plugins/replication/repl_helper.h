/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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


typedef struct _repl_genericList {
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

