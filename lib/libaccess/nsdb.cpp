/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsdb.c)
 *
 *	This provides access to a Netscape server database.
 *	A server database is composed of two (libdbm) DB files.  One
 *	of these (<dbname>.db) contains records indexed by a string
 *	key.  These records contain the primary information in the
 *	database.  A second DB file (<dbname>.id) is used to map an
 *	integer id value to a string key, which can then be used to
 *	locate a record in the first file.
 *
 *	Normally the records in the primary DB file will contain the
 *	id values which are used to key the id-to-name DB.  When this
 *	is the case, it is possible to construct the id-to-name DB from
 *	the primary DB file, and an interface is provided to facilitate
 *	this.
 */

#include <stdio.h>
#include <base/systems.h>
#include <netsite.h>
#include <base/file.h>
#define __PRIVATE_NSDB
#include <libaccess/nsdb.h>

#include <errno.h>

#define NDBMODE	0644			/* mode for creating files */

char * NSDB_Program = "NSDB";		/* NSDB facility name */

NSPR_BEGIN_EXTERN_C

/*
 * Description (ndbClose)
 *
 *	This function closes the specified database.  This involves
 *	closing the primary and id-to-name DB files, and freeing the
 *	NSDB_t object.
 *
 * Arguments:
 *
 *	ndb			- database handle from ndbOpen()
 *	flags			- (currently unused - should be zero)
 *
 */

void ndbClose(void * ndb, int flags)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */

    if (ndbp->ndb_flags & (NDBF_WRNAME|NDBF_RDNAME)) {
	(*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);
    }

    if (ndbp->ndb_flags & (NDBF_WRID|NDBF_RDID)) {
	(*ndbp->ndb_idb->close)(ndbp->ndb_idb);
    }

    if (ndbp->ndb_pname) {
	FREE(ndbp->ndb_pname);
    }

    if (ndbp->ndb_iname) {
	FREE(ndbp->ndb_iname);
    }

    FREE(ndbp);
}

/*
 * Description (ndbEnumerate)
 *
 *	This function is called to enumerate the records of the primary
 *	DB file to a caller-specified function.  The function specified
 *	by the caller is called with the name (key), length and address
 *	of each record in the primary DB file.  The 'flags' argument can
 *	be used to select normal data records, metadata records, or both.
 *	If the 'flags' value is zero, only normal data records are
 *	enumerated.  The function specified by the caller returns -1 to
 *	terminate the enumeration, 0 to continue it, or +1 to restart
 *	the enumeration from the beginning.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	flags			- bit flags:
 *					NDBF_ENUMNORM - normal data records
 *					NDBF_ENUMMETA - metadata records
 *	func			- pointer to caller's enumeration function
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned, and an error frame is generated if an
 *	error frame list was provided by the caller.
 */

int ndbEnumerate(NSErr_t * errp, void * ndb, int flags, void * argp,
#ifdef UnixWare
	ArgFn_ndbEnum func) /* for ANSI C++ standard, see nsdb.h */
#else
	int (*func)(NSErr_t * ferrp, void * parg,
		    int namelen, char * name,
		    int reclen, char * recptr))
#endif
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    int rv;
    int dbflag;

    /* Is the user DB open for reading names? */
    if (!(ndbp->ndb_flags & NDBF_RDNAME)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_RDNAME);
	if (rv) goto punt;
    }

    if (flags == 0) flags = NDBF_ENUMNORM;

    for (dbflag = R_FIRST; ; dbflag = (rv > 0) ? R_FIRST : R_NEXT) {

	/* Retrieve the next (first) record from the primary DB */
	rv = (*ndbp->ndb_pdb->seq)(ndbp->ndb_pdb, &key, &rec, dbflag);
	if (rv) break;

	/* Is this a metadata record? */
	if (*(char *)key.data == NDB_MDPREFIX) {

	    /* Yes, skip it if metadata was not requested */
	    if (!(flags & NDBF_ENUMMETA)) continue;
	}
	else {
	    /* Skip normal data if not requested */
	    if (!(flags & NDBF_ENUMNORM)) continue;
	}

	/* Pass this record to the caller's function */
	rv = (*func)(errp, argp,
		     key.size, (char *)key.data, rec.size, (char *)rec.data);
	if (rv < 0) break;
    }

    /* Indicate success */
    rv = 0;

  punt:
    return rv;
}

