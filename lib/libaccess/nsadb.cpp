/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsadb.c)
 *
 *	This module contains routines for retrieving information from
 *	a Netscape authentication database.  An authentication database
 *	consists of a user database and a group database.  This module
 *	implements an authentication database based on Netscape user and
 *	group databases defined in nsuser.h and nsgroup.h, which in turn
 *	are based on the Netscape (server) database implementation
 *	defined in nsdb.h.  The interface for managing information in
 *	an authentication database is described separately in nsamgmt.h.
 */

#include <base/systems.h>
#include <netsite.h>
#include <base/file.h>
#include <base/fsmutex.h>
#include <libaccess/nsdbmgmt.h>
#define __PRIVATE_NSADB
#include <libaccess/nsadb.h>
#include <libaccess/nsuser.h>
#include <libaccess/nsgroup.h>

/*
 * Description (NSADB_AuthIF)
 *
 *	This structure defines a generic authentication database
 *	interface for this module.  It does not currently support
 *	user/group id lookup.
 */
AuthIF_t NSADB_AuthIF = {
    0,					/* find user/group by id */
    nsadbFindByName,			/* find user/group by name */
    nsadbIdToName,			/* lookup name for user/group id */
    nsadbOpen,				/* open a named database */
    nsadbClose,				/* close a database */
};

/*
 * Description (nsadbClose)
 *
 *	This function closes an authentication database previously opened
 *	via nsadbOpen().
 *
 * Arguments:
 *
 *	authdb				- handle returned by nsadbOpen()
 *	flags				- unused (must be zero)
 */

NSAPI_PUBLIC void nsadbClose(void * authdb, int flags)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;

    if (adb->adb_userdb != 0) {
	ndbClose(adb->adb_userdb, 0);
    }

    if (adb->adb_groupdb != 0) {
	ndbClose(adb->adb_groupdb, 0);
    }

#if defined(CLIENT_AUTH)
    nsadbCloseCerts(authdb, flags);
#endif

    if (adb->adb_dbname) {
	FREE(adb->adb_dbname);
    }

    FREE(adb);
}

/*
 * Description (nsadbOpen)
 *
 *	This function is used to open an authentication database.
 *	The caller specifies a name for the database, which is actually
 *	the name of a directory containing the files which comprise the
 *	database.  The caller also indicates whether this is a new
 *	database, in which case it is created.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	adbname			- name of this database (directory)
 *	flags			- open flags:
 *				    AIF_CREATE - new database (create)
 *	rptr			- pointer to returned handle
 *
 * Returns:
 *
 *	A handle for accessing the database is always returned via 'rptr'
 *	unless there was a shortage of dynamic memory, in which case a
 *	null handle is returned.  The return value of the function is
 *	0 if it completes successfully.  An error is indicated by a
 *	negative return value (see nsautherr.h).
 */

