/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* llist.h - single link list interface */

#ifndef LLIST_H
#define LLIST_H
typedef struct llist LList;

LList* llistNew ();
void   llistDestroy (LList **list, FNFree fnFree);
void*  llistGetFirst(LList *list, void **iterator);
void*  llistGetNext (LList *list, void **iterator);
void*  llistRemoveCurrentAndGetNext (LList *list, void **iterator);
void*  llistGetHead (LList *list);
void*  llistGetTail (LList *list);
void*  llistGet		(LList *list, const char* key);
int	   llistInsertHead (LList *list, const char *key, void *data);
int    llistInsertTail (LList *list, const char *key, void *data);
void*  llistRemoveHead (LList *list);
void*  llistRemove     (LList *list, const char *key);

#endif

