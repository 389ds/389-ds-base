/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* ldbm.h - ldap dbm compatibility routine header file */

#error "Hmm, shoudn't be here"
/* Deprecated header, why are you including it ??? */

#if 1		

#ifndef _LDBM_H_
#define _LDBM_H_

/* define LDAP_USE_DB185 to get the old db library, otherwise, use db2.0 */
#ifndef LDAP_USE_DB185
#define LDAP_USE_DB20
#endif

#ifdef LDBM_USE_GDBM

/*****************************************************************
 *                                                               *
 * use gdbm if possible                                          *
 *                                                               *
 *****************************************************************/

#include <gdbm.h>

typedef datum		Datum;

typedef GDBM_FILE	LDBM;

extern gdbm_error	gdbm_errno;

/* for ldbm_open */
#define LDBM_READER	GDBM_READER
#define LDBM_WRITER	GDBM_WRITER
#define LDBM_WRCREAT	GDBM_WRCREAT
#define LDBM_NEWDB	GDBM_NEWDB
#define LDBM_FAST	GDBM_FAST

#define LDBM_SUFFIX	".gdbm"

/* for ldbm_insert */
#define LDBM_INSERT	GDBM_INSERT
#define LDBM_REPLACE	GDBM_REPLACE
#define LDBM_SYNC	0x80000000

#else /* end of gdbm */

#ifdef LDBM_USE_DBHASH

/*****************************************************************
 *                                                               *
 * use berkeley db hash package                                  *
 *                                                               *
 *****************************************************************/

#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <db.h>

typedef DBT	Datum;
#define dsize	size
#define dptr	data

typedef DB	*LDBM;

#define DB_TYPE		DB_HASH

/* for ldbm_open */
#define LDBM_READER	O_RDONLY
#define LDBM_WRITER	O_RDWR
#define LDBM_WRCREAT	(O_RDWR|O_CREAT)
#define LDBM_NEWDB	(O_RDWR|O_TRUNC|O_CREAT)
#define LDBM_FAST	0

#define LDBM_SUFFIX	".dbh"

/* for ldbm_insert */
#define LDBM_INSERT	R_NOOVERWRITE
#define LDBM_REPLACE	0
#define LDBM_SYNC	0x80000000

#else /* end of db hash */

#ifdef LDBM_USE_DBBTREE

/*****************************************************************
 *                                                               *
 * use berkeley db btree package                                 *
 *                                                               *
 *****************************************************************/

#ifndef LDAP_USE_DB20 /* old-db needed us to include these system headers first */
#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef HPUX11
#define	__BIT_TYPES_DEFINED__
typedef unsigned char u_int8_t;
typedef unsigned int u_int32_t;
typedef unsigned short u_int16_t;
#endif
#include <db.h>

#define DB_TYPE		DB_BTREE

#define LDBM_ORDERED	1

#ifdef LDAP_USE_DB20

/* pull in parts of the new interface , this comes from dblayer.h */

typedef struct _tag_dblayer_session{
	DB_ENV	db_env;
} *dblayer_session, dblayer_session_struct;


/* for ldbm_insert */
#define LDBM_INSERT	DB_NOOVERWRITE
#define LDBM_REPLACE 0 /* Db2.0 default is to replace */
#define LDBM_SYNC	0x80000000

typedef DBT Datum;
#define dsize	size
#define dptr	data

typedef struct _ldbm {
	DB	*pReal_DB;
	DBC	*pCursor;
} _ldbmstruct, *LDBM;

/* for ldbm_open */
#define LDBM_READER	DB_RDONLY
#define LDBM_WRITER	0
#define LDBM_WRCREAT	DB_CREATE
#define LDBM_NEWDB	(DB_TRUNCATE | DB_CREATE)
#define LDBM_FAST	0

#define LDBM_SUFFIX	".db2"
#else /* DB 1.85 */

/* for ldbm_insert */
#define LDBM_INSERT	R_NOOVERWRITE
#define LDBM_REPLACE	0
#define LDBM_SYNC	0x80000000

typedef DBT	Datum;
#define dsize	size
#define dptr	data

typedef DB	*LDBM;
/* for ldbm_open */
#define LDBM_READER	O_RDONLY
#define LDBM_WRITER	O_RDWR
#define LDBM_WRCREAT	(O_RDWR|O_CREAT)
#define LDBM_NEWDB	(O_RDWR|O_TRUNC|O_CREAT)
#define LDBM_FAST	0

#define LDBM_SUFFIX	".dbb"
#endif /* LDAP_USE_DB20 */

#else /* end of db btree */

#ifdef LDBM_USE_NDBM

/*****************************************************************
 *                                                               *
 * if none of the above use ndbm, the standard unix thing        *
 *                                                               *
 *****************************************************************/

#include <ndbm.h>
#ifndef O_RDONLY
#include <fcntl.h>
#endif

typedef datum	Datum;

typedef DBM	*LDBM;

/* for ldbm_open */
#define LDBM_READER	O_RDONLY
#define LDBM_WRITER	O_WRONLY
#define LDBM_WRCREAT	(O_RDWR|O_CREAT)
#define LDBM_NEWDB	(O_RDWR|O_TRUNC|O_CREAT)
#define LDBM_FAST	0

#define LDBM_SUFFIX	".ndbm"

/* for ldbm_insert */
#define LDBM_INSERT	DBM_INSERT
#define LDBM_REPLACE	DBM_REPLACE
#define LDBM_SYNC	0

