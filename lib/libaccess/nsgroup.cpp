/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsgroup.c)
 *
 *	This module contains routines for accessing information in a
 *	Netscape group database.  Group information is returned in the
 *	form of a group object (GroupObj_t), defined in nsauth.h.
 */

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#define __PRIVATE_NSGROUP
#include "libaccess/nsgroup.h"

/*
 * Description (groupDecode)
 *
 *	This function decodes an external group DB record into a
 *	dynamically allocated GroupObj_t structure.  The DB record is
 *	encoded as an attribute record as defined in attrec.h.
 *
 * Arguments:
 *
 *	name		- pointer to group name string
 *	greclen		- length of the group DB record, in octets
 *	grecptr		- pointer to group DB record
 *
 * Returns:
 *
 *	A pointer to the allocated GroupObj_t structure is returned.
 */

NSAPI_PUBLIC GroupObj_t * groupDecode(NTS_t name, int greclen, ATR_t grecptr)
{
    ATR_t cp = grecptr;			/* current pointer into DB record */
    USI_t tag;				/* attribute tag */
    USI_t len;				/* attribute value encoding length */
    int i;				/* group id index */
    int idcnt;				/* count of user or group ids */
    USI_t * ids;			/* pointer to array of ids */
    GroupObj_t * goptr;			/* group object pointer */

    /* Allocate a group object structure */
    goptr = (GroupObj_t *)MALLOC(sizeof(GroupObj_t));
    if (goptr) {

	goptr->go_name = (unsigned char *) STRDUP((char *)name);
	goptr->go_gid = 0;
	goptr->go_flags = GOF_MODIFIED;
	goptr->go_desc = 0;
	UILINIT(&goptr->go_users);
	UILINIT(&goptr->go_groups);
	UILINIT(&goptr->go_pgroups);

	/* Parse group DB record */
	while ((cp - grecptr) < greclen) {

	    /* Get the attribute tag */
	    cp = USIDECODE(cp, &tag);

	    /* Get the length of the encoding of the attribute value */
	    cp = USIDECODE(cp, &len);

	    /* Process this attribute */
	    switch (tag) {

	      case GAT_GID:		/* group id */
		cp = USIDECODE(cp, &goptr->go_gid);
		break;

	      case GAT_FLAGS:		/* flags */
		cp = USIDECODE(cp, &goptr->go_flags);
		break;

	      case GAT_DESCRIPT:	/* group description */
		cp = NTSDECODE(cp, &goptr->go_desc);
		break;

	      case GAT_USERS:		/* member users of this group */

		/* First get the number of user ids following */
		cp = USIDECODE(cp, (unsigned *)&idcnt);

		if (idcnt > 0) {

		    /* Allocate space for user ids */
		    ids = usiAlloc(&goptr->go_users, idcnt);
		    if (ids) {
			for (i = 0; i < idcnt; ++i) {
			    cp = USIDECODE(cp, ids + i);
			}
		    }
		}
		break;

	      case GAT_GROUPS:		/* member groups of this group */

		/* First get the number of group ids following */
		cp = USIDECODE(cp, (unsigned *)&idcnt);

		if (idcnt > 0) {

		    /* Allocate space for group ids */
		    ids = usiAlloc(&goptr->go_groups, idcnt);
		    if (ids) {
			for (i = 0; i < idcnt; ++i) {
			    cp = USIDECODE(cp, ids + i);
			}
		    }
		}
		break;

	      case GAT_PGROUPS:		/* parent groups of this group */

		/* First get the number of group ids following */
		cp = USIDECODE(cp, (USI_t *)&idcnt);

		if (idcnt > 0) {

		    /* Allocate space for group ids */
		    ids = usiAlloc(&goptr->go_pgroups, idcnt);
		    if (ids) {
			for (i = 0; i < idcnt; ++i) {
			    cp = USIDECODE(cp, ids + i);
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

    return goptr;
}

/*
 * Description (groupEnumHelp)
 *
 *	This is a local function that is called by NSDB during group
 *	database enumeration.  It decodes group records into group
 *	objects, and presents them to the caller of groupEnumerate().
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	parg			- pointer to GroupEnumArgs_t structure
 *	namelen			- length of group record key, including null
 *				  terminator
 *	name			- group record key (group account name)
 *	reclen			- length of group record
 *	recptr			- pointer to group record contents
 *
 * Returns:
 *
 *	Returns whatever value is returned from the upcall to the caller
 *	of groupEnumerate().
 */

static int groupEnumHelp(NSErr_t * errp, void * parg,
			 int namelen, char * name, int reclen, char * recptr)
{
    GroupEnumArgs_t * ge = (GroupEnumArgs_t *)parg;
    GroupObj_t * goptr;			/* group object pointer */
    int rv;

    goptr = groupDecode((NTS_t)name, reclen, (ATR_t)recptr);

    rv = (*ge->func)(errp, ge->user, goptr);

    if (!(ge->flags & GOF_ENUMKEEP)) {
	FREE(goptr);
    }

    return rv;
}

/*
 * Description (groupEnumerate)
 *
 *	This function enumerates all of the groups in a specified group
 *	database, calling a caller-specified function with a group object
 *	for each group in the database.  A 'flags' value of GOF_ENUMKEEP
 *	can be specified to keep the group objects around (not free them)
 *	after the caller's function returns.  Otherwise, each group
 *	object is freed after being presented to the caller's function.
 *	The 'argp' argument is an opaque pointer, which is passed to
 *	the caller's function as 'parg' on each call, along with a
 *	group object pointer.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	groupdb			- handle for group DB access
 *	flags			- bit flags:
 *					GOF_ENUMKEEP - keep group objects
 *	argp			- passed to 'func' as 'parg'
 *	func			- pointer to caller's enumeration function
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise it is a
 *	non-zero error code.
 */

NSAPI_PUBLIC int groupEnumerate(NSErr_t * errp, void * groupdb, int flags, void * argp,
		  int (*func)(NSErr_t * ferrp,
			      void * parg, GroupObj_t * goptr))
{
    int rv;
    GroupEnumArgs_t args;

    args.groupdb = groupdb;
    args.flags = flags;
    args.func = func;
    args.user = argp;

    rv = ndbEnumerate(errp,
		      groupdb, NDBF_ENUMNORM, (void *)&args, groupEnumHelp);

    return rv;
}

/*
 * Description (groupFindByName)
 *
 *	This function looks up a group record for a specified group name,
 *	converts the group record to the internal group object form, and
 *	returns a pointer to the group object.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	groupdb			- handle for group DB access
 *	name			- group name to find
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a group object
 *	for the specified group.  Otherwise it is 0.
 */

NSAPI_PUBLIC GroupObj_t * groupFindByName(NSErr_t * errp, void * groupdb, NTS_t name)
{
    GroupObj_t * goptr = 0;
    ATR_t grecptr;
    int greclen;
    int rv;

    /* Look up the group name in the database */
    rv = ndbFindName(errp, groupdb, 0, (char *)name, &greclen, (char **)&grecptr);
    if (rv == 0) {

	/* Got the group record.  Decode into a group object. */
	goptr = groupDecode(name, greclen, grecptr);
    }

    return goptr;
}

/*
 * Description (groupFindByGid)
 *
 *	This function looks up a group record for a specified group id,
 *	converts the group record to the internal group object form, and
 *	returns a pointer to the group object.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	groupdb			- handle for group DB access
 *	gid			- group id to find
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a group object
 *	for the specified group.  Otherwise it is 0.
 */

NSAPI_PUBLIC GroupObj_t * groupFindByGid(NSErr_t * errp, void * groupdb, USI_t gid)
{
    GroupObj_t * goptr = 0;
    NTS_t name;
    ATR_t grecptr;
    int greclen;
    int rv;

    /* Get the group account name corresponding to the gid */
    rv = ndbIdToName(errp, groupdb, gid, 0, (char **)&name);
    if (rv == 0) {

	rv = ndbFindName(errp, groupdb, 0, (char *)name, &greclen, (char **)&grecptr);
	if (rv == 0) {

	    /* Got the group record.  Decode into a group object. */
	    goptr = groupDecode(name, greclen, grecptr);
	}
    }

    return goptr;
}

/*
 * Description (groupFree)
 *
 *	This function is called to free a group object.  Group objects
 *	are not automatically freed when a group database is closed.
 *
 * Arguments:
 *
 *	goptr			- group object pointer
 *
 */

NSAPI_PUBLIC void groupFree(GroupObj_t * goptr)
{
    if (goptr) {

	if (goptr->go_name) FREE(goptr->go_name);
	if (goptr->go_desc) FREE(goptr->go_desc);
	UILFREE(&goptr->go_users);
	UILFREE(&goptr->go_groups);
	UILFREE(&goptr->go_pgroups);
	FREE(goptr);
    }
}
