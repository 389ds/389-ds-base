/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef CACHE_H
#define CACHE_H

NSPR_BEGIN_EXTERN_C

extern void ACL_ListHashUpdate(ACLListHandle_t **acllistp);
extern void ACL_Init(void);
extern void ACL_CritEnter(void);
extern void ACL_CritExit(void);
extern ENTRY *ACL_GetUriHash(ENTRY item, ACTION action);
extern int  ACL_CacheCheck(char *uri, ACLListHandle_t **acllist_p);
extern void  ACL_CacheEnter(char *uri, ACLListHandle_t **acllist_p);
extern void  ACL_CacheAbort(ACLListHandle_t **acllist_p);

NSPR_END_EXTERN_C

#endif