NSAPI_PUBLIC int nsadbOpen(NSErr_t * errp,
			   char * adbname, int flags, void **rptr)
{
    AuthDB_t * authdb = 0;		/* pointer to database descriptor */
    SYS_DIR dbdir;			/* database directory handle */
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Make sure we have a place to return the database handle */
    if (rptr == 0) goto err_inval;

    /* Allocate the database descriptor */
    authdb = (AuthDB_t *)MALLOC(sizeof(AuthDB_t));
    if (authdb == 0) goto err_nomem;

    /* Return the descriptor pointer as the database handle */
    *rptr = (void *)authdb;

    authdb->adb_dbname = STRDUP(adbname);
    authdb->adb_userdb = 0;
    authdb->adb_groupdb = 0;
#if defined(CLIENT_AUTH)
    authdb->adb_certdb = 0;
    authdb->adb_certlock = 0;
    authdb->adb_certnm = 0;
#endif
    authdb->adb_flags = 0;

    /* See if the database directory exists */
    dbdir = dir_open(adbname);
    if (dbdir == 0) {
	/* No, create it if this is a new database, else error */
	if (flags & AIF_CREATE) {
	    rv = dir_create(adbname);
	    if (rv < 0) goto err_mkdir;
	    authdb->adb_flags |= ADBF_NEW;
	}
	else goto err_dopen;
    }
    else {
	/* Ok, it's there */
	dir_close(dbdir);
    }

    return 0;

  err_inval:
    eid = NSAUERR3000;
    rv = NSAERRINVAL;
    goto err_ret;

  err_nomem:
    /* Error - insufficient dynamic memory */
    eid = NSAUERR3020;
    rv = NSAERRNOMEM;
    goto err_ret;

  err_ret:
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    goto punt;

  err_mkdir:
    eid = NSAUERR3040;
    rv = NSAERRMKDIR;
    goto err_dir;

  err_dopen:
    eid = NSAUERR3060;
    rv = NSAERROPEN;
    goto err_dir;

  err_dir:
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, adbname);
    goto punt;

  punt:
    /* Fatal error - free database descriptor and return null handle */
    if (authdb) {
	if (authdb->adb_dbname) {
	    FREE(authdb->adb_dbname);
	}
	FREE(authdb);
    }

    if (rptr) *rptr = 0;

    return rv;
}

/*
 * Description (nsadbOpenUsers)
 *
 *	This function is called to open the users subdatabase of an
 *	open authentication database.  The caller specifies flags to
 *	indicate whether read or write access is required.  This
 *	function is normally called only by routines below the
 *	nsadbOpen() API, in response to perform particular operations
 *	on user or group objects.  If the open is successful, the
 *	resulting handle is stored in the AuthDB_t structure.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	flags			- open flags:
 *					ADBF_UREAD - open for read
 *					ADBF_UWRITE - open for read/write
 * Returns:
 *
 *	The return value is zero if the operation is successfully
 *	completed.  An error is indicated by a negative return value
 *	(see nsautherr.h), and an error frame is generated if an error
 *	frame list was provided.
 */

NSAPI_PUBLIC int nsadbOpenUsers(NSErr_t * errp, void * authdb, int flags)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    char * userfn = 0;			/* user database name */
    int dblen;				/* strlen(adb_dbname) */
    int uversion;			/* user database version number */
    int eid;				/* error id code */
    int rv;				/* result value */

    if (adb == 0) goto err_inval;

    /* Is the user database already open? */
    if (adb->adb_userdb != 0) {

	/* Yes, is it open for the desired access? */
	if (adb->adb_flags & flags) {

	    /* Yes, that was easy */
	    return 0;
	}
    }
    else {

	/* We need to open the database */

	/* Allocate space for the user database filename */
	dblen = strlen(adb->adb_dbname);

	userfn = (char *)MALLOC(dblen + strlen(ADBUSERDBNAME) + 2);
	if (userfn == 0) goto err_nomem;

	/* Construct user database name */
	strcpy(userfn, adb->adb_dbname);

	/* Put in a '/' (or '\') if it's not there */
	if (userfn[dblen-1] != FILE_PATHSEP) {
	    userfn[dblen] = FILE_PATHSEP;
	    userfn[dblen+1] = 0;
	    ++dblen;
	}

	strcpy(&userfn[dblen], ADBUSERDBNAME);

	adb->adb_userdb = ndbOpen(errp,
				  userfn, 0, NDB_TYPE_USERDB, &uversion);
	if (adb->adb_userdb == 0) goto err_uopen;

	FREE(userfn);
    }

    /*
     * We don't really reopen the database to get the desired
     * access mode, since that is handled at the nsdb level.
     * But we do update the flags, just for the record.
     */
    adb->adb_flags &= ~(ADBF_UREAD|ADBF_UWRITE);
    if (flags & ADBF_UWRITE) adb->adb_flags |= ADBF_UWRITE;
    else adb->adb_flags |= ADBF_UREAD;

    return 0;

  err_inval:
    eid = NSAUERR3200;
    rv = NSAERRINVAL;
    goto err_ret;

  err_nomem:
    eid = NSAUERR3220;
    rv = NSAERRNOMEM;
    goto err_ret;

  err_ret:
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    goto punt;

  err_uopen:
    eid = NSAUERR3240;
    rv = NSAERROPEN;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, userfn);
    goto punt;

  punt:
    return rv;
}

