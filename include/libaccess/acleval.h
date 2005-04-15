/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
