/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsuser.c)
 *
 *	This module contains routines for accessing information in a
 *	Netscape user database.  User information is returned in the
 *	form of a user object (UserObj_t), defined in nsauth.h.
 */

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#define __PRIVATE_NSUSER
#include "libaccess/nsuser.h"

/* Authentication facility name for error frame generation */
char * NSAuth_Program = "NSAUTH";

/*
 * Description (userDecode)
 *
 *	This function decodes an external user DB record into a dynamically
 *	allocated UserObj_t structure.  The DB record is encoded as an
 *	attribute record as defined in attrec.h.
 *
 * Arguments:
 *
 *	name		- pointer to user account name string
 *	ureclen		- length of the user DB record, in octets
 *	urecptr		- pointer to user DB record
 *
 * Returns:
 *
 *	A pointer to the allocated UserObj_t structure is returned.
 */

UserObj_t * userDecode(NTS_t name, int ureclen, ATR_t urecptr)
{
    ATR_t cp = urecptr;			/* current pointer into DB record */
    USI_t tag;				/* attribute tag */
    USI_t len;				/* attribute value encoding length */
    USI_t gcnt;				/* number of group ids */
    USI_t * gids;			/* pointer to array of group ids */
    int i;				/* group id index */
    UserObj_t * uoptr;			/* user object pointer */

    /* Allocate a user object structure */
    uoptr = (UserObj_t *)MALLOC(sizeof(UserObj_t));
    if (uoptr) {

	uoptr->uo_name = (unsigned char *) STRDUP((char *)name);
	uoptr->uo_pwd = 0;
	uoptr->uo_uid = 0;
	uoptr->uo_flags = 0;
	uoptr->uo_rname = 0;
	UILINIT(&uoptr->uo_groups);

	/* Parse user DB record */
	while ((cp - urecptr) < ureclen) {

	    /* Get the attribute tag */
	    cp = USIDECODE(cp, &tag);

	    /* Get the length of the encoding of the attribute value */
	    cp = USIDECODE(cp, &len);

	    /* Process this attribute */
	    switch (tag) {

	      case UAT_PASSWORD:	/* encrypted password */
		cp = NTSDECODE(cp, &uoptr->uo_pwd);
		break;

	      case UAT_UID:		/* user id */
		cp = USIDECODE(cp, &uoptr->uo_uid);
		break;

	      case UAT_ACCFLAGS:	/* account flags */
		cp = USIDECODE(cp, &uoptr->uo_flags);
		break;

	      case UAT_REALNAME:	/* real name of user */
		cp = NTSDECODE(cp, &uoptr->uo_rname);
		break;

	      case UAT_GROUPS:		/* groups which include user */

		/* First get the number of group ids following */
		cp = USIDECODE(cp, &gcnt);

		if (gcnt > 0) {

		    /* Allocate space for group ids */
		    gids = usiAlloc(&uoptr->uo_groups, gcnt);
		    if (gids) {
			for (i = 0; i < gcnt; ++i) {
			    cp = USIDECODE(cp, gids + i);
			}
		    }
		}
		break;

	      default:			/* unrecognized attribute */
		/* Just skip it */
		cp += len;
		break;
	    }
	}
    }

    return uoptr;
}

/*
 * Description (userEnumHelp)
 *
 *	This is a local function that is called by NSDB during user
 *	database enumeration.  It decodes user records into user
 *	objects, and presents them to the caller of userEnumerate().
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	parg			- pointer to UserEnumArgs_t structure
 *	namelen			- user record key length including null
 *				  terminator
 *	name			- user record key (user account name)
 *	reclen			- length of user record
 *	recptr			- pointer to user record contents
 *
 * Returns:
 *
 *	Returns whatever value is returned from the upcall to the caller
 *	of userEnumerate().
 */

static int userEnumHelp(NSErr_t * errp, void * parg,
			int namelen, char * name, int reclen, char * recptr)
{
    UserEnumArgs_t * ue = (UserEnumArgs_t *)parg;
    UserObj_t * uoptr;			/* user object pointer */
    int rv;

    uoptr = userDecode((NTS_t)name, reclen, (ATR_t)recptr);

    rv = (*ue->func)(errp, ue->user, uoptr);

    if (!(ue->flags & UOF_ENUMKEEP)) {
	userFree(uoptr);
    }

    return rv;
}

