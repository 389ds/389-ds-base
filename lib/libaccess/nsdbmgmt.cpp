/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsdbmgmt.h)
 *
 *	The file describes the interface for managing information in
 *	a Netscape (server) database.  A database is composed of
 *	two (libdbm) DB files.  One of these (<dbname>.db) contains
 *	records indexed by a string key.  These records contain the
 *	primary information in the database.  A second DB file
 *	(<dbname>.id) is used to map an integer id value to a string
 *	key, which can then be used to locate a record in the first file.
 *	The interface for retrieving information from a database is
 *	described in nsdb.h.
 */

#include <base/systems.h>
#include <netsite.h>
#include <base/file.h>
#define __PRIVATE_NSDB
#include <libaccess/nsdbmgmt.h>
#include <base/util.h>

/*
 * Description (ndbAllocId)
 *
 *	This function allocates a unique id to be associated with a
 *	name in the primary DB file.  An id bitmap is maintained in
 *	the primary DB file as a metadata record, and an entry is
 *	created in the id-to-name DB for the assigned id and the
 *	specified name.  An allocated id value is always non-zero.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	namelen			- length of key of the desired record,
 *				  including null terminator if any
 *	name			- pointer to the key of the desired record
 *	id			- pointer to returned id value
 *
 * Returns:
 *
 *	If successful, the return value is zero, and the allocated id
 *	is returned through 'id'.  Otherwise a non-zero error code is
 *	returned (NDBERRxxxx - see nsdb.h).  If an error list is
 *	provided, an error frame will be generated when the return
 *	value is non-zero.
 */

int ndbAllocId(NSErr_t * errp,
	       void * ndb, int namelen, char * name, unsigned int * id)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    unsigned char * idmap;
    unsigned char * newmap = 0;
    int m;
    int mmsk;
    uint32 idval;
    int myid;
    int i, n;
    int rv;
    long eid;

    /*
     * Ensure that the name does not start with the metadata
     * prefix character.
     */
    if (!name || (name[0] == NDB_MDPREFIX)) goto err_name;

    /*
     * Read the primary DB file metadata record containing the id
     * allocation bitmap.
     */

    /*
     * We need the primary and the id-to-name DB files open for write
     * (and implicitly read) access.
     */
    if ((ndbp->ndb_flags & (NDBF_WRNAME|NDBF_WRID))
	!= (NDBF_WRNAME|NDBF_WRID)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, (NDBF_WRNAME|NDBF_WRID));
	if (rv < 0) goto punt;
    }

    /* Set the key to the id allocation bitmap record name */
    key.data = (void *)NDB_IDMAP;
    key.size = strlen(NDB_IDMAP) + 1;

    rec.data = 0;
    rec.size = 0;

    /* Retrieve the record by its key */
    rv = (*ndbp->ndb_pdb->get)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_mdget;

    /* Search for an available id in the bitmap */
    n = rec.size;
    idmap = (unsigned char *)rec.data;

    for (i = 0, m = 0; i < n; ++i) {

	m = idmap[i];
	if (m != 0) break;
    }

    /* Did we find a byte with an available bit? */
    if (m == 0) {

	/* No, need to grow the bitmap */
	newmap = (unsigned char *)MALLOC(rec.size + 32);
	if (newmap == 0) goto err_nomem1;

	/* Initialize free space at the beginning of the new map */
	for (i = 0; i < 32; ++i) {
	    newmap[i] = 0xff;
	}

	/* Copy the old map after it */
	n += 32;
	for ( ; i < n; ++i) {
	    newmap[i] = idmap[i-32];
	}

	/* Set i and m to allocate the new highest id value */
	i = 0;
	m = 0xff;
    }
    else {

	/*
	 * It's unfortunate, but it appears to be necessary to copy the
	 * the ?idmap record into a new buffer before updating it, rather
	 * than simply updating it in place.  The problem is that the
	 * libdbm put routine deletes the old record and then re-inserts
	 * it.  But once it has deleted the old record, it may take the
	 * opportunity to move another record into the space that the
	 * old record occupied, which is the same space that the new
	 * record occupies.  So the new record data is overwritten before
	 * new record is inserted.  :-(
	 */

	newmap = (unsigned char *)MALLOC(rec.size);
	if (newmap == 0) goto err_nomem2;

	memcpy((void *)newmap, (void *)idmap, rec.size);
    }

    /* Calculate the id associated with the low-order bit of byte i */
    myid = (n - i - 1) << 3;

    /* Find the first free (set) bit in that word */
    for (mmsk = 1; !(m & mmsk); mmsk <<= 1, myid += 1) ;

    /* Clear the bit */
    m &= ~mmsk;
    newmap[i] = m;

    /* Write the bitmap back out */

    rec.data = (void *)newmap;
    rec.size = n;

    rv = (*ndbp->ndb_pdb->put)(ndbp->ndb_pdb, &key, &rec, 0);

    /* Check for error on preceding put operation */
    if (rv) goto err_putpdb;

    /* Create the key for the id-to-name record */
    idval = myid;
