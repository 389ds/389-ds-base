/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _PLIST_H
#define _PLIST_H

#ifndef NOINTNSACL
#define INTNSACL
#endif /* !NOINTNSACL */

/*
 * TYPE:        PList_t
 *
 * DESCRIPTION:
 *
 *      This type defines a handle for a property list.
 */

#include "base/pool.h"

#ifndef PUBLIC_NSACL_PLISTDEF_H
#include "../public/nsacl/plistdef.h"
#endif /* !PUBLIC_NSACL_PLISTDEF_H */

#ifdef INTNSACL

/* Functions in plist.c */
NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC extern int PListAssignValue(PList_t plist, const char *pname,
                            const void *pvalue, PList_t ptype);
NSAPI_PUBLIC extern PList_t PListCreate(pool_handle_t *mempool,
                           int resvprop, int maxprop, int flags);
NSAPI_PUBLIC extern int PListDefProp(PList_t plist, int pindex, 
                        const char *pname, const int flags);
NSAPI_PUBLIC extern const void * PListDeleteProp(PList_t plist, int pindex, const char *pname);
NSAPI_PUBLIC extern int PListFindValue(PList_t plist,
                          const char *pname, void **pvalue, PList_t *type);
NSAPI_PUBLIC extern int PListInitProp(PList_t plist, int pindex, const char *pname,
                         const void *pvalue, PList_t ptype);
NSAPI_PUBLIC extern PList_t PListNew(pool_handle_t *mempool);
NSAPI_PUBLIC extern void PListDestroy(PList_t plist);
NSAPI_PUBLIC extern int PListGetValue(PList_t plist,
                         int pindex, void **pvalue, PList_t *type);
NSAPI_PUBLIC extern int PListNameProp(PList_t plist, int pindex, const char *pname);
NSAPI_PUBLIC extern int PListSetType(PList_t plist, int pindex, PList_t type);
NSAPI_PUBLIC extern int PListSetValue(PList_t plist,
                         int pindex, const void *pvalue, PList_t type);
NSAPI_PUBLIC extern void PListEnumerate(PList_t plist, PListFunc_t *user_func, 
                           void *user_data);
NSAPI_PUBLIC extern PList_t
PListDuplicate(PList_t plist, pool_handle_t *new_mempool, int flags);
NSAPI_PUBLIC extern pool_handle_t *PListGetPool(PList_t plist);

NSPR_END_EXTERN_C

#endif /* INTNSACL */

#endif /* _PLIST_H */
