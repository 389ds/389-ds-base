/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsgmgmt.c)
 *
 *	This module contains routines for managing information in a
 *	Netscape group database.  Information for a particular group
 *	is modified by retrieving the current information in the form
 *	of a group object (GroupObj_t), calling functions in this module,
 *	to modify the group object, and then calling groupStore() to
 *	write the information in the group object back to the database.
 */

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#include "libaccess/nsdbmgmt.h"
#define __PRIVATE_NSGROUP
#include "libaccess/nsgmgmt.h"

/*
 * Description (groupAddMember)
 *
 *	This function adds a member to a group object.  The member may
 *	be another group or a user, expressed as a group id or user id,
 *	respectively.  The 'isgid' argument is non-zero if the new
 *	member is a group, or zero if it is a user.
 *
 * Arguments:
 *
 *	goptr			- group object pointer
 *	isgid			- non-zero if 'id' is a group id
 *				  zero if 'id' is a user id
 *	id			- group or user id to be added
 *
 * Returns:
 *
 *	Returns zero if the specified member is already a direct member
 *	of the group.  Returns one if the member was added successfully.
 */

NSAPI_PUBLIC int groupAddMember(GroupObj_t * goptr, int isgid, USI_t id)
{
    USIList_t * uilptr;
    int rv = 0;

    /* Point to the relevant uid or gid list */
    uilptr = (isgid) ? &goptr->go_groups : &goptr->go_users;

    /* Add the id to the selected list */
    rv = usiInsert(uilptr, id);
    if (rv > 0) {
	goptr->go_flags |= GOF_MODIFIED;
    }

    return rv;
}

/*
 * Description (groupCreate)
 *
 *	This function creates a group object, using information about
 *	the group provided by the caller.  The strings passed for the
 *	group name and description may be on the stack.  The group id
 *	is set to zero, but the group object is marked as being new.
 *	A group id will be assigned when groupStore() is called to add
 *	the group to a group database.
 *
 * Arguments:
 *
 *	name		- pointer to group name string
 *	desc		- pointer to group description string
 *
 * Returns:
 *
 *	A pointer to a dynamically allocated GroupObj_t structure is
 *	returned.
 */

NSAPI_PUBLIC GroupObj_t * groupCreate(NTS_t name, NTS_t desc)
{
    GroupObj_t * goptr;		/* group object pointer */

    goptr = (GroupObj_t *)MALLOC(sizeof(GroupObj_t));
    if (goptr) {
	goptr->go_name = (NTS_t)STRDUP((char *)name);
	goptr->go_gid = 0;
	goptr->go_flags = (GOF_MODIFIED | GOF_NEW);
	if (desc) {
	    goptr->go_desc = (desc) ? (NTS_t)STRDUP((char *)desc) : 0;
	}
	UILINIT(&goptr->go_users);
	UILINIT(&goptr->go_groups);
	UILINIT(&goptr->go_pgroups);
    }

    return goptr;
}

/*
 * Description (groupDeleteMember)
 *
 *	This function removes a specified member from a group object's
 *	list of members.  The member to be remove may be a group or a
 *	user, expressed as a group id or user id, respectively.  The
 *	'isgid' argument is non-zero if the member being removed is a
 *	group, or zero if it is a user.
 *
 * Arguments:
 *
 *	goptr			- pointer to group object
 *	isgid			- non-zero if 'id' is a group id
 *				  zero if 'id' is a user id
 *	id			- group or user id to be removed
 *
 * Returns:
 *
 *	The return value is zero if the specified member was not present
 *	in the group object, or one if the member was successfully removed.
 */

NSAPI_PUBLIC int groupDeleteMember(GroupObj_t * goptr, int isgid, USI_t id)
{
    USIList_t * uilptr;		/* pointer to list of member users or groups */
    int rv;			/* return value */

    /* Get pointer to appropriate list of ids */
    uilptr = (isgid) ? &goptr->go_groups : &goptr->go_users;

    /* Remove the specified id */
    rv = usiRemove(uilptr, id);
    if (rv > 0) {
	goptr->go_flags |= GOF_MODIFIED;
    }

    return rv;
}

/*
 * Description (groupEncode)
 *
 *	This function encodes a group object into a group DB record.
 *
 * Arguments:
 *
 *	goptr			- pointer to group object
 *	greclen			- pointer to returned record length
 *	grecptr			- pointer to returned record pointer
 *
 * Returns:
 *
 *	The function return value is zero if successful.  The length
 *	and location of the created attribute record are returned
 *	through 'greclen' and 'grecptr'.  A non-zero function value
 *	is returned if there's an error.
 */

