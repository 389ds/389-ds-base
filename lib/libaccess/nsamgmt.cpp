/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsamgmt.c)
 *
 *	This module contains routines for managing information in a
 *	Netscape authentication database.  An authentication database
 *	consists of a user database and a group database.  This module
 *	implements an authentication database based on Netscape user and
 *	group databases defined in nsuser.h and nsgroup.h, which in turn
 *	are based on the Netscape (server) database implementation
 *	defined in nsdb.h.  The interface for retrieving information
 *	from an authentication database is described separately in
 *	nsadb.h.
 */

#include "base/systems.h"
#include "netsite.h"
#include "base/file.h"
#define __PRIVATE_NSADB
#include "libaccess/nsamgmt.h"
#include "libaccess/nsumgmt.h"
#include "libaccess/nsgmgmt.h"

/*
 * Description (nsadbEnumUsersHelp)
 *
 *	This is a local function that is called by NSDB during user
 *	database enumeration.  It decodes user records into user
 *	objects, and presents them to the caller of nsadbEnumerateUsers(),
 *	via the specified call-back function.  The call-back function
 *	return value may be a negative error code, which will cause
 *	enumeration to stop, and the error code will be returned from
 *	nsadbEnumerateUsers().  If the return value of the call-back
 *	function is not negative, it can contain one or more of the
 *	following flags:
 *
 *		ADBF_KEEPOBJ	- do not free the UserObj_t structure
 *				  that was passed to the call-back function
 *		ADBF_STOPENUM	- stop the enumeration without an error
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
 *	If the call-back returns a negative result, that value is
 *	returned.  If the call-back returns ADBF_STOPENUM, then
 *	-1 is returned, causing the enumeration to stop.  Otherwise
 *	the return value is zero.
 */

typedef struct EnumUserArgs_s EnumUserArgs_t;
struct EnumUserArgs_s {
    void * authdb;
    int (*func)(NSErr_t * ferrp,
		void * authdb, void * argp, UserObj_t * uoptr);
    void * user;
    int rv;
};

static int nsadbEnumUsersHelp(NSErr_t * errp, void * parg,
			      int namelen, char * name,
			      int reclen, char * recptr)
{
    EnumUserArgs_t * ue = (EnumUserArgs_t *)parg;
    UserObj_t * uoptr;			/* user object pointer */
    int rv;

    uoptr = userDecode((NTS_t)name, reclen, (ATR_t)recptr);
    if (uoptr != 0) {
	rv = (*ue->func)(errp, ue->authdb, ue->user, uoptr);
	if (rv >= 0) {

	    /* Count the number of users seen */
	    ue->rv += 1;

	    /* Free the user object unless the call-back says not to */
	    if (!(rv & ADBF_KEEPOBJ)) {
		userFree(uoptr);
	    }
	    /* Return either 0 or -1, depending on ADBF_STOPENUM */
	    rv = (rv & ADBF_STOPENUM) ? -1 : 0;
	}
	else {
	    /* Free the user object in the event of an error */
	    userFree(uoptr);

	    /* Also return the error code */
	    ue->rv = rv;
	}
    }

    return rv;
}

/*
 * Description (nsadbEnumGroupsHelp)
 *
 *	This is a local function that is called by NSDB during group
 *	database enumeration.  It decodes group records into group
 *	objects, and presents them to the caller of nsadbEnumerateGroups(),
 *	via the specified call-back function.  The call-back function
 *	return value may be a negative error code, which will cause
 *	enumeration to stop, and the error code will be returned from
 *	nsadbEnumerateGroups().  If the return value of the call-back
 *	function is not negative, it can contain one or more of the
 *	following flags:
 *
 *		ADBF_KEEPOBJ	- do not free the GroupObj_t structure
 *				  that was passed to the call-back function
 *		ADBF_STOPENUM	- stop the enumeration without an error
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	parg			- pointer to GroupEnumArgs_t structure
 *	namelen			- group record key length including null
 *				  terminator
 *	name			- group record key (group name)
 *	reclen			- length of group record
 *	recptr			- pointer to group record contents
 *
 * Returns:
 *
 *	If the call-back returns a negative result, that value is
 *	returned.  If the call-back returns ADBF_STOPENUM, then
 *	-1 is returned, causing the enumeration to stop.  Otherwise
 *	the return value is zero.
 */

typedef struct EnumGroupArgs_s EnumGroupArgs_t;
struct EnumGroupArgs_s {
    void * authdb;
    int (*func)(NSErr_t * ferrp,
		void * authdb, void * argp, GroupObj_t * goptr);
    void * user;
    int rv;
};

static int nsadbEnumGroupsHelp(NSErr_t * errp, void * parg,
			       int namelen, char * name,
			       int reclen, char * recptr)
{
    EnumGroupArgs_t * eg = (EnumGroupArgs_t *)parg;
    GroupObj_t * goptr;			/* group object pointer */
    int rv;

    goptr = groupDecode((NTS_t)name, reclen, (ATR_t)recptr);
    if (goptr != 0) {
	rv = (*eg->func)(errp, eg->authdb, eg->user, goptr);
	if (rv >= 0) {

	    /* Count the number of groups seen */
	    eg->rv += 1;

	    /* Free the group object unless the call-back says not to */
	    if (!(rv & ADBF_KEEPOBJ)) {
		groupFree(goptr);
	    }
	    /* Return either 0 or -1, depending on ADBF_STOPENUM */
	    rv = (rv & ADBF_STOPENUM) ? -1 : 0;
	}
	else {
	    /* Free the group object in the event of an error */
	    groupFree(goptr);

	    /* Also return the error code */
	    eg->rv = rv;
	}
    }

    return rv;
}

