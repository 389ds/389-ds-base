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

#ifndef PUBLIC_NSACL_PLISTDEF_H
#define PUBLIC_NSACL_PLISTDEF_H

/*
 * File:        plistdef.h
 *
 * Description:
 *
 *      This file defines the interface to property lists.  Property
 *      lists are a generalization of parameter blocks (pblocks).
 */

#ifndef PUBLIC_NSAPI_H
#include "public/nsapi.h"
#endif /* !PUBLIC_NSAPI_H */

typedef struct PListStruct_s *PList_t;

/* Define error codes returned from property list routines */

#define ERRPLINVPI -1 /* invalid property index */
#define ERRPLEXIST -2 /* property already exists */
#define ERRPLFULL -3  /* property list is full */
#define ERRPLNOMEM -4 /* insufficient dynamic memory */
#define ERRPLUNDEF -5 /* undefined property name */

#define PLFLG_OLD_MPOOL 0 /* use the plist memory pool */
#define PLFLG_NEW_MPOOL 1 /* use the input memory pool */
#define PLFLG_IGN_RES 2   /* ignore the reserved properties */
#define PLFLG_USE_RES 3   /* use the reserved properties */

#ifdef __cplusplus
typedef void(PListFunc_t)(char *, const void *, void *);
#else
typedef void(PListFunc_t)(void);
#endif

#ifndef INTNSACL
#define PListAssignValue (*__nsacl_table->f_PListAssignValue)
#define PListCreate (*__nsacl_table->f_PListCreate)
#define PListDefProp (*__nsacl_table->f_PListDefProp)
#define PListDeleteProp (*__nsacl_table->f_PListDeleteProp)
#define PListFindValue (*__nsacl_table->f_PListFindValue)
#define PListInitProp (*__nsacl_table->f_PListInitProp)
#define PListNew (*__nsacl_table->f_PListNew)
#define PListDestroy (*__nsacl_table->f_PListDestroy)
#define PListGetValue (*__nsacl_table->f_PListGetValue)
#define PListNameProp (*__nsacl_table->f_PListNameProp)
#define PListSetType (*__nsacl_table->f_PListSetType)
#define PListSetValue (*__nsacl_table->f_PListSetValue)
#define PListEnumerate (*__nsacl_table->f_PListEnumerate)
#define PListDuplicate (*__nsacl_table->f_PListDuplicate)
#define PListGetPool (*__nsacl_table->f_PListGetPool)

#endif /* !INTNSACL */

#endif /* !PUBLIC_NSACL_PLISTDEF_H */
