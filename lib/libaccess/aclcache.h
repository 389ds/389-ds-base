/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef CACHE_H
#define CACHE_H

NSPR_BEGIN_EXTERN_C

extern void	ACL_ListHashInit(void);
extern void 	ACL_ListHashUpdate(ACLListHandle_t **acllistp);
extern void	ACL_Destroy(void);
extern int	ACL_CritHeld(void);
extern void	ACL_CritInit(void);
extern void	ACL_UriHashInit(void);
extern void	ACL_UriHashDestroy(void);
extern int 	ACL_CacheCheck(char *uri, ACLListHandle_t **acllist_p);
extern void 	ACL_CacheEnter(char *uri, ACLListHandle_t **acllist_p);
extern void 	ACL_CacheAbort(ACLListHandle_t **acllist_p);
extern void	ACL_Init2(void);
extern int	ACL_RegisterInit ();

NSPR_END_EXTERN_C

#endif