#if BYTE_ORDER == LITTLE_ENDIAN
    M_32_SWAP(idval);
#endif

    key.data = (void *)&idval;
    key.size = sizeof(uint32);

    rec.data = (void *)name;
    rec.size = (namelen > 0) ? namelen : (strlen(name) + 1);

    /* Write the id-to-name record */
    rv = (*ndbp->ndb_idb->put)(ndbp->ndb_idb, &key, &rec, 0);
    if (rv) goto err_putidb;

    /* Return the id value + 1, to avoid returning a zero id */
    if (id) *id = myid + 1;

  punt:

    /* Free the new map space if any */
    if (newmap) {
	FREE(newmap);
    }

    return rv;

  err_name:				/* invalid name parameter */
    eid = NSDBERR2000;
    rv = NDBERRNAME;
    if (name == 0) {
	name = "(null)";
    }
    else if ((namelen > 0) && (namelen != strlen(name) + 1)) {
	name = "(unprintable)";
    }
    (void)nserrGenerate(errp, rv, eid, NSDB_Program,
			2,
			ndbp->ndb_pname,	/* primary DB filename */
			name			/* name string */
			);
    goto punt;

  err_mdget:				/* error on get from primary DB file */
    eid = NSDBERR2020;
    rv = NDBERRMDGET;
    (void)nserrGenerate(errp, rv, eid, NSDB_Program,
			2,
			ndbp->ndb_pname,	/* primary DB filename */
			(char *)key.data	/* key name string */
			);
    goto punt;

  err_nomem1:
    eid = NSDBERR2040;
    goto err_nomem;

  err_nomem2:
    eid = NSDBERR2060;
  err_nomem:				/* insufficient memory */
    rv = NDBERRNOMEM;
    (void)nserrGenerate(errp, rv, eid, NSDB_Program, 0);
    goto punt;
    
  err_putpdb:				/* error on put to primary DB file */
    eid = NSDBERR2080;
    rv = NDBERRMDPUT;
    (void)nserrGenerate(errp, rv, eid, NSDB_Program,
			2,
			ndbp->ndb_pname,	/* primary DB filename */
			(char *)key.data	/* key name string */
			);
    goto punt;

  err_putidb:				/* error on put to id-to-name DB */
    {
	char idstring[16];

	eid = NSDBERR2100;
	rv = NDBERRIDPUT;

	util_sprintf(idstring, "%d", myid);
	(void)nserrGenerate(errp, rv, eid, NSDB_Program,
			    2,
			    ndbp->ndb_iname,	/* id-to-name DB file */
			    idstring		/* id value for key */
			    );
    }
    goto punt;
}

/*
 * Description (ndbDeleteName)
 *
 *	This function deletes a named record from the primary DB file.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	flags			- (currently unused - should be zero)
 *	namelen			- length of name key, including null
 *				  terminator if any
 *	name			- pointer to name key
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned (NDBERRxxxx - see nsdberr.h).  If an error
 *	list is provided, an error frame will be generated when the
 *	return value is non-zero.
 */

int ndbDeleteName(NSErr_t * errp,
		  void * ndb, int flags, int namelen, char * name)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    int eid;
    int rv;

    /* Is the primary DB open for write access? */
    if (!(ndbp->ndb_flags & NDBF_WRNAME)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_WRNAME);
	if (rv) goto punt;
    }

    /* Set up the key descriptor */
    key.data = (void *)name;
    key.size = (namelen > 0) ? namelen : (strlen(name) + 1);

    /* Delete the record from the primary DB file */
    rv = (*ndbp->ndb_pdb->del)(ndbp->ndb_pdb, &key, 0);
    if (rv) goto err_delpdb;

    /* Successful completion */
    return 0;

    /* Begin error handlers */

  err_delpdb:			/* error deleting record from primary DB */
    eid = NSDBERR2200;
    rv = NDBERRNMDEL;
    (void)nserrGenerate(errp, rv, eid, NSDB_Program,
			2,
			ndbp->ndb_pname,	/* primary DB name */
			(char *)key.data	/* primary key */
			);
  punt:
    return rv;
}