/*
 * Description (ndbFindName)
 *
 *	This function retrieves from the database a record with the
 *	specified key.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	namelen			- length of the key, including null
 *				  terminator if any
 *	name			- pointer to the key of the desired record
 *	reclen			- pointer to returned record length
 *	recptr			- pointer to returned record pointer
 *
 * Returns:
 *
 *	If successful, the return value is zero, and the length and
 *	address of the returned record are returned through reclen and
 *	recptr.  Otherwise the return value is non-zero, and an error
 *	frame is generated if an error frame list was provided by the
 *	caller.
 *
 * Notes:
 *
 *	The record buffer is dynamically allocated and is freed 
 *	automatically when the database is closed.
 */

int ndbFindName(NSErr_t * errp, void * ndb, int namelen, char * name,
		int * reclen, char **recptr)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Is the user DB open for reading names? */
    if (!(ndbp->ndb_flags & NDBF_RDNAME)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_RDNAME);
	if (rv) goto punt;
    }

    /* Set up record key.  Include the terminating null byte. */
    key.data = (void *)name;
    key.size = (namelen > 0) ? namelen : (strlen(name) + 1);

    /* Initialize record buffer descriptor */
    rec.data = 0;
    rec.size = 0;

    /* Retrieve the record by its key */
    rv = (*ndbp->ndb_pdb->get)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_pget;

    /* Return record length and address */
    if (reclen) *reclen = rec.size;
    if (recptr) *recptr = (char *)rec.data;

    /* Indicate success */
    rv = 0;

  punt:
    return rv;

  err_pget:
    eid = NSDBERR1000;
    rv = NDBERRGET;
    nserrGenerate(errp, rv, eid, NSDB_Program, 2, ndbp->ndb_pname, name);
    goto punt;
}

/*
 * Description (ndbIdToName)
 *
 *	This function looks up a specified id in the id-to-name DB
 *	file, and returns the associated name string.  This name
 *	can be used to retrieve a record using ndbFindName().
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	id			- id to look up
 *	plen			- pointer to returned length of name
 *				  (may be null, length includes null terminator
 *				  in a string)
 *	pname			- pointer to returned name string pointer
 *
 * Returns:
 *
 *	The return value is zero if the operation is successful.  An
 *	error is indicated by a negative return value (see nsdberr.h),
 *	and an error frame is generated if an error frame list was
 *	provided by the caller.
 */

int ndbIdToName(NSErr_t * errp,
		void * ndb, unsigned int id, int * plen, char **pname)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    char * name = 0;
    int namelen = 0;
    uint32 myid = id - 1;
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Is the id-to-name DB open for reading ids? */
    if (!(ndbp->ndb_flags & NDBF_RDID)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_RDID);
	if (rv) goto punt;
    }

    /* Set up record key */
#if BYTE_ORDER == LITTLE_ENDIAN
    M_32_SWAP(myid);
#endif
    key.data = (void *)&myid;
    key.size = sizeof(myid);

    /* Initialize record buffer descriptor */
    rec.data = 0;
    rec.size = 0;

    /* Retrieve the record by its key */
    rv = (*ndbp->ndb_idb->get)(ndbp->ndb_idb, &key, &rec, 0);
    if (rv) goto err_iget;

    /* Get the name pointer (terminating null is part of the name) */
    namelen = rec.size;
    name = (char *) rec.data;

  punt:
    /* Return name length and size if requested */
    if (plen) *plen = namelen;
    if (pname) *pname = name;

    return rv;

  err_iget:
    eid = NSDBERR1100;
    rv = NDBERRGET;
    nserrGenerate(errp, rv, eid, NSDB_Program,
		  2, ndbp->ndb_iname, system_errmsg());
    goto punt;
}

