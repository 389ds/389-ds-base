/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsuser_h
#define __nsuser_h

/*
 * Description (nsuser.h)
 *
 *	This file describes the interface to user information stored in
 *	a Netscape user database.  Information about a user is provided
 *	to the caller in the form of a user object (UserObj_t), defined
 *	in nsauth.h.  This interface provides only read access to user
 *	information.  The interface for managing the user database is
 *	described in nsumgmt.h.
 */

#include "nserror.h"		/* error frame list support */
#include "nsautherr.h"		/* authentication error codes */
#include "nsauth.h"		/* authentication types */

/* Begin private definitions */
#ifdef __PRIVATE_NSUSER

#include "nsdb.h"

/*
 * Define structure used to communicate between userEnumerate() and
 * userEnumHelp().
 */

typedef struct UserEnumArgs_s UserEnumArgs_t;
struct UserEnumArgs_s {
    void * userdb;			/* user database handle */
    int flags;				/* userEnumerate() flags */
    int (*func)(NSErr_t * ferrp, void * parg,
		UserObj_t * uoptr);	/* user function pointer */
    void * user;			/* user's argp pointer */
};

/* Define attribute tags for user DB records */
#define UAT_PASSWORD	0x40		/* password (NTS) */
#define UAT_UID		0x41		/* user id (USI) */
#define UAT_ACCFLAGS	0x42		/* account flags (USI) */
#define UAT_REALNAME	0x43		/* real name (NTS) */
#define UAT_GROUPS	0x44		/* list of groups (USI...) */

#endif /* __PRIVATE_NSUSER */

/* Begin public definitions */

/* Define flags for userEnumerate() */
#define UOF_ENUMKEEP	0x1		/* don't free user objects */

NSPR_BEGIN_EXTERN_C

/* User information retrieval operations in nsuser.c */
extern UserObj_t * userDecode(NTS_t name, int ureclen, ATR_t urecptr);
extern int userEnumerate(NSErr_t * errp, void * userdb, int flags, void * argp,
			 int (*func)(NSErr_t * ferrp,
				     void * parg, UserObj_t * uoptr));
extern UserObj_t * userFindByName(NSErr_t * errp, void * userdb, NTS_t name);
extern UserObj_t * userFindByUid(NSErr_t * errp, void * userdb, USI_t uid);
NSAPI_PUBLIC extern void userFree(UserObj_t * uoptr);

NSPR_END_EXTERN_C

#endif /* __nsuser_h */
