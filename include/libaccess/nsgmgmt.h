/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsgmgmt_h
#define __nsgmgmt_h

/*
 * Description (nsgmgmt.h)
 *
 *	This file defines the interface to group management facilities
 *	implemented using a Netscape group database.  This interface
 *	provides functions for adding, modifying, and removing group
 *	entries in the database, using the group object (GroupObj_t)
 *	structure to convey information across the interface.
 */

#define __PRIVATE_NSGROUP
#include "nsgroup.h"		/* group object access */

NSPR_BEGIN_EXTERN_C

/* Group information management operations in nsgmgmt.c */
extern NSAPI_PUBLIC int groupAddMember(GroupObj_t * goptr, int isgid, USI_t id);
extern NSAPI_PUBLIC GroupObj_t * groupCreate(NTS_t name, NTS_t desc);
extern NSAPI_PUBLIC int groupDeleteMember(GroupObj_t * goptr, int isgid, USI_t id);
extern NSAPI_PUBLIC int groupEncode(GroupObj_t * goptr, int * ureclen, ATR_t * urecptr);
extern NSAPI_PUBLIC int groupRemove(NSErr_t * errp, void * groupdb, int flags, NTS_t name);
extern NSAPI_PUBLIC int groupStore(NSErr_t * errp,
		      void * groupdb, int flags, GroupObj_t * goptr);

NSPR_END_EXTERN_C

#endif /* __nsgmgmt_h */