/*
 * Description (userEnumerate)
 *
 *	This function enumerates all of the users in a specified user
 *	database, calling a caller-specified function with a user object
 *	for each user in the database.  A 'flags' value of UOF_ENUMKEEP
 *	can be specified to keep the user objects around (not free them)
 *	after the caller's function returns.  Otherwise, each user
 *	object is freed after being presented to the caller's function.
 *	The 'argp' argument is an opaque pointer, which is passed to
 *	the caller's function as 'parg' on each call, along with a
 *	user object pointer.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	flags			- bit flags:
 *					UOF_ENUMKEEP - keep user objects
 *	argp			- passed to 'func' as 'parg'
 *	func			- pointer to caller's enumeration function
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code, and an error frame is generated if an error
 *	frame list was provided by the caller.
 */

int userEnumerate(NSErr_t * errp, void * userdb, int flags, void * argp,
		  int (*func)(NSErr_t * ferrp, void * parg, UserObj_t * uoptr))
{
    int rv;
    UserEnumArgs_t args;

    args.userdb = userdb;
    args.flags = flags;
    args.func = func;
    args.user = argp;

    rv = ndbEnumerate(errp,
		      userdb, NDBF_ENUMNORM, (void *)&args, userEnumHelp);

    return rv;
}

/*
 * Description (userFindByName)
 *
 *	This function looks up a user record for a specified user account
 *	name, converts the user record to the internal user object form,
 *	and returns a pointer to the user object.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	name			- user account name to find
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a user object
 *	for the specified user.  Otherwise it is 0, and an error frame
 *	is generated if an error frame list was provided by the caller.
 */

UserObj_t * userFindByName(NSErr_t * errp, void * userdb, NTS_t name)
{
    UserObj_t * uoptr = 0;
    ATR_t urecptr;
    int ureclen;
    int rv;

    /* Look up the user name in the database */
    rv = ndbFindName(errp, userdb, 0, (char *) name, &ureclen, (char **)&urecptr);
    if (rv == 0) {

	/* Got the user record.  Decode into a user object. */
	uoptr = userDecode(name, ureclen, urecptr);
    }

    return uoptr;
}

/*
 * Description (userFindByUid)
 *
 *	This function looks up a user record for a specified user id,
 *	converts the user record to the internal user object form, and
 *	returns a pointer to the user object.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	userdb			- handle for user DB access
 *	uid			- user id to find
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a user object
 *	for the specified user.  Otherwise it is 0, and an error frame
 *	is generated if an error frame list was provided by the caller.
 */

UserObj_t * userFindByUid(NSErr_t * errp, void * userdb, USI_t uid)
{
    UserObj_t * uoptr = 0;
    NTS_t name;
    ATR_t urecptr;
    int ureclen;
    int rv;

    /* Get the user account name corresponding to the uid */
    rv = ndbIdToName(errp, userdb, uid, 0, (char **)&name);
    if (rv == 0) {

	rv = ndbFindName(errp, userdb, 0, (char *)name, &ureclen, (char **)&urecptr);
	if (rv == 0) {

	    /* Got the user record.  Decode into a user object. */
	    uoptr = userDecode(name, ureclen, urecptr);
	}
    }

    return uoptr;
}

/*
 * Description (userFree)
 *
 *	This function is called to free a user object.  User objects
 *	are not automatically freed when a user database is closed.
 *
 * Arguments:
 *
 *	uoptr			- user object pointer
 *
 */

NSAPI_PUBLIC void userFree(UserObj_t * uoptr)
{
    if (uoptr) {

	if (uoptr->uo_name) FREE(uoptr->uo_name);
	if (uoptr->uo_pwd) FREE(uoptr->uo_pwd);
	if (uoptr->uo_rname) FREE(uoptr->uo_rname);
	UILFREE(&uoptr->uo_groups);
	FREE(uoptr);
    }
}
