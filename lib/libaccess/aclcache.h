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


#ifndef CACHE_H
#define CACHE_H

NSPR_BEGIN_EXTERN_C

extern void ACL_ListHashInit(void);
extern void ACL_ListHashUpdate(ACLListHandle_t **acllistp);
extern void ACL_Destroy(void);
extern int ACL_CritHeld(void);
extern void ACL_CritInit(void);
extern void ACL_UriHashInit(void);
extern void ACL_UriHashDestroy(void);
extern int ACL_CacheCheck(char *uri, ACLListHandle_t **acllist_p);
extern void ACL_CacheEnter(char *uri, ACLListHandle_t **acllist_p);
extern void ACL_CacheAbort(ACLListHandle_t **acllist_p);
extern void ACL_Init2(void);

NSPR_END_EXTERN_C

#endif
