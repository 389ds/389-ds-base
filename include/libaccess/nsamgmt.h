/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsamgmt_h
#define __nsamgmt_h

/*
 * Description (nsamgmt.h)
 *
 *	This file defines the interface for managing information in a
 *	Netscape authentication database.  An authentication database
 *	consists of a user database and a group database.  This
 *	implementation of an authentication database based on Netscape
 *	user and group databases defined in nsuser.h and nsgroup.h,
 *	which in turn are based on the Netscape (server) database
 *	implementation defined in nsdb.h.  The interface for retrieving
 *	information from an authentication database is described
 *	separately in nsadb.h.
 */

#include "nsadb.h"

/* Flags used in enumeration call-back function return value */
#define ADBF_KEEPOBJ	0x1		/* do not free user or group object */
#define ADBF_STOPENUM	0x2		/* stop the enumeration */

NSPR_BEGIN_EXTERN_C

/* Functions in nsamgmt.c */
NSAPI_PUBLIC extern int nsadbAddGroupToGroup(NSErr_t * errp, void * authdb,
					     GroupObj_t * pgoptr,
					     GroupObj_t * cgoptr);

NSAPI_PUBLIC extern int nsadbAddUserToGroup(NSErr_t * errp, void * authdb,
					    GroupObj_t * goptr,
					    UserObj_t * uoptr);

NSAPI_PUBLIC extern int nsadbCreateGroup(NSErr_t * errp,
					 void * authdb, GroupObj_t * goptr);

NSAPI_PUBLIC extern int nsadbCreateUser(NSErr_t * errp,
					void * authdb, UserObj_t * uoptr);

/*
for ANSI C++ standard on SCO UDK must typedef fn in arg list, otherwise fn
name is managled
*/

#ifdef UnixWare
typedef int(*ArgFn_EnumUsers)(NSErr_t * ferrp, void * authdb, void * parg,
                 UserObj_t * uoptr);

NSAPI_PUBLIC extern int nsadbEnumerateUsers(NSErr_t * errp, void * authdb,
					void * argp, ArgFn_EnumUsers);
#else /* UnixWare */
NSAPI_PUBLIC extern int nsadbEnumerateUsers(NSErr_t * errp, void * authdb,
					    void * argp,
					    int (*func)(NSErr_t * ferrp,
							void * authdb,
							void * parg,
							UserObj_t * uoptr));
#endif /* UnixWare */
 
#ifdef UnixWare
typedef int(*ArgFn_EnumGroups)(NSErr_t * ferrp, void * authdb, void * parg,
                 GroupObj_t * goptr);
NSAPI_PUBLIC extern int nsadbEnumerateGroups(NSErr_t * errp,
						void * authdb, void * argp,
                       				ArgFn_EnumGroups);
#else /* UnixWare */
NSAPI_PUBLIC extern int nsadbEnumerateGroups(NSErr_t * errp,
                                             void * authdb, void * argp,
					     int (*func)(NSErr_t * ferrp,
							 void * authdb,
							 void * parg,
							 GroupObj_t * goptr));
#endif /* UnixWare */

NSAPI_PUBLIC extern int nsadbIsUserInGroup(NSErr_t * errp, void * authdb,
					   USI_t uid, USI_t gid,
					   int ngroups, USI_t * grplist);

NSAPI_PUBLIC extern int nsadbModifyGroup(NSErr_t * errp,
					 void * authdb, GroupObj_t * goptr);

NSAPI_PUBLIC extern int nsadbModifyUser(NSErr_t * errp,
					void * authdb, UserObj_t * uoptr);

NSAPI_PUBLIC extern int nsadbRemoveGroup(NSErr_t * errp,
					 void * authdb, char * name);

NSAPI_PUBLIC extern int nsadbRemoveUser(NSErr_t * errp,
					void * authdb, char * name);

NSAPI_PUBLIC extern int nsadbRemGroupFromGroup(NSErr_t * errp, void * authdb,
					       GroupObj_t * pgoptr,
					       GroupObj_t * cgoptr);

NSAPI_PUBLIC extern int nsadbRemUserFromGroup(NSErr_t * errp, void * authdb,
					      GroupObj_t * goptr,
					      UserObj_t * uoptr);

NSAPI_PUBLIC extern int nsadbSuperGroups(NSErr_t * errp, void * authdb,
					 GroupObj_t * goptr,
					 USIList_t * gsuper);


NSPR_END_EXTERN_C

#if defined(CLIENT_AUTH)

/* Removed for new ns security integration
#include <sec.h>
*/
#include <key.h>
#include <cert.h>

#endif /* defined(CLIENT_AUTH) */

#endif /* __nsamgmt_h */