/*
 * Description (nsadbOpenGroups)
 *
 *	This function is called to open the groups subdatabase of an
 *	open authentication database.  The caller specifies flags to
 *	indicate whether read or write access is required.  This
 *	function is normally called only by routines below the
 *	nsadbOpen() API, in response to perform particular operations
 *	on user or group objects.  If the open is successful, the
 *	resulting handle is stored in the AuthDB_t structure.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	flags			- open flags:
 *					ADBF_GREAD - open for read
 *					ADBF_GWRITE - open for read/write
 * Returns:
 *
 *	The return value is zero if the operation is successfully
 *	completed.  An error is indicated by a negative return value
 *	(see nsautherr.h), and an error frame is generated if an error
 *	frame list was provided.
 */

NSAPI_PUBLIC int nsadbOpenGroups(NSErr_t * errp, void * authdb, int flags)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    char * groupfn = 0;			/* group database name */
    int dblen;				/* strlen(adb_dbname) */
    int gversion;			/* group database version number */
    int eid;				/* error id code */
    int rv;				/* result value */

    if (adb == 0) goto err_inval;

    /* Is the group database already open? */
    if (adb->adb_groupdb != 0) {

	/* Yes, is it open for the desired access? */
	if (adb->adb_flags & flags) {

	    /* Yes, that was easy */
	    return 0;
	}
    }
    else {

	/* We need to open the database */

	/* Allocate space for the group database filename */
	dblen = strlen(adb->adb_dbname);

	groupfn = (char *)MALLOC(dblen + strlen(ADBGROUPDBNAME) + 2);
	if (groupfn == 0) goto err_nomem;

	/* Construct group database name */
	strcpy(groupfn, adb->adb_dbname);

	/* Put in a '/' (or '\') if it's not there */
	if (groupfn[dblen-1] != FILE_PATHSEP) {
	    groupfn[dblen] = FILE_PATHSEP;
	    groupfn[dblen+1] = 0;
	    ++dblen;
	}

	strcpy(&groupfn[dblen], ADBGROUPDBNAME);

	adb->adb_groupdb = ndbOpen(errp,
				   groupfn, 0, NDB_TYPE_GROUPDB, &gversion);
	if (adb->adb_groupdb == 0) goto err_gopen;

	FREE(groupfn);
    }

    /*
     * We don't really reopen the database to get the desired
     * access mode, since that is handled at the nsdb level.
     * But we do update the flags, just for the record.
     */
    adb->adb_flags &= ~(ADBF_GREAD|ADBF_GWRITE);
    if (flags & ADBF_GWRITE) adb->adb_flags |= ADBF_GWRITE;
    else adb->adb_flags |= ADBF_GREAD;

    return 0;

  err_inval:
    eid = NSAUERR3300;
    rv = NSAERRINVAL;
    goto err_ret;

  err_nomem:
    eid = NSAUERR3320;
    rv = NSAERRNOMEM;
    goto err_ret;

  err_ret:
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    goto punt;

  err_gopen:
    eid = NSAUERR3340;
    rv = NSAERROPEN;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, groupfn);
    goto punt;

  punt:
    return rv;
}