NSPR_BEGIN_EXTERN_C

/*
 * Description (nsadbAddGroupToGroup)
 *
 *	This function adds a child group, C, to the definition of a
 *	parent group P.  This involves updating the group entries of
 *	C and P in the group database.  It also involves updating
 *	the group lists of any user descendants of C, to reflect the
 *	fact that these users are now members of P and P's ancestors.
 *	A check is made for an attempt to create a cycle in the group
 *	hierarchy, and this is rejected as an error.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	pgoptr			- pointer to parent group object
 *	cgoptr			- pointer to child group object
 *
 * Returns:
 *
 *	The return value is zero if group C was not already a direct
 *	member of group P, and was added successfully.  A return value
 *	of +1 indicates that group C was already a direct member of
 *	group P.  A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbAddGroupToGroup(NSErr_t * errp, void * authdb,
			 GroupObj_t * pgoptr, GroupObj_t * cgoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USIList_t gsuper;		/* list of ancestors of group P */
    USIList_t dglist;		/* descendant groups of C */
    GroupObj_t * dgoptr;	/* descendant group object pointer */
    UserObj_t * uoptr;		/* user object pointer */
    USI_t id;			/* current descendant group id */
    int usercount;		/* count of users for descendant */
    USI_t * userlist;		/* pointer to array of descendant user ids */
    USI_t * idlist;		/* pointer to array of descendant group ids */
    int pass;			/* loop pass number */
    int i;			/* loop index */
    int rv;			/* result value */

    /* Is C a direct member of P already? */
    if (usiPresent(&pgoptr->go_groups, cgoptr->go_gid)) {
	/* Yes, indicate that */
	return 0;
    }

    dgoptr = 0;
    uoptr = 0;

    /* Initialize a list of the group descendants of group C */
    UILINIT(&dglist);

    /* Initialize a list of P and its ancestors */
    UILINIT(&gsuper);

    /* Add P to the ancestor list */
    rv = usiInsert(&gsuper, pgoptr->go_gid);
    if (rv < 0) goto punt;

    /* Open user database since the group lists of users may be modified */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    /* Open group database since group entries will be modified */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    /* Merge all the ancestors of group P into the list */
    rv = nsadbSuperGroups(errp, authdb, pgoptr, &gsuper);
    if (rv < 0) goto punt;

    /*
     * Each pass through the following loop visits C and all of C's
     * descendant groups.
     *
     * The first pass checks to see if making group C a member of
     * group P would create a cycle in the group structure.  It does
     * this by examining C and all of its dependents to see if any
     * appear in the list containing P and P's ancestors.
     *
     * The second pass updates the group lists of all users contained
     * in group C to include P and P's ancestors.
     */

    for (pass = 1; pass < 3; ++pass) {

	/* Use the group C as the first descendant */
	id = cgoptr->go_gid;
	dgoptr = cgoptr;

	for (;;) {

	    if (pass == 1) {
		/*
		 * Check for attempt to create a cycle in the group
		 * hierarchy.  See if this descendant is a member of
		 * the list of P and P's ancestors (gsuper).
		 */
		if (usiPresent(&gsuper, id)) {
		    /*
		     * Error - operation would create a cycle
		     * in the group structure.
		     */
		    return -1;
		}
	    }
	    else {

		/*
		 * Merge the list of ancestors of P (gsuper) with the
		 * group lists of any direct user members of the current
		 * descendant group, referenced by dgoptr.
		 */

		/* Get direct user member list size and pointer */
		usercount = UILCOUNT(&dgoptr->go_users);
		userlist = UILLIST(&dgoptr->go_users);

		/* For each direct user member of this descendant ... */
		for (i = 0; i < usercount; ++i) {

		    /* Get a user object for the user */
		    uoptr = userFindByUid(errp,
					  adb->adb_userdb, userlist[i]);
		    if (uoptr == 0) {
			/*
			 * Error - user not found,
			 * databases are inconsistent.
			 */
			rv = -1;
			goto punt;
		    }

		    /* Merge gsuper into the user's group list */
		    rv = uilMerge(&uoptr->uo_groups, &gsuper);
		    if (rv < 0) goto punt;

		    /* Write out the user object */
		    uoptr->uo_flags |= UOF_MODIFIED;
		    rv = userStore(errp, adb->adb_userdb, 0, uoptr);
		    if (rv) goto punt;

		    /* Free the user object */
		    userFree(uoptr);
		    uoptr = 0;
		}
	    }

	    /*
	     * Merge the direct member groups of the current descendant
	     * group into the list of descendants to be processed.
	     */
	    rv = uilMerge(&dglist, &dgoptr->go_groups);
	    if (rv < 0) goto punt;

	    /* Free the group object for the current descendant */
	    if (dgoptr != cgoptr) {
		groupFree(dgoptr);
		dgoptr = 0;
	    }

	    /* Exit the loop if the descendant list is empty */
	    if (UILCOUNT(&dglist) <= 0) break;

	    /* Otherwise remove the next descendant from the list */
	    idlist = UILLIST(&dglist);
	    id = idlist[0];
	    rv = usiRemove(&dglist, id);
	    if (rv < 0) goto punt;

	    /* Now get a group object for this descendant group */
	    dgoptr = groupFindByGid(errp, adb->adb_groupdb, id);
	    if (dgoptr == 0) {
		/* Error - group not found, databases are inconsistent */
		rv = -1;
		goto punt;
	    }
	}
    }

    /* Now add C to P's list of member groups */
    rv = usiInsert(&pgoptr->go_groups, cgoptr->go_gid);
    if (rv < 0) goto punt;

    /* Add P to C's list of parent groups */
    rv = usiInsert(&cgoptr->go_pgroups, pgoptr->go_gid);
    if (rv < 0) goto punt;

    /* Update the database entry for group C */
    cgoptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, cgoptr);
    if (rv) goto punt;

    /* Update the database entry for group P */
    pgoptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, pgoptr);

    return rv;

  punt:
    /* Handle errors */
    UILFREE(&gsuper);
    UILFREE(&dglist);
    if (dgoptr) {
	groupFree(dgoptr);
    }
    if (uoptr) {
	userFree(uoptr);
    }
    return rv;
}