NSAPI_PUBLIC int groupEncode(GroupObj_t * goptr, int * greclen, ATR_t * grecptr)
{
    int reclen;			/* length of DB record */
    ATR_t rptr;			/* DB record pointer */
    ATR_t rstart = 0;		/* pointer to beginning of DB record */
    ATR_t glptr;		/* saved pointer to UAT_GROUPS length */
    ATR_t gptr;			/* saved pointer to after length at glptr */
    int gidlen;			/* gid encoding length */
    int fllen;			/* flags encoding length */
    USI_t dsclen;		/* group description encoding length */
    USI_t nulen;		/* member user count encoding length */
    USI_t nglen;		/* member group count encoding length */
    int idcnt;			/* count of user or group ids */
    USI_t * ids;		/* pointer to array of user or group ids */
    int i;			/* id index */
    int rv = -1;

    /*
     * First we need to figure out how long the generated record will be.
     * This doesn't have to be exact, but it must not be smaller than the
     * actual record size.
     */

    /* GAT_GID attribute: tag, length, USI */
    gidlen = USILENGTH(goptr->go_gid);
    reclen = (1 + 1 + gidlen);

    /* GAT_FLAGS attribute: tag, length, USI */
    fllen = USILENGTH(goptr->go_flags & GOF_DBFLAGS);
    reclen += (1 + 1 + fllen);

    /* GAT_DESCRIPT attribute: tag, length, NTS */
    dsclen = NTSLENGTH(goptr->go_desc);
    reclen += (1 + USILENGTH(dsclen) + dsclen);

    /* GAT_USERS attribute: tag, length, USI(count), USI(uid)... */
    idcnt = UILCOUNT(&goptr->go_users);
    nulen = USILENGTH(idcnt);
    reclen += (1 + USIALLOC() + nulen + (5 * idcnt));

    /* GAT_GROUPS attribute: tag, length, USI(count), USI(gid)... */
    idcnt = UILCOUNT(&goptr->go_groups);
    nglen = USILENGTH(idcnt);
    reclen += (1 + USIALLOC() + nglen + (5 * idcnt));

    /* GAT_PGROUPS attribute: tag, length, USI(count), USI(gid)... */
    idcnt = UILCOUNT(&goptr->go_pgroups);
    nglen = USILENGTH(idcnt);
    reclen += (1 + USIALLOC() + nglen + (5 * idcnt));

    /* Allocate the attribute record buffer */
    rptr = (ATR_t)MALLOC(reclen);
    if (rptr) {

	/* Save pointer to start of record */
	rstart = rptr;

	/* Encode GAT_GID attribute */
	*rptr++ = GAT_GID;
	*rptr++ = gidlen;
	rptr = USIENCODE(rptr, goptr->go_gid);

	/* Encode GAT_FLAGS attribute */
	*rptr++ = GAT_FLAGS;
	*rptr++ = fllen;
	rptr = USIENCODE(rptr, (goptr->go_flags & GOF_DBFLAGS));

	/* Encode GAT_DESCRIPT attribute */
	*rptr++ = GAT_DESCRIPT;
	rptr = USIENCODE(rptr, dsclen);
	rptr = NTSENCODE(rptr, goptr->go_desc);

	/* Encode GAT_USERS attribute */
	*rptr++ = GAT_USERS;

	/*
	 * Save a pointer to the attribute encoding length, and reserve
	 * space for the maximum encoding size of a USI_t value.
	 */
	glptr = rptr;
	rptr += USIALLOC();
	gptr = rptr;

	/* Encode number of user members */
	idcnt = UILCOUNT(&goptr->go_users);
	rptr = USIENCODE(rptr, idcnt);

	/* Generate user ids encodings */
	ids = UILLIST(&goptr->go_users);
	for (i = 0; i < idcnt; ++i) {
	    rptr = USIENCODE(rptr, ids[i]);
	}

	/* Now fix up the GAT_USERS attribute encoding length */
	glptr = USIINSERT(glptr, (USI_t)(rptr - gptr));

	/* Encode GAT_GROUPS attribute */
	*rptr++ = GAT_GROUPS;

	/*
	 * Save a pointer to the attribute encoding length, and reserve
	 * space for the maximum encoding size of a USI_t value.
	 */
	glptr = rptr;
	rptr += USIALLOC();
	gptr = rptr;

	/* Encode number of groups */
	idcnt = UILCOUNT(&goptr->go_groups);
	rptr = USIENCODE(rptr, idcnt);

	/* Generate group ids encodings */
	ids = UILLIST(&goptr->go_groups);
	for (i = 0; i < idcnt; ++i) {
	    rptr = USIENCODE(rptr, ids[i]);
	}

	/* Now fix up the GAT_GROUPS attribute encoding length */
	glptr = USIINSERT(glptr, (USI_t)(rptr - gptr));

	/* Encode GAT_PGROUPS attribute */
	*rptr++ = GAT_PGROUPS;

	/*
	 * Save a pointer to the attribute encoding length, and reserve
	 * space for the maximum encoding size of a USI_t value.
	 */
	glptr = rptr;
	rptr += USIALLOC();
	gptr = rptr;

	/* Encode number of groups */
	idcnt = UILCOUNT(&goptr->go_pgroups);
	rptr = USIENCODE(rptr, idcnt);

	/* Generate group ids encodings */
	ids = UILLIST(&goptr->go_pgroups);
	for (i = 0; i < idcnt; ++i) {
	    rptr = USIENCODE(rptr, ids[i]);
	}

	/* Now fix up the GAT_PGROUPS attribute encoding length */
	glptr = USIINSERT(glptr, (USI_t)(rptr - gptr));

	/* Return record length and location if requested */
	if (greclen) *greclen = rptr - rstart;
	if (grecptr) *grecptr = rstart;

	/* Indicate success */
	rv = 0;
    }

    return rv;
}

