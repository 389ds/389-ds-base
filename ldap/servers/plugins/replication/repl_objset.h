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
 */

/* repl_objset.h */
 /*
  *  Support for lifetime management of sets of objects.
  *  Objects are refcounted. NOTE: This API should go away
  * in favor of the objset API provided by libslapd.
  */
#ifndef _REPL_OBJSET_H
#define __REPL_OBJSET_H

#include "llist.h"

#define REPL_OBJSET_SUCCESS			0
#define REPL_OBJSET_DUPLICATE_KEY	1
#define REPL_OBJSET_INTERNAL_ERROR	2
#define REPL_OBJSET_KEY_NOT_FOUND	3

typedef struct repl_objset Repl_Objset;

Repl_Objset *repl_objset_new(FNFree destructor);
void repl_objset_destroy(Repl_Objset **o, time_t maxwait, FNFree panic_fn);
int repl_objset_add(Repl_Objset *o, const char *name, void *obj);
int repl_objset_acquire(Repl_Objset *o, const char *key, void **obj, void **handle);
void repl_objset_release(Repl_Objset *o, void *handle);
void repl_objset_delete(Repl_Objset *o, void *handle);
void *repl_objset_next_object(Repl_Objset *o, void *cookie, void **handle);
void *repl_objset_first_object(Repl_Objset *o, void **cookie, void **handle);
void repl_objset_iterator_destroy(void **itcontext);

#endif /* _REPL_OBJSET_H */
