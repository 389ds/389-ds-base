/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsumgmt_h
#define __nsumgmt_h

/*
 * Description (nsumgmt.h)
 *
 *	This file defines the interface to user management facilities
 *	implemented using a Netscape user database.  This interface
 *	provides functions for adding, modifying, and removing user
 *	entries in the database, using the user object (UserObj_t)
 *	structure to convey information across the interface.
 */

#include "nsuser.h"		/* user object access */

NSPR_BEGIN_EXTERN_C

/* User information management operations in nsumgmt.c */
extern int userAddGroup(UserObj_t * uoptr, USI_t gid);
extern NSAPI_PUBLIC UserObj_t * userCreate(NTS_t name, NTS_t pwd, NTS_t rname);
extern int userDeleteGroup(UserObj_t * uoptr, USI_t gid);
extern int userEncode(UserObj_t * uoptr, int * ureclen, ATR_t * urecptr);
extern NSAPI_PUBLIC int userRemove(NSErr_t * errp, void * userdb, int flags, NTS_t name);
extern NSAPI_PUBLIC int userRename(NSErr_t * errp,
		      void * userdb, UserObj_t * uoptr, NTS_t newname);
extern NSAPI_PUBLIC int userStore(NSErr_t * errp,
		     void * userdb, int flags, UserObj_t * uoptr);

NSPR_END_EXTERN_C

#endif /* __nsumgmt_h */
