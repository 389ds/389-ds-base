/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsadb_h
#define __nsadb_h

/*
 * Description (nsadb.h)
 *
 *	This file describes the interface for retrieving information
 *	from a Netscape authentication database.  This facility is
 *	built on top of the Netscape (server) database interface as
 *	defined in nsdb.h.  It represents a subclass of a more general
 *	authentication database interface defined in nsauth.h.
 */

#include "nserror.h"		/* error frame list support */
#include "nsautherr.h"		/* authentication error codes */
#include "nsauth.h"

/* Begin private definitions */
#ifdef __PRIVATE_NSADB

#include "nsdb.h"

#if defined(CLIENT_AUTH)
#define ADBDBNAMES	3		/* number of named files */
#else
#define ADBDBNAMES	2		/* number of named files */
#endif
#define ADBUSERDBNAME	"Users"		/* name of user database */
#define ADBGROUPDBNAME	"Groups"	/* name of group database */
#if defined(CLIENT_AUTH)
#define ADBCERTDBNAME	"Certs"		/* name of certificate mapping DB */
#define ADBUMAPDBNAME	"Certs.nm"	/* name of mapped user names DB */
#endif

typedef struct AuthDB_s AuthDB_t;
struct AuthDB_s {
    char * adb_dbname;			/* database name */
    void * adb_userdb;			/* handle for user database */
    void * adb_groupdb;			/* handle for group database */
#if defined(CLIENT_AUTH)
    void * adb_certdb;			/* handle for cert mapping database */
    void * adb_certlock;		/* lock for cert mapping database */
    void * adb_certnm;			/* handle for username-to-certid DB */
#endif
    int adb_flags;			/* flags */
};

/* Definitions for adb_flags (also used on nsadbOpenXxxx() calls) */
#define ADBF_NEW	0x1		/* newly created database */
#define ADBF_UREAD	0x10		/* user database open for read */
#define ADBF_UWRITE	0x20		/* user database open for write */
#define ADBF_GREAD	0x100		/* group database open for read */
#define ADBF_GWRITE	0x200		/* group database open for write */
#define ADBF_CREAD	0x1000		/* cert database open for read */
#define ADBF_CWRITE	0x2000		/* cert database open for write */
#endif /* __PRIVATE_NSADB */

NSPR_BEGIN_EXTERN_C

/* Functions in nsadb.c */
extern NSAPI_PUBLIC int nsadbOpen(NSErr_t * errp,
				  char * adbname, int flags, void **rptr);
extern NSAPI_PUBLIC void nsadbClose(void * authdb, int flags);
extern NSAPI_PUBLIC int nsadbOpenUsers(NSErr_t * errp,
				       void * authdb, int flags);
extern NSAPI_PUBLIC int nsadbOpenGroups(NSErr_t * errp,
					void * authdb, int flags);
extern NSAPI_PUBLIC int nsadbIdToName(NSErr_t * errp, void * authdb,
				      USI_t id, int flags, char **rptr);
extern NSAPI_PUBLIC int nsadbFindByName(NSErr_t * errp, void * authdb,
					char * name, int flags, void **rptr);

#if defined(CLIENT_AUTH)
#include "nscert.h"
#endif

/* Authentication database interface structure in nsadb.c */
extern AuthIF_t NSADB_AuthIF;

NSPR_END_EXTERN_C

#endif /* __nsadb_h */