/*
 * Description (nsadbAddUserToGroup)
 *
 *	This function adds a user to a group definition.  This involves
 *	updating the group entry in the group database, and the user
 *	entry in the user database.  The caller provides a pointer to
 *	a user object for the user to be added, a pointer to a group
 *	object for the group being modified, and a handle for the
 *	authentication databases (from nsadbOpen()).
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	goptr			- pointer to group object
 *	uoptr			- pointer to user object
 *
 * Returns:
 *
 *	The return value is zero if the user was not already a direct
 *	member of the group, and was added successfully.  A return value
 *	of +1 indicates that the user was already a direct member of the
 *	group.  A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbAddUserToGroup(NSErr_t * errp, void * authdb,
			GroupObj_t * goptr, UserObj_t * uoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USIList_t nglist;		/* new group list for specified user */
    USIList_t gsuper;		/* groups containing+ the specified group */
    GroupObj_t * aoptr;		/* group object for 'id' group */
    USI_t * idlist;		/* pointer to gsuper gid array */
    USI_t id;			/* current gid from gsuper */
    int rv;			/* result value */

    /* Is the user already a direct member of the group? */
    if (usiPresent(&goptr->go_users, uoptr->uo_uid)) {

	/* Yes, nothing to do */
	return 1;
    }

    /*
     * The user object contains a list of all of the groups that contain
     * the user, either directly or indirectly.  We need to add the
     * specified group and its ancestors to this list.  Each group contains
     * a list of the group's parents, which is used to locate all of the
     * group's ancestors.  As an optimization, we need not consider any
     * ancestors which are already on the user's current group list.
     */

    /*
     * The following loop will deal with two lists of group ids.  One
     * is the list that will become the new group list for the user,
     * which is initialized to the user's current group list.  The other
     * is a list of ancestors of the group to be considered for addition
     * to the user's group list.  This list is initialized to the specified
     * group.
     */

    /* Initialize both lists to be empty */
    UILINIT(&nglist);
    UILINIT(&gsuper);

    /* Make a copy of the user's current group list */
    rv = uilDuplicate(&nglist, &uoptr->uo_groups);
    if (rv < 0) goto punt;

    /* Start the other list with the specified group */
    rv = usiInsert(&gsuper, goptr->go_gid);
    if (rv < 0) goto punt;

    /* Open user database since the group lists of users may be modified */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    /* Open group database since group entries will be modified */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    /* While entries remain on the ancestor list */
    while (UILCOUNT(&gsuper) > 0) {

	/* Get pointer to array of ancestor group ids */
	idlist = UILLIST(&gsuper);

	/* Remove the first ancestor */
	id = idlist[0];
	usiRemove(&gsuper, id);

	/* Is the ancestor on the user's current group list? */
	if (!usiPresent(&uoptr->uo_groups, id)) {

	    /* No, add its parents to the ancestor list */

	    /* Look up the ancestor group (get a group object for it) */
	    aoptr = groupFindByGid(errp, adb->adb_groupdb, id);
	    if (aoptr == 0) {
		/* Error - group not found, database inconsistent */
		rv = -1;
		goto punt;
	    }

	    /* Merge the ancestors parents into the ancestor list */
	    rv = uilMerge(&gsuper, &aoptr->go_pgroups);

	    /* Lose the ancestor group object */
	    groupFree(aoptr);

	    /* See if the merge worked */
	    if (rv < 0) goto punt;
	}

	/* Add the ancestor to the new group list for the user */
	rv = usiInsert(&nglist, id);
	if (rv < 0) goto punt;
    }

    /* Add the user to the group's user member list */
    rv = usiInsert(&goptr->go_users, uoptr->uo_uid);
    if (rv < 0) goto punt;

    /* Replace the user's group list with the new one */
    UILREPLACE(&uoptr->uo_groups, &nglist);
    
    /* Write out the updated user object */
    uoptr->uo_flags |= UOF_MODIFIED;
    rv = userStore(errp, adb->adb_userdb, 0, uoptr);
    if (rv < 0) goto punt;

    /* Write out the updated group object */
    goptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, goptr);
    
    return rv;

  punt:
    /* Handle error */

    /* Free ancestor and new group lists */
    UILFREE(&nglist);
    UILFREE(&gsuper);

    return rv;
}

/*
 * Description (nsadbCreateGroup)
 *
 *	This function creates a new group in a specified authentication
 *	database.  The group is described by a group object.  A group
 *	object can be created by calling nsadbGroupNew().
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	goptr			- pointer to group object
 *
 * Returns:
 */