/*
 * Description (ndbFreeId)
 *
 *	This function frees an id value associated with a name in the
 *	primary DB file.  It is normally called when the named record
 *	is being deleted from the primary DB file.  It deletes the
 *	record in the id-to-name DB file that is keyed by the id value,
 *	and updates the id allocation bitmap in the primary DB file.
 *	The caller may specify the name that is associated with the id
 *	value, in which case the id-to-name record will be fetched,
 *	and the name matched, before the record is deleted.  Alternatively
 *	the name parameter can be specified as zero, and id-to-name
 *	record will be deleted without a check.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	namelen			- length of name (including null terminator)
 *	name			- name associated with the id value (optional)
 *	id			- id value to be freed
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned, and an error frame is generated if the
 *	caller provided an error frame list.
 */

int ndbFreeId(NSErr_t * errp,
	      void * ndb, int namelen, char * name, unsigned int id)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    char * recname;
    DBT key;
    DBT rec;
    uint32 idval;
    int reclen;
    int mmsk;
    unsigned char * idmap = 0;
    int i;
    int eid;
    int rv;

    /*
     * We need the primary and the id-to-name DB files open for write
     * (and implicitly read) access.
     */
    if ((ndbp->ndb_flags & (NDBF_WRNAME|NDBF_WRID))
	!= (NDBF_WRNAME|NDBF_WRID)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, (NDBF_WRNAME|NDBF_WRID));
	if (rv) goto punt;
    }

    /* Was the name for this id value provided by the caller? */
    if (name) {

	/* Get length of name if not provided */
	if (namelen <= 0) namelen = strlen(name) + 1;

	/* Yes, look up the id and check for a match */
	rv = ndbIdToName(errp, ndb, id, &reclen, &recname);
	if (rv < 0) goto punt;

	/* Fail if the supplied name doesn't match */
	if ((namelen != reclen) ||
	    strncmp(recname, name, reclen)) goto err_badid1;
    }

    /* Caller views the id space as starting at 1, but we start at 0 */
    id -= 1;

    /* Create the key for the id-to-name record */
    idval = id;
#if BYTE_ORDER == LITTLE_ENDIAN
    M_32_SWAP(idval);
#endif

    key.data = (void *)&idval;
    key.size = sizeof(uint32);

    /* Delete the id-to-name record */
    rv = (*ndbp->ndb_idb->del)(ndbp->ndb_idb, &key, 0);
    if (rv) goto err_del;

    /* Set the key to the id allocation bitmap record name */
    key.data = (void *)NDB_IDMAP;
    key.size = strlen(NDB_IDMAP) + 1;

    rec.data = 0;
    rec.size = 0;

    /* Retrieve the record by its key */
    rv = (*ndbp->ndb_pdb->get)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_mdget;

    /* Make sure the id is in the range of the bitmap */
    i = (rec.size << 3) - id - 1;
    if (i < 0) goto err_badid2;

    /*
     * See comment in ndbAllocId() about updating ?idmap.  Bottom line
     * is: we have to copy the record before updating it.
     */

    idmap = (unsigned char *)MALLOC(rec.size);
    if (idmap == 0) goto err_nomem;

    memcpy((void *)idmap, rec.data, rec.size);

    /* Calculate the index of the byte with this id's bit */
    i >>= 3;

    /* Calculate the bitmask for the bitmap byte */
    mmsk = 1 << (id & 7);

    /* Set the bit in the bitmap */
    idmap[i] |= mmsk;

    /* Write the bitmap back out */

    rec.data = (void *)idmap;

    rv = (*ndbp->ndb_pdb->put)(ndbp->ndb_pdb, &key, &rec, 0);
    if (rv) goto err_mdput;

  punt:

    if (idmap) {
	FREE(idmap);
    }

    return rv;

  err_badid1:
    /* Name associated with id doesn't match supplied name */
    eid = NSDBERR2300;
    rv = NDBERRBADID;
    goto err_id;

  err_del:
    /* Error deleting id-to-name record */
    eid = NSDBERR2320;
    rv = NDBERRIDDEL;
    goto err_dbio;

  err_mdget:
    /* Error reading id bitmap from primary DB file */
    eid = NSDBERR2340;
    rv = NDBERRMDGET;
    goto err_dbio;

  err_badid2:
    eid = NSDBERR2360;
    rv = NDBERRBADID;
  err_id:
    {
	char idbuf[16];

	util_sprintf(idbuf, "%d", id);
	nserrGenerate(errp, rv, eid, NSDB_Program, 2, ndbp->ndb_pname, idbuf);
    }
    goto punt;

  err_nomem:
    eid = NSDBERR2380;
    rv = NDBERRNOMEM;
    nserrGenerate(errp, rv, eid, NSDB_Program, 0);
    goto punt;

  err_mdput:
    eid = NSDBERR2400;
    rv = NDBERRMDPUT;
    goto err_dbio;

  err_dbio:
    nserrGenerate(errp, rv, eid, NSDB_Program,
		  2, ndbp->ndb_pname, system_errmsg());
    goto punt;
}

