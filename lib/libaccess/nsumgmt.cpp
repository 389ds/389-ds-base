/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsumgmt.c)
 *
 *	This module contains routines for managing information in a
 *	Netscape user database.  Information for a particular user
 *	is modified by retrieving the current information in the form
 *	of a user object (UserObj_t), calling functions in this module,
 *	to modify the user object, and then calling userStore() to
 *	write the information in the user object back to the database.
 */

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#include "libaccess/nsdbmgmt.h"
#define __PRIVATE_NSUSER
#include "libaccess/nsumgmt.h"

/*
 * Description (userAddGroup)
 *
 *	This function adds a group id to the list of group ids associated
 *	with a user object.
 *
 * Arguments:
 *
 *	uoptr			- user object pointer
 *	gid			- group id to be added
 *
 * Returns:
 *
 *	Returns zero if the group id is already present in the group id list.
 *	Returns one if the group id was added successfully.
 *	Returns a negative value if an error occurs.
 */

int userAddGroup(UserObj_t * uoptr, USI_t gid)
{
    int rv;

    rv = usiInsert(&uoptr->uo_groups, gid);

    if (rv > 0) {

	uoptr->uo_flags |= UOF_MODIFIED;
    }

    return rv;
}

/*
 * Description (userCreate)
 *
 *	This function creates a user object, using information about
 *	the user provided by the caller.  The strings passed for the
 *	user account name, password, and real user name may be on the
 *	stack.  The user id is set to zero, but the user object is
 *	marked as being new.  A user id will be assigned when
 *	userStore() is called to add the user to a user database.
 *
 * Arguments:
 *
 *	name		- pointer to user account name string
 *	pwd		- pointer to (encrypted) password string
 *	rname		- real user name (gecos string)
 *
 * Returns:
 *
 *	A pointer to a dynamically allocated UserObj_t structure is
 *	returned.
 */

NSAPI_PUBLIC UserObj_t * userCreate(NTS_t name, NTS_t pwd, NTS_t rname)
{
    UserObj_t * uoptr;		/* user object pointer */

    uoptr = (UserObj_t *)MALLOC(sizeof(UserObj_t));
    if (uoptr) {
	uoptr->uo_name = (NTS_t)STRDUP((char *)name);
	uoptr->uo_pwd = (pwd) ? (NTS_t)STRDUP((char *)pwd) : 0;
	uoptr->uo_uid = 0;
	uoptr->uo_flags = (UOF_MODIFIED | UOF_NEW);
	uoptr->uo_rname = (rname) ? (NTS_t)STRDUP((char *)rname) : 0;
	UILINIT(&uoptr->uo_groups);
    }

    return uoptr;
}

/*
 * Description (userDeleteGroup)
 *
 *	This function removes a specified group id from a user object's
 *	list of groups.
 *
 * Arguments:
 *
 *	uoptr			- pointer to user object
 *	gid			- group id to remove
 *
 * Returns:
 *
 *	The return value is zero if the specified group id was not present
 *	in the user object, or one if the group was successfully removed.
 */

int userDeleteGroup(UserObj_t * uoptr, USI_t gid)
{
    int rv;			/* return value */

    rv = usiRemove(&uoptr->uo_groups, gid);
    if (rv > 0) {
	uoptr->uo_flags |= UOF_MODIFIED;
    }

    return rv;
}

/*
 * Description (userEncode)
 *
 *	This function encodes a user object into a user DB record.
 *
 * Arguments:
 *
 *	uoptr			- pointer to user object
 *	ureclen			- pointer to returned record length
 *	urecptr			- pointer to returned record pointer
 *
 * Returns:
 *
 *	The function return value is zero if successful.  The length
 *	and location of the created attribute record are returned
 *	through 'ureclen' and 'urecptr'.  A non-zero function value
 *	is returned if there's an error.
 */