NSAPI_PUBLIC int nsadbCreateGroup(NSErr_t * errp, void * authdb, GroupObj_t * goptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    int rv;

    /* Open the group database for write access */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    /* Add this group to the database */
    rv = groupStore(errp, adb->adb_groupdb, 0, goptr);

  punt:
    return rv;
}

/*
 * Description (nsadbCreateUser)
 *
 *	This function creates a new user in a specified authentication
 *	database.  The user is described by a user object.  A user
 *	object can be created by calling nsadbUserNew().
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	uoptr			- pointer to user object
 *
 * Returns:
 */

NSAPI_PUBLIC int nsadbCreateUser(NSErr_t * errp, void * authdb, UserObj_t * uoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    int rv;

    /* Open the user database for write access */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    /* Add this user to the database */
    rv = userStore(errp, adb->adb_userdb, 0, uoptr);

  punt:
    return rv;
}

/*
 * Description (nsadbEnumerateUsers)
 *
 *	This function is called to enumerate all of the users in a
 *	given authentication database to a call-back function specified
 *	by the caller.  The call-back function is provided with a
 *	handle for the authentication database, an opaque value provided
 *	by the caller, and a pointer to a user object.  See the
 *	description of nsadbEnumUsersHelp above for the interpretation
 *	of the call-back function's return value.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	argp			- opaque value for call-back function
 *	func			- pointer to call-back function
 *
 * Returns:
 *
 *	If the call-back function returns a negative error code, this
 *	value is returned.  A negative value may also be returned if
 *	nsadb encounters an error.  Otherwise the result is the number
 *	of users enumerated.
 */

NSAPI_PUBLIC int nsadbEnumerateUsers(NSErr_t * errp, void * authdb, void * argp,
#ifdef UnixWare
	ArgFn_EnumUsers func) /* for ANSI C++ standard, see nsamgmt.h */
#else
	int (*func)(NSErr_t * ferrp, void * authdb, void * parg, UserObj_t * uoptr))
#endif
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    EnumUserArgs_t args;		/* arguments for enumeration helper */
    int rv;				/* result value */

    /* Open the users subdatabase for read access */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UREAD);
    if (rv < 0) goto punt;

    args.authdb = authdb;
    args.func = func;
    args.user = argp;
    args.rv = 0;

    rv = ndbEnumerate(errp, adb->adb_userdb,
		      NDBF_ENUMNORM, (void *)&args, nsadbEnumUsersHelp);
    if (rv < 0) goto punt;

    rv = args.rv;

  punt:
    return rv;
}

/*
 * Description (nsadbEnumerateGroups)
 *
 *	This function is called to enumerate all of the groups in a
 *	given authentication database to a call-back function specified
 *	by the caller.  The call-back function is provided with a
 *	handle for the authentication database, an opaque value provided
 *	by the caller, and a pointer to a group object.  See the
 *	description of nsadbEnumGroupsHelp above for the interpretation
 *	of the call-back function's return value.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	argp			- opaque value for call-back function
 *	func			- pointer to call-back function
 *
 * Returns:
 *
 *	If the call-back function returns a negative error code, this
 *	value is returned.  A negative value may also be returned if
 *	nsadb encounters an error.  Otherwise the result is the number
 *	of groups enumerated.
 */

NSAPI_PUBLIC int nsadbEnumerateGroups(NSErr_t * errp, void * authdb, void * argp,
#ifdef UnixWare
	ArgFn_EnumGroups func) /* for ANSI C++ standard, see nsamgmt.h */
#else
	int (*func)(NSErr_t * ferrp, void * authdb, void * parg, GroupObj_t * goptr))
#endif
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    EnumGroupArgs_t args;
    int rv;				/* result value */

    /* Open group database for read access */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GREAD);
    if (rv < 0) goto punt;

    args.authdb = authdb;
    args.func = func;
    args.user = argp;
    args.rv = 0;

    rv = ndbEnumerate(errp, adb->adb_groupdb,
		      NDBF_ENUMNORM, (void *)&args, nsadbEnumGroupsHelp);
    if (rv < 0) goto punt;

    rv = args.rv;

  punt:
    return rv;
}

/*
 * Description (nsadbIsUserInGroup)
 *
 *	This function tests whether a given user id is a member of the
 *	group associated with a specified group id.  The caller may
 *	provide a list of group ids for groups to which the user is
 *	already known to belong, and this may speed up the check.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	uid			- user id
 *	gid			- group id
 *	ngroups			- number of group ids in grplist
 *	grplist			- groups the user is known to belong to
 *
 * Returns:
 *
 *	The return value is +1 if the user is found to belong to the
 *	indicated group, or 0 if the user does not belong to the group.
 *	An error is indicated by a negative return value.
 */

