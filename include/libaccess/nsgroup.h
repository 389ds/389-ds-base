/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsgroup_h
#define __nsgroup_h

/*
 * Description (nsgroup.h)
 *
 *	This file describes the interface to group information stored in
 *	a Netscape group database.  Information about a group is provided
 *	to the caller in the form of a group object (GroupObj_t), defined
 *	in nsauth.h.  This interface provides only read access to group
 *	information.  The interface for managing the group database is
 *	described in nsgmgmt.h.
 */

#include "nserror.h"		/* error frame list support */
#include "nsautherr.h"		/* authentication error codes */
#include "nsauth.h"		/* authentication types */

/* Begin private definitions */
#ifdef __PRIVATE_NSGROUP

#include "nsdb.h"

/*
 * Define structure used to communicate between groupEnumerate() and
 * groupEnumHelp().
 */

typedef struct GroupEnumArgs_s GroupEnumArgs_t;
struct GroupEnumArgs_s {
    void * groupdb;			/* group database handle */
    int flags;				/* groupEnumerate() flags */
    int (*func)(NSErr_t * ferrp, void * parg,
		GroupObj_t * goptr);	/* user function pointer */
    void * user;			/* user's argp pointer */
};

/* Define attribute tags for group DB records */
#define GAT_GID		0x50		/* group id (USI) */
#define GAT_FLAGS	0x51		/* flags (USI) */
#define GAT_DESCRIPT	0x52		/* group description (NTS) */
#define GAT_USERS	0x53		/* list of users (USI...) */
#define GAT_GROUPS	0x54		/* list of groups (USI...) */
#define GAT_PGROUPS	0x55		/* list of paret groups (USI...) */

#endif /* __PRIVATE_NSGROUP */

/* Begin public definitions */

/* Define flags for groupEnumerate() */
#define GOF_ENUMKEEP	0x1		/* don't free group objects */

NSPR_BEGIN_EXTERN_C

    /* Operations on a group object (see nsgroup.c) */
extern NSAPI_PUBLIC GroupObj_t * groupDecode(NTS_t name, int ureclen, ATR_t urecptr);
extern NSAPI_PUBLIC int groupEnumerate(NSErr_t * errp,
			  void * groupdb, int flags, void * argp,
			  int (*func)(NSErr_t * ferrp,
				     void * parg, GroupObj_t * goptr));
extern NSAPI_PUBLIC GroupObj_t * groupFindByName(NSErr_t * errp,
				    void * groupdb, NTS_t name);
extern NSAPI_PUBLIC GroupObj_t * groupFindByGid(NSErr_t * errp, void * groupdb, USI_t gid);
extern NSAPI_PUBLIC void groupFree(GroupObj_t * goptr);

NSPR_END_EXTERN_C

#endif /* __nsgroup_h */