int userEncode(UserObj_t * uoptr, int * ureclen, ATR_t * urecptr)
{
    int reclen;			/* length of DB record */
    ATR_t rptr;			/* DB record pointer */
    ATR_t rstart = 0;		/* pointer to beginning of DB record */
    ATR_t glptr;		/* saved pointer to UAT_GROUPS length */
    ATR_t gptr;			/* saved pointer to after length at glptr */
    int pwdlen;			/* password encoding length */
    int uidlen;			/* uid encoding length */
    int fllen;			/* account flags encoding length */
    USI_t rnlen;		/* real name encoding length */
    USI_t nglen;		/* group count encoding length */
    USI_t gcnt;			/* number of group ids */
    USI_t * gids;		/* pointer to array of group ids */
    int i;			/* group id index */
    int rv = -1;

    /*
     * First we need to figure out how long the generated record will be.
     * This doesn't have to be exact, but it must not be smaller than the
     * actual record size.
     */

    /* UAT_PASSWORD attribute: tag, length, NTS */
    pwdlen = NTSLENGTH(uoptr->uo_pwd);
    reclen = 1 + 1 + pwdlen;
    if (pwdlen > 127) goto punt;

    /* UAT_UID attribute: tag, length, USI */
    uidlen = USILENGTH(uoptr->uo_uid);
    reclen += (1 + 1 + uidlen);

    /* UAT_ACCFLAGS attribute: tag, length, USI */
    fllen = USILENGTH(uoptr->uo_flags & UOF_DBFLAGS);
    reclen += (1 + 1 + fllen);

    /* UAT_REALNAME attribute: tag, length, NTS */
    rnlen = NTSLENGTH(uoptr->uo_rname);
    reclen += (1 + USILENGTH(rnlen) + rnlen);

    /* UAT_GROUPS attribute: tag, length, USI(count), USI(gid)... */
    gcnt = UILCOUNT(&uoptr->uo_groups);
    nglen = USILENGTH(gcnt);
    reclen += (1 + USIALLOC() + nglen + (5 * gcnt));

    /* Allocate the attribute record buffer */
    rptr = (ATR_t)MALLOC(reclen);
    if (rptr) {

	/* Save pointer to start of record */
	rstart = rptr;

	/* Encode UAT_PASSWORD attribute */
	*rptr++ = UAT_PASSWORD;
	*rptr++ = pwdlen;
	rptr = NTSENCODE(rptr, uoptr->uo_pwd);

	/* Encode UAT_UID attribute */
	*rptr++ = UAT_UID;
	*rptr++ = uidlen;
	rptr = USIENCODE(rptr, uoptr->uo_uid);

	/* Encode UAT_ACCFLAGS attribute */
	*rptr++ = UAT_ACCFLAGS;
	*rptr++ = fllen;
	rptr = USIENCODE(rptr, (uoptr->uo_flags & UOF_DBFLAGS));

	/* Encode UAT_REALNAME attribute */
	*rptr++ = UAT_REALNAME;
	rptr = USIENCODE(rptr, rnlen);
	rptr = NTSENCODE(rptr, uoptr->uo_rname);

	/* Encode UAT_GROUPS attribute */
	*rptr++ = UAT_GROUPS;

	/*
	 * Save a pointer to the attribute encoding length, and reserve
	 * space for the maximum encoding size of a USI_t value.
	 */
	glptr = rptr;
	rptr += USIALLOC();
	gptr = rptr;

	/* Encode number of groups */
	rptr = USIENCODE(rptr, gcnt);

	/* Generate group ids encodings */
	gids = UILLIST(&uoptr->uo_groups);
	for (i = 0; i < gcnt; ++i) {
	    rptr = USIENCODE(rptr, gids[i]);
	}

	/* Now fix up the UAT_GROUPS attribute encoding length */
	glptr = USIINSERT(glptr, (USI_t)(rptr - gptr));

	/* Return record length and location if requested */
	if (ureclen) *ureclen = rptr - rstart;
	if (urecptr) *urecptr = rstart;

	/* Indicate success */
	rv = 0;
    }

  punt:
    return rv;
}

/*
 * Description (userRemove)
 *
 *	This function is called to remove a user from a specified user
 *	database.  Both the primary DB file and the id-to-name DB file
 *	are updated.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	flags			- (unused - must be zero)
 *	name			- pointer to user account name
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.
 */

NSAPI_PUBLIC int userRemove(NSErr_t * errp, void * userdb, int flags, NTS_t name)
{
    UserObj_t * uoptr;		/* user object pointer */
    int rv;
    int rv2;

    /* First retrieve the user record */
    uoptr = userFindByName(errp, userdb, name);
    if (!uoptr) {
	/* Error - specified user not found */
	return NSAERRNAME;
    }

    /* Free the user id value, if any */
    rv = 0;
    if (uoptr->uo_uid != 0) {
	rv = ndbFreeId(errp, userdb, 0, (char *)name, uoptr->uo_uid);
    }

    rv2 = ndbDeleteName(errp, userdb, 0, 0, (char *)name);

    return (rv) ? rv : rv2;
}

