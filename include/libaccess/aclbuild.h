/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __aclbuild_h
#define __aclbuild_h

/*
 * Description (aclbuild.h)
 *
 *	This file describes the interface to a module which provides
 *	functions for building Access Control List (ACL) structures
 *	in memory.
 */

#include "usi.h"
#include "nserror.h"
#include "aclstruct.h"

/* Define flags for aclAuthNameAdd() return value */
#define ANA_GROUP	0x1		/* name matches group name */
#define ANA_USER	0x2		/* name matches user name */
#define ANA_DUP		0x4		/* name already in AuthNode_t */

NSPR_BEGIN_EXTERN_C

/* Functions in aclbuild.c */
extern int accCreate(NSErr_t * errp, void * stp, ACContext_t **pacc);
extern void accDestroy(ACContext_t * acc, int flags);
extern int accDestroySym(Symbol_t * sym, void * argp);
extern int accReadFile(NSErr_t * errp, char * aclfile, ACContext_t **pacc);
extern int aclAuthDNSAdd(HostSpec_t **hspp, char * dnsspec, int fqdn);
extern int aclAuthIPAdd(HostSpec_t **hspp, IPAddr_t ipaddr, IPAddr_t netmask);
extern int aclAuthNameAdd(NSErr_t * errp, UserSpec_t * usp,
			  Realm_t * rlm, char * name);
extern ACClients_t * aclClientsDirCreate();
extern int aclCreate(NSErr_t * errp,
		     ACContext_t * acc, char * aclname, ACL_t **pacl);
extern void aclDestroy(ACL_t * acl);
extern void aclDelete(ACL_t * acl);
extern int aclDirectiveAdd(ACL_t * acl, ACDirective_t * acd);
extern ACDirective_t * aclDirectiveCreate();
extern void aclDirectiveDestroy(ACDirective_t * acd);
extern int aclDNSSpecDestroy(Symbol_t * sym, void * parg);
extern void aclHostSpecDestroy(HostSpec_t * hsp);
extern void aclRealmSpecDestroy(RealmSpec_t * rsp);
extern int aclRightDef(NSErr_t * errp,
		       ACContext_t * acc, char * rname, RightDef_t **prd);
extern void aclRightSpecDestroy(RightSpec_t * rsp);
extern UserSpec_t * aclUserSpecCreate();
extern void aclUserSpecDestroy(UserSpec_t * usp);

NSPR_END_EXTERN_C

#endif /* __aclbuild_h */