/*
 * Description (ndbInitPrimary)
 *
 *	This function creates and initializes the primary DB file.
 *	Initialization involves writing any required metadata records.
 *	Currently there is a ?dbtype record, which specifies the nsdb
 *	version number, and a database type and version number that
 *	were passed as arguments to ndbOpen().  There is also a
 *	?idmap record, which contains an allocation bitmap for id values
 *	used as keys in the associated id-to-name DB file.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned, and an error frame is generated if an
 *	error frame list was provided by the caller.
 */

int ndbInitPrimary(NSErr_t * errp, void * ndb)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint32 m;
    int i;
#endif
    int eid;				/* error id code */
    int rv;				/* result value */
    uint32 dbtype[4];

    /* Error if the primary DB is marked as existing already */
    if (!(ndbp->ndb_flags & NDBF_NONAME)) goto err_exists;

    /* First create the primary DB file */
    ndbp->ndb_pdb = dbopen(ndbp->ndb_pname, O_RDWR | O_CREAT | O_TRUNC,
			   NDBMODE, DB_HASH, 0);
    if (!ndbp->ndb_pdb) goto err_open;

    /* Generate data for the ?dbtype record */
    dbtype[0] = NDB_VERSION;
    dbtype[1] = ndbp->ndb_dbtype;
    dbtype[2] = ndbp->ndb_version;
    dbtype[3] = 0;
#if BYTE_ORDER == LITTLE_ENDIAN
    for (i = 0; i < 4; ++i) {
	m = dbtype[i];
	M_32_SWAP(m);
	dbtype[i] = m;
    }
#endif

    /* Set up descriptors for the ?dbtype record key and data */
    key.data = (void *)NDB_DBTYPE;
    key.size = strlen(NDB_DBTYPE) + 1;

    rec.data = (void *)dbtype;
    rec.size = sizeof(dbtype);

    /* Write the ?dbtype record out */
    rv = (*ndbp->ndb_pdb->put)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_mput1;

    /* Write out an empty ?idmap record */
    key.data = (void *)NDB_IDMAP;
    key.size = strlen(NDB_IDMAP) + 1;

    rec.data = 0;
    rec.size = 0;

    /* Write the ?idmap record */
    rv = (*ndbp->ndb_pdb->put)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_mput2;

    /* Close the DB file */
    (*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);

    /* Clear the flag that says the primary DB file does not exist */
    ndbp->ndb_flags &= ~(NDBF_NONAME|NDBF_RDNAME|NDBF_WRNAME);

    /* Indicate success */
    return 0;

  err_exists:
    /* Primary database already exists */
    eid = NSDBERR1200;
    rv = NDBERREXIST;
    nserrGenerate(errp, rv, eid, NSDB_Program, 1, ndbp->ndb_pname);
    return rv;

  err_open:
    /* Error opening primary database for write */
    eid = NSDBERR1220;
    rv = NDBERROPEN;
    goto err_dbio;

  err_mput1:
    /* Error writing "?dbtype" record */
    eid = NSDBERR1240;
    rv = NDBERRMDPUT;
    goto err_dbio;

  err_mput2:
    /* Error writing "?idmap" record */
    eid = NSDBERR1260;
    rv = NDBERRMDPUT;
    goto err_dbio;

  err_dbio:
    nserrGenerate(errp, rv, eid, NSDB_Program,
		  2, ndbp->ndb_pname, system_errmsg());
    
    /* Close the primary DB file if it exists */
    if (ndbp->ndb_pdb) {
	(*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);
	ndbp->ndb_flags &= ~(NDBF_RDNAME|NDBF_WRNAME);
    }

    /* Delete the file */
    system_unlink(ndbp->ndb_pname);
    return rv;
}

/*
 * Description (ndbOpen)
 *
 *	This function opens a server database by name.  The specified
 *	name may be the name of the primary DB file, or the name
 *	without the ".db" suffix.  This function will attempt to open
 *	both the primary and the id-to-name DB files for read access.
 *	If either of the DB files do not exist, they are not created
 *	here, but a handle for the database will still be returned.
 *	The DB files will be created when a subsequent access writes
 *	to the database.  The caller also specifies an application
 *	database type, which is checked against a value stored in
 *	in the database metadata, if the primary DB file exists, or
 *	which is stored in the file metadata when the file is created.
 *	A type-specific version number is passed and returned.  The
 *	value passed will be stored in the file metadata if it is
 *	subsequently created.  If the file exists, the value in the
 *	file metadata is returned, and it is the caller's responsibility
 *	to interpret it.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	dbname			- primary DB filename
 *	flags			- (currently unused - should be zero)
 *	dbtype			- application DB type (NDB_TYPE_xxxxx)
 *	version			- (in/out) type-specific version number
 *
 * Returns:
 *
 *	A handle that can be used for subsequent accesses to the database
 *	is returned, or 0, if an error occurs, and an error frame is
 *	generated if an error frame list was provided by the caller.
 */