/*
 * Description (ndbRenameId)
 *
 *	This function changes the name associated with a specified id
 *	int the id-to-name DB file.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	namelen			- length of new name string, including
 *				  null terminator if any
 *	newname			- pointer to the new name string
 *	id			- id value to be renamed
 *
 * Returns:
 *
 *	The return value is zero if the operation is successful.  An
 *	error is indicated by a non-zero return value, and an error
 *	frame is generated if the caller provided an error frame list.
 */

int ndbRenameId(NSErr_t * errp,
		void * ndb, int namelen, char * newname, unsigned int id)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    uint32 idval = id - 1;
    int eid;
    int rv;

    /*
     * Ensure that the name does not start with the metadata
     * prefix character.
     */
    if (!newname || (newname[0] == NDB_MDPREFIX)) goto err_name;

    /*
     * We need the id-to-name DB file open for write
     * (and implicitly read) access.
     */
    if (!(ndbp->ndb_flags & NDBF_WRID)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_WRID);
	if (rv) goto punt;
    }

    /* Set up record key */
#if BYTE_ORDER == LITTLE_ENDIAN
    M_32_SWAP(idval);
#endif
    key.data = (void *)&idval;
    key.size = sizeof(uint32);

    rec.data = 0;
    rec.size = 0;

    /* Retrieve the record by its key */
    rv = (*ndbp->ndb_idb->get)(ndbp->ndb_idb, &key, &rec, 0);
    if (rv) goto err_idget;

    /* Set up to write the new name */
    rec.data = (void *)newname;
    rec.size = (namelen > 0) ? namelen : (strlen(newname) + 1);

    /* Write the id-to-name record */
    rv = (*ndbp->ndb_idb->put)(ndbp->ndb_idb, &key, &rec, 0);
    if (rv) goto err_idput;

  punt:
    return rv;

  err_name:
    eid = NSDBERR2500;
    rv = NDBERRNAME;
    if (newname == 0) newname = "(null)";
    else if ((namelen > 0) && (namelen != (strlen(newname) + 1))) {
	newname = "(unprintable)";
    }
    (void)nserrGenerate(errp, rv, eid, NSDB_Program,
			2,
			ndbp->ndb_pname,	/* primary DB filename */
			newname			/* name string */
			);
    goto punt;

  err_idget:
    /* Error getting id record from id-to-name database */
    eid = NSDBERR2520;
    rv = NDBERRGET;
    goto err_dbio;

  err_idput:
    /* Error putting id record back to id-to-name database */
    eid = NSDBERR2540;
    rv = NDBERRIDPUT;
  err_dbio:
    nserrGenerate(errp, rv, eid, NSDB_Program,
		  2, ndbp->ndb_pname, system_errmsg());
    goto punt;
}

/*
 * Description (ndbStoreName)
 *
 *	This function stores a record, keyed by a specified name, in the
 *	primary DB file.  The record will overwrite any existing record
 *	with the same key, unless NDBF_NEWNAME, is included in the 'flags'
 *	argument.  If NDBF_NEWNAME is set, and the record already exists,
 *	it is not overwritten, and an error is returned.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	ndb			- database handle from ndbOpen()
 *	flags			- bit flags:
 *					NDBF_NEWNAME - name is new
 *	namelen			- length of name key, including null
 *				  terminator if any
 *	name			- pointer to name key
 *	reclen			- length of the record data
 *	recptr			- pointer to the record data
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise a non-zero
 *	error code is returned, and an error frame is generated if the
 *	caller provided an error frame list.
 */

int ndbStoreName(NSErr_t * errp, void * ndb, int flags,
		 int namelen, char * name, int reclen, char * recptr)
{
    NSDB_t * ndbp = (NSDB_t *)ndb;	/* database object pointer */
    DBT key;
    DBT rec;
    int eid;
    int rv;

    /* Is the primary DB open for write access? */
    if (!(ndbp->ndb_flags & NDBF_WRNAME)) {

	/* No, (re)open it */
	rv = ndbReOpen(errp, ndb, NDBF_WRNAME);
	if (rv) goto punt;
    }

    /* Set up the key and record descriptors */
    key.data = (void *)name;
    key.size = (namelen > 0) ? namelen : (strlen(name) + 1);

    rec.data = (void *)recptr;
    rec.size = reclen;

    /* Write the record to the primary DB file */
    rv = (*ndbp->ndb_pdb->put)(ndbp->ndb_pdb, &key, &rec,
			       (flags & NDBF_NEWNAME) ? R_NOOVERWRITE : 0);
    if (rv) goto err_put;

  punt:
    return rv;

  err_put:
    eid = NSDBERR2700;
    rv = NDBERRPUT;
    nserrGenerate(errp, rv, eid, NSDB_Program,
		  2, ndbp->ndb_pname, system_errmsg());
    goto punt;
}