NSAPI_PUBLIC int nsadbIsUserInGroup(NSErr_t * errp, void * authdb,
		       USI_t uid, USI_t gid, int ngroups, USI_t * grplist)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USIList_t dglist;			/* descendant group list */
    GroupObj_t * goptr = 0;		/* group object pointer */
    USI_t * idlist;			/* pointer to array of group ids */
    USI_t tgid;				/* test group id */
    int i;				/* loop index */
    int rv;				/* result value */

    UILINIT(&dglist);

    /* Open group database for read access */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GREAD);
    if (rv < 0) goto punt;

    for (tgid = gid;;) {

	/* Get a group object for this group id */
	goptr = groupFindByGid(errp, adb->adb_groupdb, tgid);
	if (goptr == 0) {
	    /* Error - group id not found, databases are inconsistent */
	    rv = -1;
	    goto punt;
	}

	/* Is the user a direct member of this group? */
	if (usiPresent(&goptr->go_users, uid)) goto is_member;

	/*
	 * Is there any group to which the user is already known to
	 * belong that is a direct group member of this group?  If so,
	 * the user is also a member of this group.
	 */

	/* Scan list of groups to which the user is known to belong */
	for (i = 0; i < ngroups; ++i) {

	    if (usiPresent(&goptr->go_groups, grplist[i])) goto is_member;
	}

	/* Merge group member list of this group with descendants list */
	rv = uilMerge(&dglist, &goptr->go_groups);
	if (rv < 0) goto punt;

	/*
	 * If descendants list is empty, the user is not contained in
	 * the specified group.
	 */
	if (UILCOUNT(&dglist) <= 0) {
	    rv = 0;
	    goto punt;
	}

	/* Remove the next id from the descendants list */
	idlist = UILLIST(&dglist);
	tgid = idlist[0];

	rv = usiRemove(&dglist, tgid);
	if (rv < 0) goto punt;

	groupFree(goptr);
	goptr = 0;
    }

  is_member:
    rv = 1;

  punt:
    if (goptr) {
	groupFree(goptr);
    }
    UILFREE(&dglist);
    return rv;
}

/*
 * Description (nsadbModifyGroup)
 *
 *	This function is called to write modifications to a group to
 *	a specified authentication database.  The group is assumed to
 *	already exist in the database.  Information about the group
 *	is passed in a group object.  This function should not be used
 *	to alter the lists of group members or parents.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	goptr			- pointer to modified group object
 *
 * Returns:
 *
 *	The return value is zero if the group information is successfully
 *	updated.  An error is indicated by a negative return value, and
 *	an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbModifyGroup(NSErr_t * errp, void * authdb, GroupObj_t * goptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    int rv;

    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    rv = groupStore(errp, adb->adb_groupdb, 0, goptr);

  punt:
    return rv;
}

/*
 * Description (nsadbModifyUser)
 *
 *	This function is called to write modifications to a user to
 *	a specified authentication database.  The user is assumed to
 *	already exist in the database.  Information about the user
 *	is passed in a user object.  This function should not be used
 *	to modify the list of groups which contain the user.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	uoptr			- pointer to modified user object
 *
 * Returns:
 *
 *	The return value is zero if the user information is successfully
 *	updated.  An error is indicated by a negative return value, and
 *	an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbModifyUser(NSErr_t * errp, void * authdb, UserObj_t * uoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    int rv;

    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    rv = userStore(errp, adb->adb_userdb, 0, uoptr);

  punt:
    return rv;
}

/*
 * Description (nsadbRemoveGroup)
 *
 *	This function is called to remove a given group name from
 *	a specified authentication database.  This can cause updates
 *	to both the user and group subdatabases.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	name			- pointer to name of group to remove
 *
 * Returns:
 *
 *	The return value is zero if the group information is successfully
 *	removed.  An error is indicated by a negative return value, and
 *	an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbRemoveGroup(NSErr_t * errp, void * authdb, char * name)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    UserObj_t * uoptr = 0;		/* user object pointer */
    GroupObj_t * goptr = 0;		/* group object pointer */
    GroupObj_t * ogoptr = 0;		/* other group object pointer */
    char * ugname;			/* user or group name */
    USI_t * list;			/* pointer into user/group id list */
    int cnt;				/* count of user or group ids */
    int i;				/* loop index */
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Open the groups subdatabase for write access */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    /* Look up the group to be removed, and get a group object */
    rv = nsadbFindByName(errp, authdb, name, AIF_GROUP, (void **)&goptr);
    if (rv != AIF_GROUP) {
	if (rv < 0) goto punt;
	goto err_nogroup;
    }

    /* Mark the group for delete */
    goptr->go_flags |= GOF_DELPEND;

    /* Does the specified group belong to any groups? */
    cnt = UILCOUNT(&goptr->go_pgroups);
    if (cnt > 0) {

	/* Yes, for each parent group ... */
	for (i = 0; i < cnt; ++i) {

	    /* Note that nsadbRemGroupFromGroup() will shrink this list */
	    list = UILLIST(&goptr->go_pgroups);

	    /* Get group name associated with the group id */
	    rv = nsadbIdToName(errp, authdb, *list, AIF_GROUP, &ugname);
	    if (rv < 0) goto punt;

	    /* Look up the group by name and get a group object for it */
	    rv = nsadbFindByName(errp,
				 authdb, ugname, AIF_GROUP, (void **)&ogoptr);
	    if (rv < 0) goto punt;

	    /* Remove the specified group from the parent group */
	    rv = nsadbRemGroupFromGroup(errp, authdb, ogoptr, goptr);
	    if (rv < 0) goto punt;

	    /* Free the parent group object */
	    groupFree(ogoptr);
	    ogoptr = 0;
	}
    }

    /* Are there any group members of this group? */
    cnt = UILCOUNT(&goptr->go_groups);
    if (cnt > 0) {

	/* For each group member of the group ... */

	for (i = 0; i < cnt; ++i) {

	    /* Note that nsadbRemGroupFromGroup() will shrink this list */
	    list = UILLIST(&goptr->go_groups);

	    /* Get group name associated with the group id */
	    rv = nsadbIdToName(errp, authdb, *list, AIF_GROUP, &ugname);
	    if (rv < 0) goto punt;

	    /* Look up the group by name and get a group object for it */
	    rv = nsadbFindByName(errp,
				 authdb, ugname, AIF_GROUP, (void **)&ogoptr);
	    if (rv < 0) goto punt;

	    /* Remove member group from the specified group */
	    rv = nsadbRemGroupFromGroup(errp, authdb, goptr, ogoptr);
	    if (rv < 0) goto punt;

	    /* Free the member group object */
	    groupFree(ogoptr);
	    ogoptr = 0;
	}
    }

    /* Are there any direct user members of this group? */
    cnt = UILCOUNT(&goptr->go_users);
    if (cnt > 0) {

	/* Yes, open users subdatabase for write access */
	rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
	if (rv < 0) goto punt;

	/* For each user member of the group ... */
	for (i = 0; i < cnt; ++i) {

	    /* Note that nsadbRemUserFromGroup() will shrink this list */
	    list = UILLIST(&goptr->go_users);

	    /* Get user name associated with the user id */
	    rv = nsadbIdToName(errp, authdb, *list, AIF_USER, &ugname);
	    if (rv < 0) goto punt;

	    /* Look up the user by name and get a user object for it */
	    rv = nsadbFindByName(errp,
				 authdb, ugname, AIF_USER, (void **)&uoptr);
	    if (rv < 0) goto punt;

	    /* Remove user from the group */
	    rv = nsadbRemUserFromGroup(errp, authdb, goptr, uoptr);
	    if (rv < 0) goto punt;

	    /* Free the member user object */
	    userFree(uoptr);
	    uoptr = 0;
	}
    }

    /* Free the group object for the specified group */
    groupFree(goptr);
    goptr = 0;

    /* Now we can remove the group entry */
    rv = groupRemove(errp, adb->adb_groupdb, 0, (NTS_t)name);

    return rv;

  err_nogroup:
    eid = NSAUERR4100;
    rv = NSAERRNAME;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 2, adb->adb_dbname, name);
    goto punt;

  punt:
    /* Free any user or group objects that we created */
    if (ogoptr != 0) {
	groupFree(ogoptr);
    }
    if (uoptr != 0) {
	userFree(uoptr);
    }
    if (goptr != 0) {
	groupFree(goptr);
    }
    return rv;
}