/*
 * Description (userRename)
 *
 *	This function is called to change the account name associated
 *	with an existing user.  The caller provides a pointer to a
 *	user object for the existing user (with the current user account
 *	name referenced by uo_name), and the new account name for this
 *	user.  A check is made to ensure the uniqueness of the new name
 *	in the specified user database.  The account name in the user
 *	object is modified.  The user database is not modified until
 *	userStore() is called.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	uoptr			- user object pointer
 *	newname			- pointer to new account name string
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.  The user object remains intact in either
 *	case.
 */

NSAPI_PUBLIC int userRename(NSErr_t * errp, void * userdb, UserObj_t * uoptr, NTS_t newname)
{
    int reclen;				/* user record length */
    ATR_t recptr = 0;			/* user record pointer */
    char * oldname;			/* old user account name */
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Save the current account name and replace it with the new one */
    oldname = (char *)uoptr->uo_name;
    uoptr->uo_name = (unsigned char *) STRDUP((char *)newname);

    if ((oldname != 0) && !(uoptr->uo_flags & UOF_NEW)) {

	/* Convert the information in the user object to a DB record */
	rv = userEncode(uoptr, &reclen, &recptr);
	if (rv) goto err_nomem;

	/*
	 * Store the record in the database
	 * under the new user account name.
	 */
	rv = ndbStoreName(errp, userdb, NDBF_NEWNAME,
			  0, (char *)uoptr->uo_name, reclen, (char *)recptr);
	if (rv) goto punt;

	/* Change the mapping of the user id to the new name */
	rv = ndbRenameId(errp, userdb, 0, (char *)uoptr->uo_name, uoptr->uo_uid);
	if (rv) goto punt;

	/* Delete the user record with the old account name */
	rv = ndbDeleteName(errp, userdb, 0, 0, oldname);
	if (rv) goto punt;
    }
    else {
	/* Set flags in user object for userStore() */
	uoptr->uo_flags |= UOF_MODIFIED;
    }

  punt:
    if (recptr) {
	FREE(recptr);
    }
    if (oldname) {
	FREE(oldname);
    }
    return rv;

  err_nomem:
    eid = NSAUERR1000;
    rv = NSAERRNOMEM;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    goto punt;
}

/*
 * Description (userStore)
 *
 *	This function is called to store a user object in the database.
 *	If the object was created by userCreate(), it is assumed to be
 *	a new user account, the user account name must not match any
 *	existing user account names in the database, and a uid is
 *	assigned before adding the user to the database.  If the object
 *	was created by userFindByName(), the information in the user
 *	object will replace the existing database entry for the
 *	indicated user account name.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	flags			- (unused - must be zero)
 *	uoptr			- user object pointer
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.  The user object remains intact in either
 *	case.
 */

NSAPI_PUBLIC int userStore(NSErr_t * errp, void * userdb, int flags, UserObj_t * uoptr)
{
    ATR_t recptr = 0;
    USI_t uid;
    int reclen = 0;
    int stflags = 0;
    int eid;
    int rv;

    /* If this is a new user, allocate a uid value */
    if (uoptr->uo_flags & UOF_NEW) {
	/*
	 * Yes, allocate a user id and add a user id to user
	 * account name mapping to the id-to-name DB file.
	 */
	uid = 0;
	rv = ndbAllocId(errp, userdb, 0, (char *)uoptr->uo_name, &uid);
	if (rv) goto punt;

	uoptr->uo_uid = uid;

	/* Let the database manager know that this is a new entry */
	stflags = NDBF_NEWNAME;
    }

    /* Convert the information in the user object to a DB record */
    rv = userEncode(uoptr, &reclen, &recptr);
    if (rv) goto err_nomem;

    /* Store the record in the database under the user account name. */
    rv = ndbStoreName(errp, userdb, stflags,
		      0, (char *)uoptr->uo_name, reclen, (char *)recptr);
    if (rv) goto punt;

    FREE(recptr);
    recptr = 0;

    uoptr->uo_flags &= ~(UOF_NEW | UOF_MODIFIED);
    return 0;

  err_nomem:
    eid = NSAUERR1100;
    rv = NSAERRNOMEM;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);

  punt:
    if (recptr) {
	FREE(recptr);
    }
    if ((uoptr->uo_flags & UOF_NEW) && (uid != 0)) {
	/* Free the user id value if we failed after allocating it */
	ndbFreeId(errp, userdb, 0, (char *)uoptr->uo_name, uid);
    }
    return rv;
}