/*
 * Description (nsadbIdToName)
 *
 *	This function looks up a specified user or group id in the
 *	authentication database.  The name associated with the specified
 *	id is returned.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	id			- user or group id
 *	flags			- AIF_USER or AIF_GROUP (defined in nsauth.h)
 *	rptr			- pointer to returned group or user name
 *
 * Returns:
 *
 *	The return value is zero if no error occurs,
 *	A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbIdToName(NSErr_t * errp,
			       void * authdb, USI_t id, int flags, char **rptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    void * whichdb = 0;
    char * name;
    int rv;

    if (rptr != 0) *rptr = 0;

    /* Decide whether to use user or group database */
    if (flags & AIF_USER) {

	whichdb = adb->adb_userdb;
	if (whichdb == 0) {
	    rv = nsadbOpenUsers(errp, authdb, ADBF_UREAD);
	    if (rv < 0) goto punt;
	    whichdb = adb->adb_userdb;
	}
    }
    else if (flags & AIF_GROUP) {

	whichdb = adb->adb_groupdb;
	if (whichdb == 0) {
	    rv = nsadbOpenGroups(errp, authdb, ADBF_GREAD);
	    if (rv < 0) goto punt;
	    whichdb = adb->adb_groupdb;
	}
    }

    if (whichdb != 0) {

	/* Get the name corresponding to the id */
	rv = ndbIdToName(errp, whichdb, id, 0, &name);
	if (rv < 0) goto punt;

	if ((rptr != 0)) *rptr = name;
	rv = 0;
    }

  punt:
    return rv;
}

/*
 * Description (nsadbFindByName)
 *
 *	This function looks up a specified name in the authentication
 *	database.  Flags specified by the caller indicate whether a
 *	group name, user name, or either should be found.  The caller
 *	may optionally provide for the return of a user or group object
 *	pointer, in which case the information associated with a
 *	matching group or user is used to create a group or user object.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	name			- name of group or user
 *	flags			- search flags (defined in nsauth.h)
 *	rptr			- pointer to returned group or user
 *				  object pointer (may be null)
 *
 * Returns:
 *
 *	The return value is a non-negative value if no error occurs,
 *	and the value indicates whether the name matched a group or
 *	user:
 *
 *	AIF_NONE		- name did not match a group or user name
 *	AIF_GROUP		- name matched a group name
 *	AIF_USER		- name matched a user name
 *
 *	If the value is AIF_GROUP or AIF_USER, and rptr is non-null,
 *	then a group or user object is created, and a pointer to it is
 *	returned in the location indicated by rptr.
 *
 *	A negative return value indicates an error.
 */

NSAPI_PUBLIC int nsadbFindByName(NSErr_t * errp, void * authdb,
				 char * name, int flags, void **rptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    ATR_t recptr;
    int reclen;
    int rv;

    if (rptr != 0) *rptr = 0;

    /* Search for group name? */
    if (flags & AIF_GROUP) {

	if (adb->adb_groupdb == 0) {
	    rv = nsadbOpenGroups(errp, authdb, ADBF_GREAD);
	    if (rv < 0) goto punt;
	}

	/* Look up the name in the group database */
	rv = ndbFindName(errp, adb->adb_groupdb, 0, (char *)name,
			 &reclen, (char **)&recptr);
	if (rv == 0) {

	    /* Found it.  Make a group object if requested. */
	    if (rptr != 0) {

		/* Got the group record.  Decode into a group object. */
		*rptr = (void *)groupDecode((NTS_t)name, reclen, recptr);
	    }

	    return AIF_GROUP;
	}
    }

    /* Search for user name? */
    if (flags & AIF_USER) {

	if (adb->adb_userdb == 0) {
	    rv = nsadbOpenUsers(errp, authdb, ADBF_UREAD);
	    if (rv < 0) goto punt;
	}

	/* Look up the name in the user database */
	rv = ndbFindName(errp, adb->adb_userdb, 0, (char *)name,
			 &reclen, (char **)&recptr);
	if (rv == 0) {

	    /* Found it.  Make a user object if requested. */
	    if (rptr != 0) {

		/* Got the user record.  Decode into a user object. */
		*rptr = (void *)userDecode((NTS_t)name, reclen, recptr);
	    }

	    return AIF_USER;
	}
    }

    /* Nothing found */
    nserrDispose(errp);
    return AIF_NONE;

  punt:
    return rv;
}
