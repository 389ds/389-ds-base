/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsdbmgmt_h
#define __nsdbmgmt_h

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
 *
 * FUTURE:
 *	Normally the records in the primary DB file will contain the
 *	id values which are used to key the id-to-name DB.  When this
 *	is the case, it is possible to construct the id-to-name DB from
 *	the primary DB file, and an interface is provided to facilitate
 *	this.
 */

#include "nsdb.h"			/* database access */

/* Define flags for ndbStoreName() */
#define NDBF_NEWNAME	0x1		/* this is (should be) a new name */

NSPR_BEGIN_EXTERN_C

/* Functions for database management in nsdbmgmt.c */
extern int ndbAllocId(NSErr_t * errp, void * ndb,
		      int namelen, char * name, unsigned int * id);
extern int ndbDeleteName(NSErr_t * errp,
			 void * ndb, int flags, int namelen, char * name);
extern int ndbFreeId(NSErr_t * errp,
		     void * ndb, int namelen, char * name, unsigned int id);
extern int ndbRenameId(NSErr_t * errp, void * ndb,
		       int namelen, char * newname, unsigned int id);
extern int ndbStoreName(NSErr_t * errp, void * ndb, int flags,
			int namelen, char * name, int reclen, char * recptr);
extern int ndbSync(NSErr_t * errp, void * ndb, int flags);

NSPR_END_EXTERN_C

#endif /* __nsdbmgmt_h */