/*
 * Description (nsadbRemoveUser)
 *
 *	This function is called to remove a given user name from
 *	a specified authentication database.  This can cause updates
 *	to both the user and user subdatabases.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	name			- pointer to name of user to remove
 *
 * Returns:
 *
 *	The return value is zero if the user information is successfully
 *	removed.  An error is indicated by a negative return value, and
 *	an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbRemoveUser(NSErr_t * errp, void * authdb, char * name)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    UserObj_t * uoptr = 0;		/* user object pointer */
    GroupObj_t * goptr = 0;		/* group object pointer */
    char * gname;			/* group name */
    USI_t * list;			/* pointer into group id list */
    int gcnt;				/* number of groups containing user */
    int i;				/* loop index */
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Open the users subdatabase for write access */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    /* Look up the user to be removed, and get a user object */
    rv = nsadbFindByName(errp, authdb, name, AIF_USER, (void **)&uoptr);
    if (rv != AIF_USER) {
	if (rv < 0) goto punt;
	goto err_nouser;
    }

    /* Mark the user for delete */
    uoptr->uo_flags |= UOF_DELPEND;

    /* Does this user belong to any groups? */
    gcnt = UILCOUNT(&uoptr->uo_groups);
    if (gcnt > 0) {

	/* Yes, get pointer to list of group ids */
	list = UILLIST(&uoptr->uo_groups);

	/* Open groups subdatabase for write access */
	rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
	if (rv < 0) goto punt;

	/* For each group that the user belongs to ... */
	for (i = 0; i < gcnt; ++i) {

	    /* Get group name associated with the group id */
	    rv = nsadbIdToName(errp, authdb, *list, AIF_GROUP, &gname);
	    if (rv < 0) goto punt;

	    /* Look up the group by name and get a group object for it */
	    rv = nsadbFindByName(errp,
				 authdb, gname, AIF_GROUP, (void **)&goptr);
	    if (rv < 0) goto punt;

	    /* Remove user from group if it's a direct member */
	    rv = nsadbRemUserFromGroup(errp, authdb, goptr, uoptr);
	    if (rv < 0) goto punt;

	    /* Free the group object */
	    groupFree(goptr);
	    goptr = 0;

	    ++list;
	}
    }

#ifdef CLIENT_AUTH
    /* Remove certificate mapping for user, if any */
    rv = nsadbRemoveUserCert(errp, authdb, name);
#endif

    /* Free the user object */
    userFree(uoptr);

    /* Now we can remove the user entry */
    rv = userRemove(errp, adb->adb_userdb, 0, (NTS_t)name);

    return rv;

  err_nouser:
    eid = NSAUERR4000;
    rv = NSAERRNAME;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 2, adb->adb_dbname, name);
    goto punt;

  punt:
    if (goptr != 0) {
	groupFree(goptr);
    }
    if (uoptr != 0) {
	userFree(uoptr);
    }
    return rv;
}