#else /* end of ndbm */

#ifdef LDBM_USE_CISAM

/*****************************************************************
 *                                                               *
 * use CISAM db package                                          *
 *                                                               *
 *****************************************************************/

#include <sys/types.h>
#include <sys/errno.h>
#include <limits.h>
#include <fcntl.h>
#include "isam.h"

extern int	errno;

struct datum {
	void    *dptr;                  /* data */
        size_t   dsize;                 /* data length */
};

typedef struct datum	Datum;

struct ldbm {
	int	fd;			/* all callers expect a ptr */
	int	cur_recnum;		/* for reading sequentially */
};

typedef struct ldbm	*LDBM;

/* for ldbm_open */
#define LDBM_READER	(ISINPUT | ISVARLEN | ISMANULOCK)
#define LDBM_WRITER	(ISINOUT | ISVARLEN | ISMANULOCK)
#define LDBM_WRCREAT	(ISINOUT | ISVARLEN | ISMANULOCK | ISEXCLLOCK)
#define LDBM_NEWDB	(ISINOUT | ISVARLEN | ISMANULOCK | ISEXCLLOCK)
#define LDBM_FAST	0

#define LDBM_SUFFIX	""
#define LDBM_ORDERED	1

/* for ldbm_insert */
#define LDBM_INSERT	1
#define LDBM_REPLACE	0
#define LDBM_SYNC	0x80000000

#else /* end of cisam */

#ifdef LDBM_USE_TRIO

/*****************************************************************
 *                                                               *
 * use C-Index/II from Trio                                      *
 *                                                               *
 *****************************************************************/

#include <sys/types.h>
#include <sys/errno.h>
#include <limits.h>
#include <fcntl.h>
#include "cndx.h"

#define	CRDCREAT	0x100

extern int	errno;

struct datum {
	void    *dptr;                  /* data */
        size_t   dsize;                 /* data length */
};

typedef struct datum	Datum;

typedef CFILE		*LDBM;

/* for ldbm_open */
#define LDBM_READER	(CRDONLY)
#define LDBM_WRITER	(CRDWRITE)
#define LDBM_WRCREAT	(CRDWRITE | CRDCREAT)
#define LDBM_NEWDB	(CRDWRITE | CRDCREAT)
#define LDBM_FAST	0

#define LDBM_SUFFIX	".c2i"
#define LDBM_ORDERED	1

/* for ldbm_insert */
#define LDBM_INSERT	1
#define LDBM_REPLACE	0
#define LDBM_SYNC	0x80000000


#else /* end of trio */

#ifdef LDBM_USE_CTREE

/*****************************************************************
 *                                                               *
 * use Faircom Ctree db package                                  *
 *                                                               *
 *****************************************************************/

#include <sys/types.h>
#include <sys/errno.h>
#include <limits.h>
#include <fcntl.h>

#include "ctstdr.h"
#include "ctoptn.h"
#include "ctaerr.h"
#include "ctdecl.h"
#include "cterrc.h"

extern int	errno;

struct datum {
	void    *dptr;                  /* data */
        size_t   dsize;                 /* data length */
};

typedef struct datum	Datum;
typedef IFIL	*LDBM;

/* for ldbm_open */
#define LDBM_READER	0
#define LDBM_WRITER	0
#define LDBM_WRCREAT	1
#define LDBM_NEWDB	1
#define LDBM_FAST	0

#define LDBM_SUFFIX	""
#define LDBM_ORDERED	1

/* for ldbm_insert */
#define LDBM_INSERT	1
#define LDBM_REPLACE	0
#define LDBM_SYNC	0x80000000

#endif /* ctree */
#endif /* trio */
#endif /* cisam */
#endif /* ndbm */
#endif /* db hash */
#endif /* db btree */
#endif /* gdbm */

/*
 * name: file name without the suffix
 * rw: read/write flags
 * mode: this has the desired permissions mode on the file
 * dbcachesize: advisory cache size in bytes
 */
LDBM	ldbm_open( char *name, int rw, int mode, int dbcachesize );
#ifdef LDAP_USE_DB20
/* This is a stopgap measure to allow us to associate a session with ldbm_ calls */
LDBM	ldbm_open2( dblayer_session session, char *name, int rw, int mode);
/* These are stolen from beta2's dblayer.h */
int dblayer_session_open(char *home_dir, char* log_dir, char* temp_dir, int cachesize, dblayer_session session) ;
int dblayer_session_terminate(dblayer_session session) ;
#endif
int	ldbm_close( LDBM ldbm );
void	ldbm_sync( LDBM ldbm );
void	ldbm_datum_free( LDBM ldbm, Datum data );
Datum	ldbm_datum_dup( LDBM ldbm, Datum data );
Datum	ldbm_fetch( LDBM ldbm, Datum key );
int	ldbm_store( LDBM ldbm, Datum key, Datum data, int flags );
int	ldbm_delete( LDBM ldbm, Datum key );
Datum	ldbm_firstkey( LDBM ldbm );
Datum	ldbm_nextkey( LDBM ldbm, Datum key );
Datum	ldbm_prevkey( LDBM ldbm, Datum key );
Datum	ldbm_lastkey( LDBM ldbm );
Datum	ldbm_cursorkey( LDBM ldbm, Datum key );
int	ldbm_errno( LDBM ldbm );

#endif /* _ldbm_h_ */

#endif /* 0 */
