/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsdb_h
#define __nsdb_h

/*
 * Description (nsdb.h)
 *
 *	This file describes the interface for retrieving information
 *	from a Netscape (server) database.  A database is composed of
 *	two (libdbm) DB files.  One of these (<dbname>.db) contains
 *	records indexed by a string key.  These records contain the
 *	primary information in the database.  A second DB file
 *	(<dbname>.id) is used to map an integer id value to a string
 *	key, which can then be used to locate a record in the first file.
 *	The interface for managing information in a database is described
 *	in nsdbmgmt.h.
 */

/* Begin private definitions */
#ifdef __PRIVATE_NSDB

#include "mcom_db.h"

/*
 * Description (NSDB_t)
 *
 *	This type describes the structure that used to represent a
 *	Netscape server database.  It includes fields to reference
 *	both the primary and id-to-name DB files, and information
 *	about the current state of the database.
 */

typedef struct NSDB_s NSDB_t;
struct NSDB_s {
    char * ndb_pname;			/* primary DB file name pointer */
    DB * ndb_pdb;			/* primary DB file handle */
    char * ndb_iname;			/* id-to-name DB file name pointer */
    DB * ndb_idb;			/* id-to-name DB file handle */
    int ndb_flags;			/* bit flags */
#define NDBF_RDNAME	0x1		/* primary DB open for read */
#define NDBF_WRNAME	0x2		/* primary DB open for write */
#define NDBF_NONAME	0x4		/* primary DB does not exist */
#define NDBF_RDID	0x10		/* id-to-name DB open for read */
#define NDBF_WRID	0x20		/* id-to-name DB open for write */
#define NDBF_NOID	0x40		/* id-to-name DB does not exist */

    int ndb_dbtype;			/* database type */
    int ndb_version;			/* type-specific version number */
};

/* Define metadata record keys (must start with NDB_MDPREFIX) */
#define NDB_DBTYPE	"?dbtype"	/* database type and version info */
#define NDB_IDMAP	"?idmap"	/* id allocation bitmap */

#endif /* __PRIVATE_NSDB */

/* Begin public definitions */

#include "nserror.h"		/* error frame list support */
#include "nsdberr.h"		/* error codes for NSDB facility */

/* Define the NSDB version number */
#define NDB_VERSION		0x10	/* NSDB version 1.0 */

/* Define reserved database type codes for ndb_dbtype */
#define NDB_TYPE_USERDB		1	/* user database */
#define NDB_TYPE_GROUPDB	2	/* group database */
#define NDB_TYPE_CLIENTDB	3	/* client database */
#define NDB_TYPE_ACLDB		4	/* access control list database */

/*
 * Define the metadata record key prefix character.  Normal data record
 * keys (names) cannot begin with this character.
 */
#define NDB_MDPREFIX	'?'

/* Define flags for ndbEnumerate() */
#define NDBF_ENUMNORM	0x1		/* enumerate normal data records */
#define NDBF_ENUMMETA	0x2		/* enumerate metadata records */

/* Define return values for a user function called by ndbEnumerate */
#define NDB_ENUMSTOP	-1		/* terminate enumeration */
#define NDB_ENUMCONT	0		/* continue enumeration */
#define NDB_ENUMRESET	1		/* restart enumeration at beginning */

NSPR_BEGIN_EXTERN_C

/* Functions for database information retrieval in nsdb.c */
extern void ndbClose(void * ndb, int flags);

/* for ANSI C++ standard on SCO UDK, otherwise fn name is mangled */
#ifdef UnixWare
typedef int (*ArgFn_ndbEnum)(NSErr_t * ferrp, void * parg, int namelen,
                           char * name, int reclen, char * recptr);
extern int ndbEnumerate(NSErr_t * errp, void * ndb, int flags, void * argp,
                        ArgFn_ndbEnum);
#else /* UnixWare */
extern int ndbEnumerate(NSErr_t * errp, void * ndb, int flags, void * argp,
			int (*func)(NSErr_t * ferrp, void * parg,
				    int namelen, char * name,
				    int reclen, char * recptr));
#endif /* UnixWare */
extern int ndbFindName(NSErr_t * errp, void * ndb, int namelen, char * name,
		       int * reclen, char **recptr);
extern int ndbIdToName(NSErr_t * errp,
		       void * ndb, unsigned int id, int * plen, char **pname);
extern int ndbInitPrimary(NSErr_t * errp, void * ndb);
extern void * ndbOpen(NSErr_t * errp,
		      char * dbname, int flags, int dbtype, int * version);
extern int ndbReOpen(NSErr_t * errp, void * ndb, int flags);

NSPR_END_EXTERN_C

/* richm - 20020218 - these macros were added as part of the port to DBM 1.6
 * apparently, these were exported for outside use from mcom_db.h in
 * DBM 1.5x and earlier, but were made private in 1.6 - so I copied them
 * here
 */
/*
 * Little endian <==> big endian 32-bit swap macros.
 *	M_32_SWAP	swap a memory location
 *	P_32_SWAP	swap a referenced memory location
 *	P_32_COPY	swap from one location to another
 */
#ifndef M_32_SWAP
#define	M_32_SWAP(a) {							\
	uint32 _tmp = a;						\
	((char *)&a)[0] = ((char *)&_tmp)[3];				\
	((char *)&a)[1] = ((char *)&_tmp)[2];				\
	((char *)&a)[2] = ((char *)&_tmp)[1];				\
	((char *)&a)[3] = ((char *)&_tmp)[0];				\
}
#endif
#ifndef P_32_SWAP
#define	P_32_SWAP(a) {							\
	uint32 _tmp = *(uint32 *)a;				\
	((char *)a)[0] = ((char *)&_tmp)[3];				\
	((char *)a)[1] = ((char *)&_tmp)[2];				\
	((char *)a)[2] = ((char *)&_tmp)[1];				\
	((char *)a)[3] = ((char *)&_tmp)[0];				\
}
#endif
#ifndef P_32_COPY
#define	P_32_COPY(a, b) {						\
	((char *)&(b))[0] = ((char *)&(a))[3];				\
	((char *)&(b))[1] = ((char *)&(a))[2];				\
	((char *)&(b))[2] = ((char *)&(a))[1];				\
	((char *)&(b))[3] = ((char *)&(a))[0];				\
}
#endif
/*
 * Little endian <==> big endian 16-bit swap macros.
 *	M_16_SWAP	swap a memory location
 *	P_16_SWAP	swap a referenced memory location
 *	P_16_COPY	swap from one location to another
 */
#ifndef M_16_SWAP
#define	M_16_SWAP(a) {							\
	uint16 _tmp = a;						\
	((char *)&a)[0] = ((char *)&_tmp)[1];				\
	((char *)&a)[1] = ((char *)&_tmp)[0];				\
}
#endif
#ifndef P_16_SWAP
#define	P_16_SWAP(a) {							\
	uint16 _tmp = *(uint16 *)a;				\
	((char *)a)[0] = ((char *)&_tmp)[1];				\
	((char *)a)[1] = ((char *)&_tmp)[0];				\
}
#endif
#ifndef P_16_COPY
#define	P_16_COPY(a, b) {						\
	((char *)&(b))[0] = ((char *)&(a))[1];				\
	((char *)&(b))[1] = ((char *)&(a))[0];				\
}
#endif

#endif /* __nsdb_h */