void * ndbOpen(NSErr_t * errp,
	       char * dbname, int flags, int dbtype, int * version)
{
    NSDB_t * ndbp = 0;		/* database object pointer */
    char * pname = 0;		/* primary DB file name */
    char * iname = 0;		/* id-to-name DB file name */
    int namelen;
    uint32 dbtrec[4];
    uint32 m;
    DBT key;
    DBT rec;
    int eid;				/* error id code */
    int rv;				/* result value */

    /* Get the database name */
    namelen = strlen(dbname);
    if (!strcmp(&dbname[namelen-3], ".db")) {
	namelen -= 3;
    }

    /* Get the primary DB file name */
    pname = (char *)MALLOC(namelen + 4);
    if (pname == 0) goto err_nomem1;
    strncpy(pname, dbname, namelen);
    strcpy(&pname[namelen], ".db");

    /* Get the id-to-name DB file name */
    iname = (char *)MALLOC(namelen + 4);
    if (iname == 0) goto err_nomem2;
    strncpy(iname, dbname, namelen);
    strcpy(&iname[namelen], ".id");

    /* Allocate the database object */
    ndbp = (NSDB_t *)MALLOC(sizeof(NSDB_t));
    if (ndbp == 0) goto err_nomem3;

    /* Initialize the database object */
    ndbp->ndb_pname = pname;
    ndbp->ndb_pdb = 0;
    ndbp->ndb_iname = iname;
    ndbp->ndb_idb = 0;
    ndbp->ndb_flags = 0;
    ndbp->ndb_dbtype = dbtype;
    ndbp->ndb_version = (version) ? *version : 0;

    /* Open the primary DB file */
    ndbp->ndb_pdb = dbopen(pname, O_RDONLY, NDBMODE, DB_HASH, 0);

    /* Was it there? */
    if (ndbp->ndb_pdb) {

	/* Retrieve the ?dbtype record */
	key.data = (void *)NDB_DBTYPE;
	key.size = strlen(NDB_DBTYPE) + 1;

	rec.data = 0;
	rec.size = 0;

	/* Read the ?dbtype record */
	rv = (*ndbp->ndb_pdb->get)(ndbp->ndb_pdb, &key, &rec, 0);
	if (rv) goto err_mdget;

	/* Check it out */
	if (rec.size < 16) goto err_fmt;

	/* Copy data to an aligned area */
	memcpy((void *)dbtrec, rec.data, sizeof(dbtrec));

	/* Get the NSDB version number */
	m = dbtrec[0];
#if BYTE_ORDER == LITTLE_ENDIAN
	M_32_SWAP(m);
#endif
	/* Assume forward compatibility with versions up to current + 0.5 */
	if (m > (NDB_VERSION + 5)) goto err_vers;

	/* XXX Assume infinite backward compatibility */

	/* Get the application database type */
	m = dbtrec[1];
#if BYTE_ORDER == LITTLE_ENDIAN
	M_32_SWAP(m);
#endif
	/* It's got to match */
	if (m != dbtype) goto err_type;

	/* Get the type-specific version number */
	m = dbtrec[3];
#if BYTE_ORDER == LITTLE_ENDIAN
	M_32_SWAP(m);
#endif
	/* Don't check it.  Just return it. */
	if (version) *version = m;

	/* The value in dbtrec[3] is currently ignored */

	/* Mark the primary DB file open for read access */
	ndbp->ndb_flags |= NDBF_RDNAME;
    }
    else {
	/* Indicate that the primary DB file does not exist */
	ndbp->ndb_flags |= NDBF_NONAME;
    }

    return (void *)ndbp;

  err_nomem1:
    eid = NSDBERR1400;
    rv = NDBERRNOMEM;
    goto err_nomem;

  err_nomem2:
    eid = NSDBERR1420;
    rv = NDBERRNOMEM;
    goto err_nomem;

  err_nomem3:
    eid = NSDBERR1440;
    rv = NDBERRNOMEM;
  err_nomem:
    nserrGenerate(errp, rv, eid, NSDB_Program, 0);
    goto punt;

  err_mdget:
    eid = NSDBERR1460;
    rv = NDBERRMDGET;
    nserrGenerate(errp, rv, eid, NSDB_Program, 2, ndbp->ndb_pname,
		  system_errmsg());
    goto err_ret;

  err_fmt:
    eid = NSDBERR1480;
    rv = NDBERRMDFMT;
    goto err_ret;

  err_vers:
    {
	char vnbuf[16];

	eid = NSDBERR1500;
	rv = NDBERRVERS;
	sprintf(vnbuf, "%d", (int)m);
	nserrGenerate(errp, rv, eid, NSDB_Program, 2, ndbp->ndb_pname, vnbuf);
    }
    goto punt;

  err_type:
    eid = NSDBERR1520;
    rv = NDBERRDBTYPE;
    goto err_ret;

  err_ret:
    nserrGenerate(errp, rv, eid, NSDB_Program, 1, ndbp->ndb_pname);
    goto punt;

  punt:
    /* Error clean-up */
    if (pname) FREE(pname);
    if (iname) FREE(iname);
    if (ndbp) {
	/* Close the DB files if we got as far as opening them */
	if (ndbp->ndb_pdb) {
	    (*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);
	}
	if (ndbp->ndb_idb) {
	    (*ndbp->ndb_idb->close)(ndbp->ndb_idb);
	}
	FREE(ndbp);
    }
    return 0;
}

