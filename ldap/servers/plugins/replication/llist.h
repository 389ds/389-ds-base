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

/* llist.h - single link list interface */

#ifndef LLIST_H
#define LLIST_H
typedef struct llist LList;

LList *llistNew(void);
void llistDestroy(LList **list, FNFree fnFree);
void *llistGetFirst(LList *list, void **iterator);
void *llistGetNext(LList *list, void **iterator);
void *llistRemoveCurrentAndGetNext(LList *list, void **iterator);
void *llistGetHead(LList *list);
void *llistGetTail(LList *list);
void *llistGet(LList *list, const char *key);
int llistInsertHead(LList *list, const char *key, void *data);
int llistInsertTail(LList *list, const char *key, void *data);
void *llistRemoveHead(LList *list);
void *llistRemove(LList *list, const char *key);

#endif
