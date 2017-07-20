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

#define REPL_OBJSET_SUCCESS 0
#define REPL_OBJSET_DUPLICATE_KEY 1
#define REPL_OBJSET_INTERNAL_ERROR 2
#define REPL_OBJSET_KEY_NOT_FOUND 3

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