/*
 * Description (ndbReOpen)
 *
 *	This function is called to ensure that the primary DB file
 *	and/or the id-to-name DB file are open with specified access
 *	rights.  For example, a file may be open for read, and it needs
 *	to be open for write.  Both the primary and id-to-name DB files
 *	can be manipulated with a single call.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	flags			- (currently unused - should be zero)
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned (NDBERRxxxx - see nsdb.h).  If an error
 *	list is provided, an error frame will be generated when the
 *	return value is non-zero.
 */

int ndbReOpen(NSErr_t * errp, void * ndb, int flags)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    char * dbname;			/* database name pointer */
    int eid;
    int rv;

    /* Want to read or write the primary DB file? */
    if (flags & (NDBF_RDNAME|NDBF_WRNAME)) {

	/* Need to open for write? */
	if ((flags & NDBF_WRNAME) && !(ndbp->ndb_flags & NDBF_WRNAME)) {

	    /* If it's already open for read, close it first */
	    if (ndbp->ndb_flags & NDBF_RDNAME) {
		(*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);
		ndbp->ndb_flags &= ~NDBF_RDNAME;
	    }

	    /* Create it if it doesn't exist */
	    if (ndbp->ndb_flags & NDBF_NONAME) {
		rv = ndbInitPrimary(errp, ndb);
		if (rv) goto err_init;
	    }

	    /* Open primary DB file for write access */
	    dbname = ndbp->ndb_pname;
	    ndbp->ndb_pdb = dbopen(dbname, O_RDWR, NDBMODE, DB_HASH, 0);
	    if (!ndbp->ndb_pdb) goto err_open1;

	    /* Update flags to indicate successful open */
	    ndbp->ndb_flags |= (NDBF_RDNAME|NDBF_WRNAME);
	}

	/* Need to open for read? */
	if ((flags & NDBF_RDNAME) && !(ndbp->ndb_flags & NDBF_RDNAME)) {

	    /* If it's already open for write, close it first */
	    if (ndbp->ndb_flags & NDBF_WRNAME) {
		(*ndbp->ndb_pdb->close)(ndbp->ndb_pdb);
		ndbp->ndb_flags &= ~(NDBF_RDNAME|NDBF_WRNAME);
	    }

	    /* Open primary DB file for read access */
	    dbname = ndbp->ndb_pname;
	    ndbp->ndb_pdb = dbopen(dbname, O_RDONLY, NDBMODE, DB_HASH, 0);
	    if (!ndbp->ndb_pdb) goto err_open2;

	    /* Update flags to indicate successful open */
	    ndbp->ndb_flags |= NDBF_RDNAME;
	}
    }

    /* Want to read or write the id-to-name DB file? */
    if (flags & (NDBF_RDID|NDBF_WRID)) {

	/* Need to open for write? */
	if ((flags & NDBF_WRID) && !(ndbp->ndb_flags & NDBF_WRID)) {

	    /*
	     * If it's not open for read yet, try to open it for read
	     * in order to find out if it exists.
	     */
	    if (!(ndbp->ndb_flags & NDBF_RDID)) {

		/* Open id-to-name DB file for read access */
		dbname = ndbp->ndb_iname;
		ndbp->ndb_idb = dbopen(dbname, O_RDONLY, NDBMODE, DB_HASH,0);

		/* Does it exist? */
		if (ndbp->ndb_idb == 0) {

		    /* No, create it */
		    dbname = ndbp->ndb_iname;
		    ndbp->ndb_idb = dbopen(dbname,O_RDWR | O_CREAT | O_TRUNC,
					      NDBMODE, DB_HASH, 0);
		    if (!ndbp->ndb_idb) goto err_open3;
		    (*ndbp->ndb_idb->close)(ndbp->ndb_idb);
		}
		else {
		    /* Mark it open for read */
		    ndbp->ndb_flags |= NDBF_RDID;
		}
	    }

	    /* If it's already open for read, close it first */
	    if (ndbp->ndb_flags & NDBF_RDID) {
		(*ndbp->ndb_idb->close)(ndbp->ndb_idb);
		ndbp->ndb_flags &= ~NDBF_RDID;
	    }

	    /* Open id-to-name DB file for write access */
	    dbname = ndbp->ndb_iname;
	    ndbp->ndb_idb = dbopen(dbname, O_RDWR, NDBMODE, DB_HASH, 0);
	    if (!ndbp->ndb_idb) goto err_open4;

	    /* Update flags to indicate successful open */
	    ndbp->ndb_flags |= (NDBF_RDID|NDBF_WRID);
	}

	/* Need to open for read? */
	if ((flags & NDBF_RDID) && !(ndbp->ndb_flags & NDBF_RDID)) {

	    /* If it's already open for write, close it first */
	    if (ndbp->ndb_flags & NDBF_WRID) {
		(*ndbp->ndb_idb->close)(ndbp->ndb_idb);
		ndbp->ndb_flags &= ~(NDBF_RDID|NDBF_WRID);
	    }

	    /* Open id-to-name DB file for read access */
	    dbname = ndbp->ndb_iname;
	    ndbp->ndb_idb = dbopen(dbname, O_RDONLY, NDBMODE, DB_HASH, 0);
	    if (!ndbp->ndb_idb) goto err_open5;

	    /* Update flags to indicate successful open */
	    ndbp->ndb_flags |= NDBF_RDID;
	}
    }

    /* Successful completion */
    return 0;

    /* Begin error handlers */

  err_init:			/* failed to create primary DB file */
    (void)nserrGenerate(errp, NDBERRPINIT, NSDBERR1600, NSDB_Program,
			1,
			ndbp->ndb_pname		/* primary DB filename */
			);
    rv = NDBERRPINIT;
    goto punt;

  err_open1:
    eid = NSDBERR1620;
    goto err_open;

  err_open2:
    eid = NSDBERR1640;
    goto err_open;

  err_open3:
    eid = NSDBERR1660;
    goto err_open;

  err_open4:
    eid = NSDBERR1680;
    goto err_open;

  err_open5:
    eid = NSDBERR1700;
    goto err_open;

  err_open:			/* database open error */
    rv = NDBERROPEN;
    (void)nserrGenerate(errp, NDBERROPEN, eid, NSDB_Program,
			2, dbname, system_errmsg());

  punt:
    return rv;
}

NSPR_END_EXTERN_C