/*
 * Description (groupRemove)
 *
 *	This function is called to remove a group from a specified group
 *	database.  Both the primary DB file and the id-to-name DB file
 *	are updated.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	groupdb			- handle for group DB access
 *	flags			- (unused - must be zero)
 *	name			- pointer to group name
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.
 */

NSAPI_PUBLIC int groupRemove(NSErr_t * errp, void * groupdb, int flags, NTS_t name)
{
    GroupObj_t * goptr;		/* group object pointer */
    int rv;
    int rv2;

    /* First retrieve the group record */
    goptr = groupFindByName(errp, groupdb, name);
    if (!goptr) {
	/* Error - specified group not found */
	return NSAERRNAME;
    }

    /* Free the group id value, if any */
    rv = 0;
    if (goptr->go_gid != 0) {
	rv = ndbFreeId(errp, groupdb, 0, (char *)name, goptr->go_gid);
    }

    rv2 = ndbDeleteName(errp, groupdb, 0, 0, (char *)name);

    return (rv) ? rv : rv2;
}

/*
 * Description (groupStore)
 *
 *	This function is called to store a group object in the database.
 *	If the object was created by groupCreate(), it is assumed to be
 *	a new group, the group account name must not match any existing
 *	group account names in the database, and a gid is assigned before
 *	adding the group to the database.  If the object was created by
 *	groupFindByName(), the information in the group object will
 *	replace the existing database entry for the indicated group
 *	name.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	groupdb			- handle for group DB access
 *	flags			- (unused - must be zero)
 *	goptr			- group object pointer
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.  The group object remains intact in either
 *	case.
 */

NSAPI_PUBLIC int groupStore(NSErr_t * errp, void * groupdb, int flags, GroupObj_t * goptr)
{
    ATR_t recptr = 0;
    USI_t gid;
    int reclen = 0;
    int stflags = 0;
    int eid;
    int rv;

    /* If this is a new group, allocate a uid value */
    if (goptr->go_flags & GOF_NEW) {

	rv = ndbAllocId(errp, groupdb, 0, (char *)goptr->go_name, &gid);
	if (rv) goto punt;

	goptr->go_gid = gid;

	/* Let the database manager know that this is a new entry */
	stflags = NDBF_NEWNAME;
    }

    /* Convert the information in the group object to a DB record */
    rv = groupEncode(goptr, &reclen, &recptr);
    if (rv) goto err_nomem;

    /*
     * Store the record in the database under the group name.
     * If this is a new entry, a group id to group name mapping
     * also will be added to the id-to-name DB file.
     */
    rv = ndbStoreName(errp, groupdb, stflags,
		      0, (char *)goptr->go_name, reclen, (char *)recptr);

    FREE(recptr);

    if (rv == 0) {
	goptr->go_flags &= ~(GOF_NEW | GOF_MODIFIED);
    }

  punt:
    return rv;

  err_nomem:
    eid = NSAUERR2000;
    rv = NSAERRNOMEM;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    goto punt;
}