/*
 * Description (nsadbRemGroupFromGroup)
 *
 *	This function removes a given group C from a parent group P.
 *	The group C must be a direct member of the group P.  However,
 *	group C may also be a member of one or more of P's ancestor or
 *	descendant groups, and this function deals with that.  The
 *	group entries for C and P are updated in the group database.
 *	But the real work is updating the groups lists of all of the
 *	users contained in C.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	pgoptr			- pointer to parent group object
 *	cgoptr			- pointer to child group object
 *
 * Returns:
 *
 *	The return value is zero if group C was a direct member of
 *	group P, and was removed successfully.  A return value of +1
 *	indicates that group C was not a direct member of the group P.
 *	A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbRemGroupFromGroup(NSErr_t * errp, void * authdb,
			   GroupObj_t * pgoptr, GroupObj_t * cgoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USIList_t dglist;		/* list of descendant groups of C */
    GroupObj_t * dgoptr;	/* descendant group object pointer */
    UserObj_t * uoptr;		/* user object pointer */
    USI_t * gidlist;		/* pointer to group id array */
    USI_t * userlist;		/* pointer to array of descendant user ids */
    USI_t dgid;			/* descendant group id */
    int iusr;			/* index on descendant user list */
    int usercnt;		/* count of descendant users */
    int igrp;			/* index of group in user group id list */
    int rv;			/* result value */

    dgoptr = 0;
    uoptr = 0;

    /* Initialize a list of descendant groups of C */
    UILINIT(&dglist);

    /* Is group C a direct member of group P? */
    if (!usiPresent(&pgoptr->go_groups, cgoptr->go_gid)) {

	/* No, nothing to do */
	return 1;
    }

    /* Remove group C from group P's group member list */
    rv = usiRemove(&pgoptr->go_groups, cgoptr->go_gid);
    if (rv < 0) goto punt;

    /* Remove group P from group C's parent group list */
    rv = usiRemove(&cgoptr->go_pgroups, pgoptr->go_gid);
    if (rv < 0) goto punt;

    /* Open user database since the group lists of users may be modified */
    rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
    if (rv < 0) goto punt;

    /* Open group database since group entries will be modified */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
    if (rv < 0) goto punt;

    /* Write out the updated group C object */
    cgoptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, cgoptr);
    if (rv) goto punt;

    /* Write out the updated group P object */
    pgoptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, pgoptr);
    if (rv) goto punt;

    /* Now check the group lists of all users contained in group C */
    dgoptr = cgoptr;
    dgid = cgoptr->go_gid;

    for (;;) {

	/* Scan the direct user members of this descendant group */
	usercnt = UILCOUNT(&dgoptr->go_users);
	userlist = UILLIST(&dgoptr->go_users);

	for (iusr = 0; iusr < usercnt; ++iusr) {

	    /* Get a user object for this user member */
	    uoptr = userFindByUid(errp, adb->adb_userdb, userlist[iusr]);
	    if (uoptr == 0) {
		/* Error - user id not found, databases are inconsistent */
		rv = -1;
		goto punt;
	    }

	    /* Scan the group list for this user */
	    for (igrp = 0; igrp < UILCOUNT(&uoptr->uo_groups); ) {

		gidlist = UILLIST(&uoptr->uo_groups);

		/* Is the user a member of this group? */
		if (nsadbIsUserInGroup(errp, authdb,
				       uoptr->uo_uid, gidlist[igrp],
				       igrp, gidlist)) {

		    /* Yes, step to next group id */
		    ++igrp;
		}
		else {
		    /*
		     * No, remove it from the user's list of groups.  The
		     * next group id to consider will be shifted into the
		     * igrp position when the current id is removed.
		     */
		    rv = usiRemove(&uoptr->uo_groups, gidlist[igrp]);
		    if (rv < 0) goto punt;
		    uoptr->uo_flags |= UOF_MODIFIED;
		}
	    }

	    /* Write out the user object if it was changed */
	    if (uoptr->uo_flags & UOF_MODIFIED) {
		rv = userStore(errp, adb->adb_userdb, 0, uoptr);
		if (rv < 0) goto punt;
	    }

	    /* Free the user object */
	    userFree(uoptr);
	    uoptr = 0;
	}

	/*
	 * Merge the direct member groups of this group into the
	 * descendants list.
	 */
	rv = uilMerge(&dglist, &dgoptr->go_groups);
	if (rv < 0) goto punt;

	/* Free this descendant group object */
	if (dgoptr != cgoptr) {
	    groupFree(dgoptr);
	    dgoptr = 0;
	}

	/* If the descendants list is empty, we're done */
	if (UILCOUNT(&dglist) <= 0) break;

	/* Remove the next group id from the descendants list */
	gidlist = UILLIST(&dglist);
	dgid = gidlist[0];
	rv = usiRemove(&dglist, dgid);
	if (rv < 0) goto punt;

	/* Get a group object for this descendant group */
	dgoptr = groupFindByGid(errp, adb->adb_groupdb, dgid);
	if (dgoptr == 0) {
	    /* Error - group id not found, databases are inconsistent */
	    rv = -1;
	    goto punt;
	}
    }

    UILFREE(&dglist);
    return 0;

  punt:
    if (dgoptr) {
	groupFree(dgoptr);
    }
    if (uoptr) {
	userFree(uoptr);
    }
    UILFREE(&dglist);
    return rv;
}

