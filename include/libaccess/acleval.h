/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __acleval_h
#define __acleval_h

/*
 * Description (acleval.h)
 *
 *	This file defines the interface to the ACL evaluation module.
 */

#include "nserror.h"
#include "nsauth.h"
#include "aclstruct.h"

/* Define values returned by lookup routines */
#define ACL_NOMATCH	0		/* no match */
#define ACL_IPMATCH	0x1		/* IP address match */
#define ACL_DNMATCH	0x2		/* DNS name match */
#define ACL_USMATCH	0x4		/* user name match */
#define ACL_GRMATCH	0x8		/* user is member of group */

NSPR_BEGIN_EXTERN_C

/* Functions in acleval.c */
extern int aclDNSLookup(DNSFilter_t * dnf,
			char * dnsspec, int fqdn, char **match);
extern int aclIPLookup(IPFilter_t * ipf, IPAddr_t ipaddr, void **match);
extern int aclUserLookup(UidUser_t * uup, UserObj_t * uoptr);
extern int aclEvaluate(ACL_t * acl, USI_t arid, ClAuth_t * clauth, int * padn);

NSPR_END_EXTERN_C

#endif /* __acleval_h */