/*
 * Description (nsadbRemUserFromGroup)
 *
 *	This function removes a given user from a specified group G.
 *	The user must be a direct member of the group.  However, the
 *	user may also be a member of one or more of G's descendant
 *	groups, and this function deals with that.  The group entry
 *	for G is updated in the group database, with the user removed
 *	from its user member list.  The user entry is updated in the
 *	user database, with an updated list of all groups which now
 *	contain the user.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	goptr			- pointer to group object
 *	uoptr			- pointer to user object
 *
 * Returns:
 *
 *	The return value is zero if the user was a direct member of the
 *	group, and was removed successfully.  A return value of +1
 *	indicates that the user was not a direct member of the
 *	group.  A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbRemUserFromGroup(NSErr_t * errp, void * authdb,
			  GroupObj_t * goptr, UserObj_t * uoptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USI_t * idlist;		/* pointer to user group id array */
    USI_t tgid;			/* test group id */
    int igrp;			/* position in user group list */
    int rv;			/* result value */

    /* Is the user a direct member of the group? */
    if (!usiPresent(&goptr->go_users, uoptr->uo_uid)) {

	/* No, nothing to do */
	return 1;
    }

    /* Remove the user from the group's user member list */
    rv = usiRemove(&goptr->go_users, uoptr->uo_uid);
    if (rv < 0) goto punt;

    /* If the user object is pending deletion, no need to open databases */
    if (!(uoptr->uo_flags & UOF_DELPEND)) {

	/*
	 * Open user database since the group list of the user
	 * will be modified.
	 */
	rv = nsadbOpenUsers(errp, authdb, ADBF_UWRITE);
	if (rv < 0) goto punt;

	/* Open group database since group entries will be modified */
	rv = nsadbOpenGroups(errp, authdb, ADBF_GWRITE);
	if (rv < 0) goto punt;
    }

    /*
     * Write out the updated group object.  This must be done here
     * because nsadbIsUserInGroup() in the loop below will read the
     * entry for this group, and it needs to reflect the user's
     * removal from being a direct member of the group.  This does
     * not preclude the possibility that the user will still be an
     * indirect member of this group.
     */
    goptr->go_flags |= GOF_MODIFIED;
    rv = groupStore(errp, adb->adb_groupdb, 0, goptr);
    if (rv) goto punt;

    /* If a delete is pending on the user, we're done */
    if (uoptr->uo_flags & UOF_DELPEND) goto punt;

    /*
     * Begin loop to check whether user is still a member of each
     * of the groups in its group list.  Note that the group list
     * may shrink during an iteration of the loop.
     */

    for (igrp = 0; igrp < UILCOUNT(&uoptr->uo_groups); ) {

	/* Get pointer to the user's array of group ids */
	idlist = UILLIST(&uoptr->uo_groups);

	/* Get the group id of the next group to consider */
	tgid = idlist[igrp];

	/* Is the user a member of this group? */
	if (nsadbIsUserInGroup(errp, authdb,
			       uoptr->uo_uid, tgid, igrp, idlist)) {

	    /* Yes, step to next group id */
	    ++igrp;
	}
	else {

	    /*
	     * No, remove it from the user's list of groups.  The
	     * next group id to consider will be shifted into the
	     * igrp position when the current id is removed.
	     */
	    rv = usiRemove(&uoptr->uo_groups, tgid);
	    if (rv < 0) goto punt;
	}
    }

    /* Write out the updated user object */
    uoptr->uo_flags |= UOF_MODIFIED;
    rv = userStore(errp, adb->adb_userdb, 0, uoptr);

  punt:
    return rv;
}

/*
 * Description (nsadbSuperGroups)
 *
 *	This function builds a list of the group ids for all groups
 *	which contain, directly or indirectly, a specified group as
 *	a subgroup.  We call these the supergroups of the specified
 *	group.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle for authentication databases
 *	goptr			- pointer to group object
 *	gsuper			- pointer to list to contain supergroups
 *				  (caller must initialize)
 *
 * Returns:
 *
 *	Returns the number of elements in gsuper if successful.  An
 *	error is indicated by a negative return value.
 */

NSAPI_PUBLIC int nsadbSuperGroups(NSErr_t * errp, void * authdb,
		     GroupObj_t * goptr, USIList_t * gsuper)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    USIList_t aglist;			/* ancestor group id list */
    GroupObj_t * aoptr;			/* ancestor group object pointer */
    USI_t * idlist;			/* pointer to array of group ids */
    USI_t id;				/* current group id */
    int rv;				/* result value */

    /* Initialize an empty ancestor group list */
    UILINIT(&aglist);

    /* Enter loop with specified group as first ancestor */
    id = goptr->go_gid;
    aoptr = goptr;

    /* Open group database for read access */
    rv = nsadbOpenGroups(errp, authdb, ADBF_GREAD);
    if (rv < 0) goto punt;

    /* Loop until the ancestor list is empty */
    for (;;) {

	/* Merge parent groups of current ancestor into ancestor list */
	rv = uilMerge(&aglist, &aoptr->go_pgroups);
	if (rv < 0) goto punt;

	/* Also merge parent groups into the result list */
	rv = uilMerge(gsuper, &aoptr->go_pgroups);
	if (rv < 0) goto punt;

	/* Free the ancestor group object (but not the original) */
	if (aoptr != goptr) {
	    groupFree(aoptr);
	    aoptr = 0;
	}

	/* Exit the loop if the ancestor list is empty */
	if (UILCOUNT(&aglist) <= 0) break;

	/* Get pointer to array of ancestor group ids */
	idlist = UILLIST(&aglist);

	/* Remove the first ancestor */
	id = idlist[0];
	rv = usiRemove(&aglist, id);

	/* Get a group object for the ancestor */
	aoptr = groupFindByGid(errp, adb->adb_groupdb, id);
	if (aoptr == 0) {
	    /* Error - group not found, database inconsistent */
	    rv = -1;
	    goto punt;
	}
    }

    return UILCOUNT(gsuper);

  punt:
    /* Handle error */

    /* Free ancestor list */
    UILFREE(&aglist);

    return rv;
}

NSPR_END_EXTERN_C

