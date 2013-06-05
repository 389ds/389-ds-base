/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2010 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* cl5_api.c - implementation of 5.0 style changelog API */

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#if defined( OS_solaris ) || defined( hpux )
#include <sys/types.h>
#include <sys/statvfs.h>
#endif
#if defined( linux )
#include <sys/vfs.h>
#endif


#include "cl5.h"
#include "cl_crypt.h"
#include "plhash.h" 
#include "plstr.h"

#include "db.h"
#include "cl5_clcache.h" /* To use the Changelog Cache */
#include "repl5.h"	 /* for agmt_get_consumer_rid() */

#define GUARDIAN_FILE		"guardian"		/* name of the guardian file */
#define VERSION_FILE		"DBVERSION"		/* name of the version file  */
#define MAX_TRIALS			50				/* number of retries on db operations */
#define V_5					5				/* changelog entry version */
#define CHUNK_SIZE			64*1024
#define DBID_SIZE			64
#define FILE_SEP            "_"             /* separates parts of the db file name */

#define T_CSNSTR			"csn"
#define T_UNIQUEIDSTR 		"nsuniqueid"
#define T_PARENTIDSTR		"parentuniqueid"
#define T_NEWSUPERIORDNSTR	"newsuperiordn"
#define T_NEWSUPERIORIDSTR	"newsuperioruniqueid"
#define T_REPLGEN           "replgen"

#define ENTRY_COUNT_TIME	111 /* this time is used to construct csn 
								   used to store/retrieve entry count */
#define PURGE_RUV_TIME      222 /* this time is used to construct csn
                                   used to store purge RUV vector */
#define MAX_RUV_TIME        333 /* this time is used to construct csn
                                   used to store upper boundary RUV vector */

#define DB_EXTENSION_DB3	"db3"
#define DB_EXTENSION	"db4"

#define HASH_BACKETS_COUNT 16   /* number of buckets in a hash table */

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4100
#define USE_DB_TXN 1 /* use transactions */
#define DEFAULT_DB_ENV_OP_FLAGS DB_AUTO_COMMIT
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR <= 4300
/* we are enabling transactions everywhere - since we are opening databases
   with DB_AUTO_COMMIT we must either open and pass a valid txn handle to
   all operations that modify the database (put, del) or we must pass the
   DB_AUTO_COMMIT flag to those operations */
#define DEFAULT_DB_OP_FLAGS(txn) (txn ? 0 : DB_AUTO_COMMIT)
#else
#define DEFAULT_DB_OP_FLAGS(txn) 0
#endif
#define DB_OPEN(oflags, db, txnid, file, database, type, flags, mode, rval)    \
{                                                                              \
	if (((oflags) & DB_INIT_TXN) && ((oflags) & DB_INIT_LOG))                  \
	{                                                                          \
		(rval) = (db)->open((db), (txnid), (file), (database), (type), (flags)|DB_AUTO_COMMIT, (mode)); \
	}                                                                          \
	else                                                                       \
	{                                                                          \
		(rval) = (db)->open((db), (txnid), (file), (database), (type), (flags), (mode)); \
	}                                                                          \
}
#else /* older then db 41 */
#define DEFAULT_DB_OP_FLAGS(txn) 0
#define DB_OPEN(oflags, db, txnid, file, database, type, flags, mode, rval)    \
	(rval) = (db)->open((db), (file), (database), (type), (flags), (mode))
#endif

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4000
#define DB_ENV_SET_REGION_INIT(env) (env)->set_flags((env), DB_REGION_INIT, 1)
#define TXN_BEGIN(env, parent_txn, tid, flags) \
    (env)->txn_begin((env), (parent_txn), (tid), (flags))
#define TXN_COMMIT(txn, flags) (txn)->commit((txn), (flags))
#define TXN_ABORT(txn) (txn)->abort(txn)
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
	(env)->memp_stat((env), (gsp), (fsp), (flags))
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4400 /* db4.4 or later */
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
    (env)->mutex_set_tas_spins((env), (tas_spins))
#else
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
    (env)->set_tas_spins((env), (tas_spins))
#endif

#else	/* older than db 4.0 */
#define DB_ENV_SET_REGION_INIT(env) db_env_set_region_init(1)
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
	db_env_set_tas_spins((tas_spins))
#define TXN_BEGIN(env, parent_txn, tid, flags) \
    txn_begin((env), (parent_txn), (tid), (flags))
#define TXN_COMMIT(txn, flags) txn_commit((txn), (flags))
#define TXN_ABORT(txn) txn_abort((txn))

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
#define MEMP_STAT(env, gsp, fsp, flags, malloc) memp_stat((env), (gsp), (fsp))

#else	/* older than db 3.3 */
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
	memp_stat((env), (gsp), (fsp), (malloc))
#endif
#endif
/* 
 * The defult thread stacksize for nspr21 is 64k. For OSF, we require
 * a larger stacksize as actual storage allocation is higher i.e
 * pointers are allocated 8 bytes but lower 4 bytes are used.
 * The value 0 means use the default stacksize.
 */
#if defined (OSF1) || defined (__LP64__) || defined (_LP64) /* 64-bit architectures need bigger stacks */
#if defined(__hpux) && defined(__ia64)
#define DEFAULT_THREAD_STACKSIZE        524288L
#else
#define DEFAULT_THREAD_STACKSIZE 	131072L
#endif
#else
#define DEFAULT_THREAD_STACKSIZE 	0
#endif

#ifdef _WIN32
#define FILE_CREATE_MODE S_IREAD | S_IWRITE
#define DIR_CREATE_MODE  0755
#else /* _WIN32 */
#define FILE_CREATE_MODE S_IRUSR | S_IWUSR
#define DIR_CREATE_MODE  0755
#endif

#define NO_DISK_SPACE 1024
#define MIN_DISK_SPACE 10485760    /* 10 MB */

/***** Data Definitions *****/

/* possible changelog open modes */
typedef enum
{
	CL5_OPEN_NONE,				/* nothing specified */
	CL5_OPEN_NORMAL,			/* open for normal read/write use */
	CL5_OPEN_RESTORE_RECOVER,	/* restore from archive and recover */
	CL5_OPEN_RESTORE,			/* restore, but no recovery */
	CL5_OPEN_LDIF2CL,			/* open as part of ldif2cl: no locking,
								   recovery, checkpointing */
	CL5_OPEN_CLEAN_RECOVER		/* remove env after recover open (upgrade) */
} CL5OpenMode;

#define DB_FILE_DELETED 0x1
#define DB_FILE_INIT	0x2
/* this structure represents one changelog file, Each changelog file contains
   changes applied to a single backend. Files are named by the database id */
typedef struct cl5dbfile
{
	char *name;	            /* file name (with the extension) */
    char *replGen;          /* replica generation of the data */
    char *replName;         /* replica name                   */
	DB	 *db;				/* db handle to the changelog file*/
	int	 entryCount;		/* number of entries in the file  */
	int  flags;				/* currently used to mark the file as deleted 
							 * or as initialized */
    RUV  *purgeRUV;         /* ruv to which the file has been purged */
    RUV  *maxRUV;           /* ruv that marks the upper boundary of the data */
	char *semaName;			/* semaphore name */ 
	PRSem *sema;			/* semaphore for max concurrent cl writes */
}CL5DBFile;

/* structure that allows to iterate through entries to be sent to a consumer
   that originated on a particular supplier. */
struct cl5replayiterator
{
	Object		*fileObj;
	CLC_Buffer	*clcache;		/* changelog cache */
	ReplicaId	 consumerRID;	/* consumer's RID */
	const RUV	*consumerRuv;	/* consumer's update vector					*/
    Object      *supplierRuvObj;/* supplier's update vector object          */
};

typedef struct cl5iterator
{
	DBC		*cursor;	/* current position in the db file	*/
	Object	*file;		/* handle to release db file object	*/	
}CL5Iterator;

/* changelog trimming configuration */
typedef struct cl5trim
{
	time_t		maxAge;		/* maximum entry age in seconds							*/
	int			maxEntries;	/* maximum number of entries across all changelog files	*/
	PRLock*		lock;		/* controls access to trimming configuration			*/
} CL5Trim;

/* this structure defines 5.0 changelog internals */
typedef struct cl5desc
{
	char		*dbDir;		/* absolute path to changelog directory				*/								    
	DB_ENV		*dbEnv;		/* db environment shared by all db files			*/
	int			dbEnvOpenFlags;/* openflag used for env->open */
	Objset		*dbFiles;	/* ref counted set of changelog files (CL5DBFile)	*/
	PRLock		*fileLock;	/* ensures that changelog file is not added twice	*/
	CL5OpenMode	dbOpenMode;	/* how we open db									*/
	CL5DBConfig	dbConfig;	/* database configuration params					*/
	CL5Trim     dbTrim;		/* trimming parameters								*/
	CL5State	dbState;	/* changelog current state							*/
	Slapi_RWLock	*stLock;	/* lock that controls access to the changelog state	*/	
	PRBool      dbRmOnClose;/* indicates whether changelog should be removed when
							   it is closed	*/
	PRBool		fatalError; /* bad stuff happened like out of disk space; don't 
							   write guardian file on close - UnUsed so far */
	int			threadCount;/* threads that globally access changelog like 
							   deadlock detection, etc. */
	PRLock		*clLock;	/* Lock associated to clVar, used to notify threads on close */
	PRCondVar	*clCvar;	/* Condition Variable used to notify threads on close */
	void        *clcrypt_handle; /* for cl encryption */
} CL5Desc;

typedef void (*VFP)(void *);

/***** Global Variables *****/
static CL5Desc s_cl5Desc;

/***** Forward Declarations *****/

/* changelog initialization and cleanup */
static int _cl5Open (const char *dir, const CL5DBConfig *config, CL5OpenMode openMode);
static int _cl5AppInit (void);
static int _cl5DBOpen (void);
static void _cl5SetDefaultDBConfig ();
static void _cl5SetDBConfig (const CL5DBConfig *config);
static int _cl5CheckDBVersion ();
static int _cl5ReadDBVersion (const char *dir, char *clVersion, int buflen);
static int _cl5WriteDBVersion ();
static void _cl5Close ();
static int  _cl5Delete (const char *dir, PRBool rmDir);
static void _cl5DBClose ();

/* thread management */
static int _cl5DispatchDBThreads ();
static int _cl5AddThread ();
static void _cl5RemoveThread ();

/* functions that work with individual changelog files */
static int _cl5NewDBFile (const char *replName, const char *replGen, CL5DBFile** dbFile);
static int _cl5DBOpenFile (Object *replica, Object **obj, PRBool checkDups);
static int _cl5DBOpenFileByReplicaName (const char *replName, const char *replGen, 
                                        Object **obj, PRBool checkDups);
static void	_cl5DBCloseFile (void **data);
static void _cl5DBDeleteFile (Object *obj);
static void _cl5DBFileInitialized (Object *obj);
static int _cl5GetDBFile (Object *replica, Object **obj);
static int _cl5GetDBFileByReplicaName (const char *replName, const char *replGen, 
                                       Object **obj);
static int _cl5AddDBFile (CL5DBFile *file, Object **obj);
static int _cl5CompareDBFile (Object *el1, const void *el2);
static char* _cl5Replica2FileName (Object *replica);
static char* _cl5MakeFileName (const char *replName, const char *replGen);
static PRBool _cl5FileName2Replica (const char *fileName, Object **replica);
static int _cl5ExportFile (PRFileDesc *prFile, Object *obj);
static PRBool _cl5ReplicaInList (Object *replica, Object **replicas);

/* data storage and retrieval */
static int _cl5Entry2DBData (const CL5Entry *entry, char **data, PRUint32 *len);
static int _cl5WriteOperation(const char *replName, const char *replGen,
                              const slapi_operation_parameters *op, PRBool local);
static int _cl5WriteOperationTxn(const char *replName, const char *replGen,
                                 const slapi_operation_parameters *op, PRBool local, void *txn);
static int _cl5GetFirstEntry (Object *obj, CL5Entry *entry, void **iterator, DB_TXN *txnid);
static int _cl5GetNextEntry (CL5Entry *entry, void *iterator);
static int _cl5CurrentDeleteEntry (void *iterator);
static PRBool _cl5IsValidIterator (const CL5Iterator *iterator);
static int _cl5GetOperation (Object *replica, slapi_operation_parameters *op);
static const char* _cl5OperationType2Str (int type);
static int _cl5Str2OperationType (const char *str);
static void _cl5WriteString (const char *str, char **buff);
static void _cl5ReadString (char **str, char **buff);
static void _cl5WriteMods (LDAPMod **mods, char **buff);
static void _cl5WriteMod (LDAPMod *mod, char **buff);
static int _cl5ReadMods (LDAPMod ***mods, char **buff);
static int _cl5ReadMod (Slapi_Mod *mod, char **buff);
static int _cl5GetModsSize (LDAPMod **mods);
static int _cl5GetModSize (LDAPMod *mod);
static void _cl5ReadBerval (struct berval *bv, char** buff);
static void _cl5WriteBerval (struct berval *bv, char** buff);
static int _cl5ReadBervals (struct berval ***bv, char** buff, unsigned int size);
static int _cl5WriteBervals (struct berval **bv, char** buff, unsigned int *size);

/* replay iteration */
#ifdef FOR_DEBUGGING
static PRBool _cl5ValidReplayIterator (const CL5ReplayIterator *iterator);
#endif
static int _cl5PositionCursorForReplay (ReplicaId consumerRID, const RUV *consumerRuv,
			Object *replica, Object *fileObject, CL5ReplayIterator **iterator);
static int _cl5CheckMissingCSN (const CSN *minCsn, const RUV *supplierRUV, CL5DBFile *file);

/* changelog trimming */
static int _cl5TrimInit ();
static void _cl5TrimCleanup ();
static int _cl5TrimMain (void *param);
static void _cl5DoTrimming (ReplicaId rid);
static void _cl5TrimFile (Object *obj, long *numToTrim, ReplicaId cleaned_rid);
static PRBool _cl5CanTrim (time_t time, long *numToTrim);
static int  _cl5ReadRUV (const char *replGen, Object *obj, PRBool purge);
static int  _cl5WriteRUV (CL5DBFile *file, PRBool purge);
static int  _cl5ConstructRUV (const char *replGen, Object *obj, PRBool purge);
static int  _cl5UpdateRUV (Object *obj, CSN *csn, PRBool newReplica, PRBool purge);
static int  _cl5GetRUV2Purge2 (Object *fileObj, RUV **ruv);
void trigger_cl_trimming_thread(void *rid);

/* bakup/recovery, import/export */
static int _cl5LDIF2Operation (char *ldifEntry, slapi_operation_parameters *op,
                               char **replGen);
static int _cl5Operation2LDIF (const slapi_operation_parameters *op, const char *replGen,
                               char **ldifEntry, PRInt32 *lenLDIF);

/* entry count */
static int _cl5GetEntryCount (CL5DBFile *file);
static int _cl5WriteEntryCount (CL5DBFile *file);

/* misc */
static char*  _cl5GetHelperEntryKey (int type, char *csnStr);
static Object* _cl5GetReplica (const slapi_operation_parameters *op, const char* replGen);
static int _cl5FileEndsWith(const char *filename, const char *ext);

static PRLock *cl5_diskfull_lock = NULL;
static int cl5_diskfull_flag = 0;

static void cl5_set_diskfull();
static void cl5_set_no_diskfull();

/***** Module APIs *****/

/* Name:		cl5Init
   Description:	initializes changelog module; must be called by a single thread
				before any other changelog function.
   Parameters:  none
   Return:		CL5_SUCCESS if function is successful;
				CL5_SYSTEM_ERROR error if NSPR call fails.
 */
int cl5Init ()
{
	s_cl5Desc.stLock = slapi_new_rwlock();
	if (s_cl5Desc.stLock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"cl5Init: failed to create state lock; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}
    if ((s_cl5Desc.clLock = PR_NewLock()) == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
                        "cl5Init: failed to create on close lock; NSPR error - %d\n",
                        PR_GetError ());
        return CL5_SYSTEM_ERROR;

    }
	if ((s_cl5Desc.clCvar = PR_NewCondVar(s_cl5Desc.clLock)) == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"cl5Init: failed to create on close cvar; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}

	if (( clcache_init (&s_cl5Desc.dbEnv) != 0 )) {
		return CL5_SYSTEM_ERROR;
	}

	s_cl5Desc.dbState = CL5_STATE_CLOSED;
	s_cl5Desc.fatalError = PR_FALSE;
	s_cl5Desc.dbRmOnClose = PR_FALSE;
	s_cl5Desc.threadCount = 0;

	if (NULL == cl5_diskfull_lock)
	{
		cl5_diskfull_lock = PR_NewLock ();
	}

	return CL5_SUCCESS;
}

/* Name:		cl5Cleanup
   Description:	performs cleanup of the changelog module; must be called by a single
				thread; it closes changelog if it is still open.
   Parameters:  none
   Return:      none
 */
void cl5Cleanup ()
{
	/* close db if it is still open */
	if (s_cl5Desc.dbState == CL5_STATE_OPEN)
	{
		cl5Close ();
	}

	if (s_cl5Desc.stLock)
		slapi_destroy_rwlock (s_cl5Desc.stLock);
	s_cl5Desc.stLock = NULL;

	if (cl5_diskfull_lock)
	{
		PR_DestroyLock (cl5_diskfull_lock);
		cl5_diskfull_lock = NULL;
	}

	memset (&s_cl5Desc, 0, sizeof (s_cl5Desc));
}

/* Name:		cl5Open 
   Description:	opens changelog; must be called after changelog is
				initialized using cl5Init. It is thread safe and the second
				call is ignored.
   Parameters:  dir - changelog dir
				config - db configuration parameters; currently not used
   Return:		CL5_SUCCESS if successfull;
				CL5_BAD_DATA if invalid directory is passed;
				CL5_BAD_STATE if changelog is not initialized;
				CL5_BAD_DBVERSION if dbversion file is missing or has unexpected data
				CL5_SYSTEM_ERROR if NSPR error occured (during db directory creation);
				CL5_MEMORY_ERROR if memory allocation fails;
				CL5_DB_ERROR if db initialization fails.
 */
int cl5Open (const char *dir, const CL5DBConfig *config)
{
	int rc;

	if (dir == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "cl5Open: null directory\n");
		return CL5_BAD_DATA;	
	}

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5Open: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	/* prevent state from changing */
	slapi_rwlock_wrlock (s_cl5Desc.stLock);

	/* already open - ignore */
	if (s_cl5Desc.dbState == CL5_STATE_OPEN)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
					"cl5Open: changelog already opened; request ignored\n");
		rc = CL5_SUCCESS;
		goto done;
	}
	else if (s_cl5Desc.dbState != CL5_STATE_CLOSED)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5Open: invalid state - %d\n", s_cl5Desc.dbState);
		rc = CL5_BAD_STATE;
		goto done;
	}

	rc = _cl5Open (dir, config, CL5_OPEN_NORMAL);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5Open: failed to open changelog\n");
		goto done;
	}

	/* dispatch global threads like deadlock detection, trimming, etc */
	rc = _cl5DispatchDBThreads ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5Open: failed to start database monitoring threads\n");
		
		_cl5Close ();
	}
	else
	{
		s_cl5Desc.dbState = CL5_STATE_OPEN;	
		clcache_set_config();

		/* Set the cl encryption algorithm (if configured) */
		rc = clcrypt_init(config, &s_cl5Desc.clcrypt_handle);
	}

done:
	slapi_rwlock_unlock (s_cl5Desc.stLock);

	return rc;
}

/* Name:		cl5Close
   Description:	closes changelog; waits until all threads are done using changelog;
				call is ignored if changelog is already closed.
   Parameters:  none
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if db is not in the open or closed state;
				CL5_SYSTEM_ERROR if NSPR call fails;
				CL5_DB_ERROR if db shutdown fails
 */
int cl5Close ()
{
	int rc = CL5_SUCCESS;

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5Close: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	slapi_rwlock_wrlock (s_cl5Desc.stLock);

	/* already closed - ignore */
	if (s_cl5Desc.dbState == CL5_STATE_CLOSED)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
					"cl5Close: changelog closed; request ignored\n");
		slapi_rwlock_unlock (s_cl5Desc.stLock);
		return CL5_SUCCESS;
	}
	else if (s_cl5Desc.dbState != CL5_STATE_OPEN)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5Close: invalid state - %d\n", s_cl5Desc.dbState);
		slapi_rwlock_unlock (s_cl5Desc.stLock);
		return CL5_BAD_STATE;
	}

	/* signal changelog closing to all threads */
	s_cl5Desc.dbState = CL5_STATE_CLOSING;

	PR_Lock(s_cl5Desc.clLock);
	PR_NotifyCondVar(s_cl5Desc.clCvar);
	PR_Unlock(s_cl5Desc.clLock);

	_cl5Close ();

	s_cl5Desc.dbState = CL5_STATE_CLOSED;

	slapi_rwlock_unlock (s_cl5Desc.stLock);

	return rc;
}

/* Name:		cl5Delete
   Description:	removes changelog; changelog must be in the closed state.
   Parameters:  dir - changelog directory
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if the changelog is not in closed state;
				CL5_BAD_DATA if invalid directory supplied
				CL5_SYSTEM_ERROR if NSPR call fails
 */
int cl5Delete (const char *dir)
{	
	int   rc;

	if (dir == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, "cl5Delete: null directory\n");
		return CL5_BAD_DATA;
	}

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5Delete: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	slapi_rwlock_wrlock (s_cl5Desc.stLock);

	if (s_cl5Desc.dbState != CL5_STATE_CLOSED)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5Delete: invalid state - %d\n", s_cl5Desc.dbState);
		slapi_rwlock_unlock (s_cl5Desc.stLock);
		return CL5_BAD_STATE;
	}

	rc = _cl5Delete (dir, PR_TRUE /* remove changelog dir */);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5Delete: failed to remove changelog\n");
	}
	 
	slapi_rwlock_unlock (s_cl5Desc.stLock);
	return rc;
} 

/* Name:        cl5DeleteDBSync
   Description: The same as cl5DeleteDB except the function does not return
                until the file is removed.
*/
int cl5DeleteDBSync (Object *replica)
{
    Object *obj;
	int rc;
    CL5DBFile *file;

	if (replica == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5DeleteDBSync: invalid database id\n");
		return CL5_BAD_DATA;
	}

	/* changelog is not initialized */
	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5DeleteDBSync: "
					    "changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	/* make sure that changelog stays open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;

	rc = _cl5GetDBFile (replica, &obj);
	if (rc == CL5_SUCCESS)
	{
        char *filename = NULL;
        file = (CL5DBFile*)object_get_data (obj);
        PR_ASSERT (file);
        /* file->name is freed in _cl5DBDeleteFile */
        filename = slapi_ch_strdup(file->name);

        _cl5DBDeleteFile (obj);

        /* wait until the file is gone */
        while (PR_Access (filename, PR_ACCESS_EXISTS) == PR_SUCCESS)
        {
            DS_Sleep (PR_MillisecondsToInterval(100));
        }
        slapi_ch_free_string(&filename);
    }
    else
    {
        Replica *r = (Replica*)object_get_data (replica);
        PR_ASSERT (r);
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5DeleteDBSync: "
					    "file for replica at (%s) not found\n",
                        slapi_sdn_get_dn (replica_get_root (r)));
    }

	_cl5RemoveThread ();
	return rc;
}

/* Name:        cl5GetUpperBoundRUV
   Description: retrieves vector for that represents the upper bound of the changes for a replica.
   Parameters:  r - replica for which the purge vector is requested
                ruv - contains a copy of the purge ruv if function is successful; 
                unchanged otherwise. It is responsibility of the caller to free
                the ruv when it is no longer is in use
   Return:      CL5_SUCCESS if function is successful
                CL5_BAD_STATE if the changelog is not initialized;
				CL5_BAD_DATA - if NULL id is supplied
                CL5_NOTFOUND, if changelog file for replica is not found
 */
int cl5GetUpperBoundRUV (Replica *r, RUV **ruv)
{
    int rc;
    Object *r_obj, *file_obj;
    CL5DBFile *file;

    if (r == NULL || ruv == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5GetUpperBoundRUV: invalid parameters\n");
		return CL5_BAD_DATA;
	}

	/* changelog is not initialized */
	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5GetUpperBoundRUV: "
					    "changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	/* make sure that changelog stays open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;

    /* create a temporary replica object because of the interface we have */
    r_obj = object_new (r, NULL);

	rc = _cl5GetDBFile (r_obj, &file_obj);
	if (rc == CL5_SUCCESS)
	{
        file = (CL5DBFile*)object_get_data (file_obj);
        PR_ASSERT (file && file->maxRUV);

        *ruv = ruv_dup (file->maxRUV);

        object_release (file_obj);
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5GetUpperBoundRUV: "
					    "could not find DB object for replica\n");
	}

    object_release (r_obj);
    
	_cl5RemoveThread ();
	return rc;
}


/* Name:		cl5ExportLDIF
   Description:	dumps changelog to an LDIF file; changelog can be open or closed.
   Parameters:  clDir - changelog dir
				ldifFile - full path to ldif file to write
				replicas - optional list of replicas whose changes should be exported;
						   if the list is NULL, entire changelog is exported.
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE if changelog is not initialized;
				CL5_DB_ERROR if db api fails;
				CL5_SYSTEM_ERROR if NSPR call fails;
				CL5_MEMORY_ERROR if memory allocation fials.
 */
int cl5ExportLDIF (const char *ldifFile, Object **replicas)
{
	int i;
	int rc;
	PRFileDesc *prFile = NULL;
    Object *obj;

	if (ldifFile == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ExportLDIF: null ldif file name\n");
		return CL5_BAD_DATA;
	}

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ExportLDIF: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;

	prFile = PR_Open (ldifFile, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0600);
	if (prFile == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ExportLDIF: failed to open (%s) file; NSPR error - %d\n",
						ldifFile, PR_GetError ());
		rc = CL5_SYSTEM_ERROR;
		goto done;
	}

	slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
					"cl5ExportLDIF: starting changelog export to (%s) ...\n", ldifFile);

	if (replicas)	/* export only selected files */
	{
		for (i = 0; replicas[i]; i++)
		{
            rc = _cl5GetDBFile (replicas[i], &obj);
            if (rc == CL5_SUCCESS)
            {
			    rc = _cl5ExportFile (prFile, obj);
                object_release (obj);            
			}
            else
            {
                Replica *r = (Replica*)object_get_data (replicas[i]);

                PR_ASSERT (r);

                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "cl5ExportLDIF: "
                                "failed to locate changelog file for replica at (%s)\n",
                                slapi_sdn_get_dn (replica_get_root (r)));
            }
		}
	}
	else /* export all files */
	{
		for (obj = objset_first_obj(s_cl5Desc.dbFiles); obj; 
			 obj = objset_next_obj(s_cl5Desc.dbFiles, obj))
		{
			rc = _cl5ExportFile (prFile, obj);
		}
	}

    rc = CL5_SUCCESS;
done:;

	_cl5RemoveThread ();

    if (rc == CL5_SUCCESS)
	    slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
				    "cl5ExportLDIF: changelog export is finished.\n");

	if (prFile)
		PR_Close (prFile);
	
    return rc;
}

/* Name:		cl5ImportLDIF
   Description:	imports ldif file into changelog; changelog must be in the closed state
   Parameters:  clDir - changelog dir
				ldifFile - absolute path to the ldif file to import
				replicas - list of replicas whose data should be imported.
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE if changelog is open or not inititalized;
				CL5_DB_ERROR if db api fails;
				CL5_SYSTEM_ERROR if NSPR call fails;
				CL5_MEMORY_ERROR if memory allocation fials.
 */
int
cl5ImportLDIF (const char *clDir, const char *ldifFile, Object **replicas)
{
#if defined(USE_OPENLDAP)
	LDIFFP *file = NULL;
	int buflen;
	ldif_record_lineno_t lineno = 0;
#else
	FILE *file = NULL;
	int lineno = 0;
#endif
	int rc;
	char *buff = NULL;
	slapi_operation_parameters op;
	Object *prim_replica_obj = NULL;
	Object *replica_obj = NULL;
	Object *file_obj = NULL;
	Replica *prim_replica = NULL;
	char *replGen = NULL;
	CL5DBFile *dbfile = NULL;
	struct berval **purgevals = NULL;
	struct berval **maxvals = NULL;
	int purgeidx = 0;
	int maxidx = 0;
	int maxpurgesz = 0;
	int maxmaxsz = 0;
	int entryCount = 0;

	/* validate params */
	if (ldifFile == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: null ldif file name\n");
		return CL5_BAD_DATA;
	}

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	if (replicas == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"cl5ImportLDIF: null list of replicas\n");
		return CL5_BAD_DATA;
	}

	prim_replica_obj = replicas[0];
	if (NULL == prim_replica_obj)
	{
		/* Never happens for now. (see replica_execute_ldif2cl_task) */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: empty replica list\n");
		return CL5_BAD_DATA;
	}
	prim_replica = (Replica*)object_get_data(prim_replica_obj);

	/* make sure that nobody change changelog state while import is in progress */
	slapi_rwlock_wrlock (s_cl5Desc.stLock);

	/* make sure changelog is closed */
	if (s_cl5Desc.dbState != CL5_STATE_CLOSED)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: invalid state - %d \n", s_cl5Desc.dbState);

		slapi_rwlock_unlock (s_cl5Desc.stLock);
		return CL5_BAD_STATE;
	}
	
	/* open LDIF file */
#if defined(USE_OPENLDAP)
	file = ldif_open (ldifFile, "r");
#else
	file = fopen (ldifFile, "r"); /* XXXggood Does fopen reliably work if > 255 files open? */
#endif
	if (file == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: failed to open (%s) ldif file; system error - %d\n",
						ldifFile, errno);
		rc = CL5_SYSTEM_ERROR;
		goto done;
	}

	/* remove changelog */
	rc = _cl5Delete (clDir, PR_FALSE);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: failed to remove changelog\n");
		goto done;
	}

	/* open changelog */
	rc = _cl5Open (clDir, NULL, CL5_OPEN_LDIF2CL);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ImportLDIF: failed to open changelog\n");
		goto done;
	}
	s_cl5Desc.dbState = CL5_STATE_OPEN; /* force to change the state */
	
	/* read entries and write them to changelog */
#if defined(USE_OPENLDAP)
	while (ldif_read_record( file, &lineno, &buff, &buflen ))
#else
	while ((buff = ldif_get_entry( file, &lineno )) != NULL)
#endif
	{
		rc = _cl5LDIF2Operation (buff, &op, &replGen);
		if (rc != CL5_SUCCESS)
		{
            /*
             * clpurgeruv: {replicageneration} 4d13a124000000010000
             * clpurgeruv: {replica 2 ldap://host:port} 
             * clpurgeruv: {replica 1 ldap://host:port} 
             * clmaxruv: {replicageneration} 4d13a124000000010000
             * clmaxruv: {replica 2} <mincsn> <maxcsn> <timestamp>
             * clmaxruv: {replica 1} <mincsn> <maxcsn> <timestamp>
             */
            char *line;
            char *next = buff;
            struct berval type, value;
            int freeval = 0;

            while ((line = ldif_getline(&next)) != NULL) {
                rc = slapi_ldif_parse_line(line, &type, &value, &freeval);
                /* ruv_dump (dbfile->purgeRUV, "clpurgeruv", prFile); */
                if (0 == strcasecmp (type.bv_val, "clpurgeruv")) {
                    if (maxpurgesz < purgeidx + 2) {
                        if (!maxpurgesz) {
                            maxpurgesz = 4 * (purgeidx + 2);
                        } else {
                            maxpurgesz *= 2;
                        }
                        purgevals = (struct berval **)slapi_ch_realloc(
                                          (char *)purgevals,
                                          sizeof(struct berval *) * maxpurgesz);
                    }
                    purgevals[purgeidx++] = slapi_ch_bvdup(&value);
                    purgevals[purgeidx] = NULL; /* make sure NULL terminated */
                }
                /* ruv_dump (dbfile->maxRUV, "clmaxruv", prFile); */
                else if (0 == strcasecmp (type.bv_val, "clmaxruv")) {
                    if (maxmaxsz < maxidx + 2) {
                        if (!maxmaxsz) {
                            maxmaxsz = 4 * (maxidx + 2);
                        } else {
                            maxmaxsz *= 2;
                        }
                        maxvals = (struct berval **)slapi_ch_realloc(
                                          (char *)maxvals,
                                          sizeof(struct berval *) * maxmaxsz);
                    }
                    /* {replica #} min_csn csn [last_modified] */
                    /* get rid of last_modified, if any */
                    maxvals[maxidx++] = slapi_ch_bvdup(&value);
                    maxvals[maxidx] = NULL; /* make sure NULL terminated */
                }
                if (freeval) {
                    slapi_ch_free_string(&value.bv_val);
                }
            }
            slapi_ch_free_string(&buff);
#if defined(USE_OPENLDAP)
            buflen = 0;
#endif
            goto next;
        }
        slapi_ch_free_string(&buff);
#if defined(USE_OPENLDAP)
        buflen = 0;
#endif
        /* if we perform selective import, check if the operation should be wriiten to changelog */
        replica_obj = _cl5GetReplica (&op, replGen);
        if (replica_obj == NULL)
        {
            /* 
             * changetype: delete
             * replgen: 4d13a124000000010000
             * csn: 4d23b909000000020000
             * nsuniqueid: 00000000-00000000-00000000-00000000
             * dn: cn=start iteration
             */
            rc = _cl5WriteOperation (replica_get_name(prim_replica), 
                                     replGen, &op, 1);
            if (rc != CL5_SUCCESS)
            {
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                        "cl5ImportLDIF: "
                        "failed to write operation to the changelog: "
                        "type: %lu, dn: %s\n",
                        op.operation_type, REPL_GET_DN(&op.target_address));
                slapi_ch_free_string(&replGen);
                operation_parameters_done (&op);
                goto done;
            }
            entryCount++;
            goto next;
        }

		if (!replicas || _cl5ReplicaInList (replica_obj, replicas))
		{
			/* write operation creates the file if it does not exist */
			rc = _cl5WriteOperation (replica_get_name ((Replica*)object_get_data(replica_obj)), 
                                     replGen, &op, 1);
			if (rc != CL5_SUCCESS)
			{
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                        "cl5ImportLDIF: "
                        "failed to write operation to the changelog: "
                        "type: %lu, dn: %s\n",
                        op.operation_type, REPL_GET_DN(&op.target_address));
                object_release (replica_obj);
                slapi_ch_free_string(&replGen);
				operation_parameters_done (&op);
				goto done;
			}
            entryCount++;
		}
next:
        if (replica_obj) {
            object_release (replica_obj);
        }
        slapi_ch_free_string(&replGen);
        operation_parameters_done (&op);
	}

    /* Set RUVs and entry count */
    file_obj = objset_first_obj(s_cl5Desc.dbFiles);
    while (file_obj)
    {
        dbfile = (CL5DBFile *)object_get_data(file_obj);
        if (0 == strcasecmp(dbfile->replName, replica_get_name(prim_replica))) {
            break;
        }
        dbfile = NULL;
        file_obj = objset_next_obj(s_cl5Desc.dbFiles, file_obj);
    }

    if (dbfile) {
        if (purgeidx > 0) {
            ruv_destroy (&dbfile->purgeRUV);
            rc = ruv_init_from_bervals(purgevals, &dbfile->purgeRUV);
        }
        if (maxidx > 0) {
            ruv_destroy (&dbfile->maxRUV);
            rc = ruv_init_from_bervals(maxvals, &dbfile->maxRUV);
        }

        dbfile->entryCount = entryCount;
    }
    if (file_obj) {
        object_release (file_obj);
    }

done:
    for (purgeidx = 0; purgevals && purgevals[purgeidx]; purgeidx++) {
        slapi_ch_bvfree(&purgevals[purgeidx]);
    }
    slapi_ch_free((void **)&purgevals);
    for (maxidx = 0; maxvals && maxvals[maxidx]; maxidx++) {
        slapi_ch_bvfree(&maxvals[maxidx]);
    }
    slapi_ch_free((void **)&maxvals);

	if (file) {
#if defined(USE_OPENLDAP)
		ldif_close(file);
#else
		fclose(file);
#endif
	}
	if (CL5_STATE_OPEN == s_cl5Desc.dbState) {
		_cl5Close ();
		s_cl5Desc.dbState = CL5_STATE_CLOSED; /* force to change the state */
	}
	slapi_rwlock_unlock (s_cl5Desc.stLock);
    return rc;
}

/* Name:		cl5GetState
   Description:	returns database state
   Parameters:  none
   Return:		changelog state
 */
int cl5GetState ()
{
	return s_cl5Desc.dbState;
}

/* Name:		cl5ConfigTrimming
   Description:	sets changelog trimming parameters; changelog must be open.
   Parameters:  maxEntries - maximum number of entries in the chnagelog (in all files);
				maxAge - maximum entry age;
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if changelog is not open
 */
int cl5ConfigTrimming (int maxEntries, const char *maxAge)
{
	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ConfigTrimming: changelog is not initialized\n");
		return CL5_BAD_STATE;	
	}

	/* make sure changelog is not closed while trimming configuration
       is updated.*/
	if (CL5_SUCCESS != _cl5AddThread ()) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5ConfigTrimming: could not start changelog trimming thread\n");
		return CL5_BAD_STATE;	
	}

	PR_Lock (s_cl5Desc.dbTrim.lock);
	
	if (maxAge)
	{
		/* don't ignore this argument */
		if (strcmp (maxAge, CL5_STR_IGNORE) != 0)
		{
			s_cl5Desc.dbTrim.maxAge = age_str2time (maxAge);
		}
	}
	else
	{
		/* unlimited */
		s_cl5Desc.dbTrim.maxAge = 0;
	}

	if (maxEntries != CL5_NUM_IGNORE)
	{
		s_cl5Desc.dbTrim.maxEntries = maxEntries;	
	}

	PR_Unlock (s_cl5Desc.dbTrim.lock);
	
	_cl5RemoveThread ();

	return CL5_SUCCESS;	
}

/* Name:		cl5GetOperation
   Description:	retireves operation specified by its csn and databaseid
   Parameters:  op - must contain csn and databaseid; the rest of data is
				filled if function is successfull
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid op is passed;
				CL5_BAD_STATE if db has not been initialized;
				CL5_NOTFOUND if entry was not found;
				CL5_DB_ERROR if any other db error occured;
				CL5_BADFORMAT if db data format does not match entry format.
 */
int cl5GetOperation (Object *replica, slapi_operation_parameters *op)
{
	int rc;
	char *agmt_name;

	agmt_name = get_thread_private_agmtname();

	if (replica == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5GetOperation: NULL replica\n");
		return CL5_BAD_DATA;
	}

    if (op == NULL)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "cl5GetOperation: NULL operation\n");
		return CL5_BAD_DATA;
    }

    if (op->csn == NULL)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "%s: cl5GetOperation: operation contains no CSN\n", agmt_name);
		return CL5_BAD_DATA;
    }

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"%s: cl5GetOperation: changelog is not initialized\n", agmt_name);
		return CL5_BAD_STATE;	
	}	

	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;

	rc = _cl5GetOperation (replica, op);

	_cl5RemoveThread ();

	return rc;
}

/* Name:		cl5GetFirstOperation
   Description: retrieves first operation for a particular database
                replica - replica for which the operation should be retrieved.
   Parameters:  op - buffer to store the operation;
				iterator - to be passed to the call to cl5GetNextOperation
   Return:		CL5_SUCCESS, if successful
				CL5_BADDATA, if operation is NULL
				CL5_BAD_STATE, if changelog is not open
				CL5_DB_ERROR, if db call fails
 */
int cl5GetFirstOperation (Object *replica, slapi_operation_parameters *op, void **iterator)
{
	int rc;
	CL5Entry entry;
    Object *obj;
	char *agmt_name;

	if (replica == NULL || op == NULL || iterator == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5GetFirstOperation: invalid argument\n");
		return CL5_BAD_DATA;
	}

	*iterator = NULL;

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		agmt_name = get_thread_private_agmtname();
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"%s: cl5GetFirstOperation: changelog is not initialized\n", agmt_name);
		return CL5_BAD_STATE;
	}

	/* make sure that changelog stays open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;
    
    rc = _cl5GetDBFile (replica, &obj);
	if (rc != CL5_SUCCESS)
	{
        _cl5RemoveThread ();
		return rc;
	}

	entry.op = op;
	/* Callers of this function should cl5_operation_parameters_done(op) */
	rc = _cl5GetFirstEntry (obj, &entry, iterator, NULL);
    object_release (obj);

	_cl5RemoveThread ();

	return rc;
}

/* Name:		cl5GetNextOperation
   Description: retrieves the next op from the changelog as defined by the iterator;
				changelog must be open.
   Parameters:  op - returned operation, if function is successful
				iterator - in: identifies op to retrieve; out: identifies next op
   Return:		CL5_SUCCESS, if successful
				CL5_BADDATA, if op is NULL
				CL5_BAD_STATE, if changelog is not open
				CL5_NOTFOUND, empty changelog
				CL5_DB_ERROR, if db call fails
 */
int cl5GetNextOperation (slapi_operation_parameters *op, void *iterator)
{
	CL5Entry entry;

	if (op == NULL || iterator == NULL || !_cl5IsValidIterator (iterator))
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5GetNextOperation: invalid argument\n");
		return CL5_BAD_DATA;
	}

	if (s_cl5Desc.dbState != CL5_STATE_OPEN)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5GetNextOperation: changelog is not open\n");
		return CL5_BAD_STATE;
	}

	/* we don't need to increment thread count since cl5GetFirstOperation
       locked the file through which we are iterating */
	entry.op = op;
	/* Callers of this function should cl5_operation_parameters_done(op) */
	return _cl5GetNextEntry (&entry, iterator);	
}

/* Name:		cl5DestroyIterator
   Description: destroys iterator once iteration through changelog is done
   Parameters:  iterator - iterator to destroy
   Return:		none
 */
void cl5DestroyIterator (void *iterator)
{
	CL5Iterator *it = (CL5Iterator*)iterator;

	if (it == NULL)
		return;

	/* close cursor */
	if (it->cursor)
		it->cursor->c_close (it->cursor);

	if (it->file)
		object_release (it->file);

	slapi_ch_free ((void**)&it);
}

/* Name:		cl5WriteOperationTxn
   Description:	writes operation to changelog
   Parameters:  replName - name of the replica to which operation applies
                replGen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
				local - this is a non-replicated operation
                txn - the transaction containing this operation
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid op is passed;
				CL5_BAD_STATE if db has not been initialized;
				CL5_MEMORY_ERROR if memory allocation failed;
				CL5_DB_ERROR if any other db error occured;
 */
int cl5WriteOperationTxn(const char *replName, const char *replGen,
                         const slapi_operation_parameters *op, PRBool local, void *txn)
{
	int rc;

	if (op == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5WriteOperation: NULL operation passed\n");
		return CL5_BAD_DATA;
	}

    if (!IsValidOperation (op))
    {
		return CL5_BAD_DATA;
	}
    

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5WriteOperation: changelog is not initialized\n");
		return CL5_BAD_STATE;
	} 

	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return rc;

	rc = _cl5WriteOperationTxn(replName, replGen, op, local, txn);

    /* update the upper bound ruv vector */
    if (rc == CL5_SUCCESS)
    {
        Object *file_obj = NULL;

        if ( _cl5GetDBFileByReplicaName (replName, replGen, &file_obj) == CL5_SUCCESS) {
			rc = _cl5UpdateRUV (file_obj, op->csn, PR_FALSE, PR_FALSE);
			object_release (file_obj);
		}
		
    }

	_cl5RemoveThread ();
	
	return rc;	
}

/* Name:		cl5WriteOperation
   Description:	writes operation to changelog
   Parameters:  replName - name of the replica to which operation applies
                replGen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
				local - this is a non-replicated operation
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid op is passed;
				CL5_BAD_STATE if db has not been initialized;
				CL5_MEMORY_ERROR if memory allocation failed;
				CL5_DB_ERROR if any other db error occured;
 */
int cl5WriteOperation(const char *replName, const char *replGen,
                      const slapi_operation_parameters *op, PRBool local)
{
    return cl5WriteOperationTxn(replName, replGen, op, local, NULL);
}

/* Name:		cl5CreateReplayIterator
   Description:	creates an iterator that allows to retrieve changes that should
				to be sent to the consumer identified by ruv. The iteration is performed by
                repeated calls to cl5GetNextOperationToReplay.
   Parameters:  replica - replica whose data we wish to iterate;
				ruv - consumer ruv;
				iterator - iterator to be passed to cl5GetNextOperationToReplay call
   Return:		CL5_SUCCESS, if function is successful;
                CL5_MISSING_DATA, if data that should be in the changelog is missing
                CL5_PURGED_DATA, if some data that consumer needs has been purged.
                Note that the iterator can be non null if the supplier contains
                some data that needs to be sent to the consumer
                CL5_NOTFOUND if the consumer is up to data with respect to the supplier
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE  if db has not been open;
				CL5_DB_ERROR if any other db error occurred;
				CL5_MEMORY_ERROR if memory allocation fails.
   Algorithm:   Build a list of csns from consumer's and supplier's ruv. For each element
                of the consumer's ruv put max csn into the csn list. For each element
                of the supplier's ruv not in the consumer's ruv put min csn from the
                supplier's ruv into the list. The list contains, for each known replica,
                the starting point for changes to be sent to the consumer.
                Sort the list in ascending order.
                Build a hash which contains, for each known replica, whether the
                supplier can bring the consumer up to data with respect to that replica.
                The hash is used to decide whether a change can be sent to the consumer 
                Find the replica with the smallest csn in the list for which
                we can bring the consumer up to date.
                Position the db cursor on the change entry that corresponds to this csn.
                Hash entries are created for each replica traversed so far. sendChanges
                flag is set to FALSE for all replicas except the last traversed.
                
 */
int cl5CreateReplayIteratorEx (Private_Repl_Protocol *prp, const RUV *consumerRuv, 
                             CL5ReplayIterator **iterator,	ReplicaId consumerRID )
{
	int rc;
	Object *replica;
	Object *obj = NULL;

	replica = prp->replica_object;
	if (replica == NULL || consumerRuv == NULL || iterator == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIteratorEx: invalid parameter\n");
		return CL5_BAD_DATA;
	}

    *iterator = NULL;

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIteratorEx: changelog is not initialized\n");
		return CL5_BAD_STATE;
	}

	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS ) return rc;
	

	rc = _cl5GetDBFile (replica, &obj);
	if (rc == CL5_SUCCESS && obj)
	{
    	/* iterate through the ruv in csn order to find first master for which 
	       we can replay changes */		    
		
		rc = _cl5PositionCursorForReplay (consumerRID, consumerRuv, replica, obj, iterator);
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIteratorEx: could not find DB object for replica\n");
	}

	if (rc != CL5_SUCCESS)
	{
		if (obj) object_release (obj);

		/* release the thread */
		_cl5RemoveThread ();
	}

	return rc;	
}

/* cl5CreateReplayIterator is now a wrapper for cl5CreateReplayIteratorEx */
int cl5CreateReplayIterator (Private_Repl_Protocol *prp, const RUV *consumerRuv, 
                             CL5ReplayIterator **iterator)
{

/*	DBDB : I thought it should be possible to refactor this like so, but it seems to not work.
	Possibly the ordering of the calls is significant.
	ReplicaId consumerRID = agmt_get_consumer_rid ( prp->agmt, prp->conn );
	return cl5CreateReplayIteratorEx(prp,consumerRuv,iterator,consumerRID); */
	
	int rc;
	Object *replica;
	Object *obj = NULL;

	replica = prp->replica_object;
	if (replica == NULL || consumerRuv == NULL || iterator == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIterator: invalid parameter\n");
		return CL5_BAD_DATA;
	}

    *iterator = NULL;

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIterator: changelog is not initialized\n");
		return CL5_BAD_STATE;
	}

	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS ) return rc;
	

	rc = _cl5GetDBFile (replica, &obj);
	if (rc == CL5_SUCCESS && obj)
	{
    	/* iterate through the ruv in csn order to find first master for which 
	       we can replay changes */		    
		ReplicaId consumerRID = agmt_get_consumer_rid ( prp->agmt, prp->conn );
		rc = _cl5PositionCursorForReplay (consumerRID, consumerRuv, replica, obj, iterator);
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5CreateReplayIterator: could not find DB object for replica\n");
	}

	if (rc != CL5_SUCCESS)
	{
		if (obj) object_release (obj);

		/* release the thread */
		_cl5RemoveThread ();
	}

	return rc;	

}

/* Name:		cl5GetNextOperationToReplay
   Description:	retrieves next operation to be sent to a particular consumer and
				that was created on a particular master. Consumer and master info
				is encoded in the iterator parameter that must be created by call
				to cl5CreateReplayIterator.
   Parameters:  iterator - iterator that identifies next entry to retrieve;
				op - operation retrieved if function is successful
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_NOTFOUND if end of iteration list is reached
				CL5_DB_ERROR if any other db error occured;
				CL5_BADFORMAT if data in db is of unrecognized format;
				CL5_MEMORY_ERROR if memory allocation fails.
    Algorithm:  Iterate through changelog entries until a change is found that
                originated at the replica for which we are sending changes
                (based on the information in the iteration hash) and
                whose csn is larger than the csn already seen by the consumer
                If change originated at the replica not in the hash,
                determine whether we should send changes originated at the replica
                and add replica entry into the hash. We can send the changes for
                the replica if the current csn is smaller or equal to the csn
                in the consumer's ruv (if present) or if it is equal to the min
                csn in the supplier's ruv.
 */
int
cl5GetNextOperationToReplay (CL5ReplayIterator *iterator, CL5Entry *entry)
{
	CSN *csn;
	char *key, *data;
	size_t keylen, datalen;
	char *agmt_name;
	int rc = 0;

	agmt_name = get_thread_private_agmtname();

	if (entry == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"%s: cl5GetNextOperationToReplay: invalid parameter passed\n", agmt_name);
		return CL5_BAD_DATA;
	}

	rc = clcache_get_next_change (iterator->clcache, (void **)&key, &keylen, (void **)&data, &datalen, &csn);

	if (rc == DB_NOTFOUND)
	{
		/* 
		 * Abort means we've figured out that we've passed the replica Min CSN,
		 * so we should stop looping through the changelog 
		 */
		return CL5_NOTFOUND;
	}
	
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, "%s: cl5GetNextOperationToReplay: "
                   "failed to read next entry; DB error %d\n", agmt_name, rc);
		return CL5_DB_ERROR;
	}

	if(is_cleaned_rid(csn_get_replicaid(csn))){
		/*
		 *  This operation is from a deleted replica.  During the cleanallruv task the
		 *  replicas are cleaned first before this instance is.  This can cause the
		 *  server to basically do a full update over and over.  So we have to watch for
		 *  this, and not send these operations out.
		 */
		return CL5_IGNORE_OP;
	}

	/* there is an entry we should return */
	/* Callers of this function should cl5_operation_parameters_done(op) */
	if ( 0 != cl5DBData2Entry ( data, datalen, entry ) )
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"%s: cl5GetNextOperationToReplay: failed to format entry rc=%d\n", agmt_name, rc);
		return rc;
	}

	return CL5_SUCCESS;	
}

/* Name:		cl5DestroyReplayIterator
   Description:	destorys iterator
   Parameters:  iterator - iterator to destory
   Return:		none
 */
void cl5DestroyReplayIterator (CL5ReplayIterator **iterator)
{	
	if (iterator == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5DestroyReplayIterator: invalid iterartor passed\n");
		return;
	}

	clcache_return_buffer ( &(*iterator)->clcache );

	if ((*iterator)->fileObj) {
		object_release ((*iterator)->fileObj);
		(*iterator)->fileObj = NULL;
	}

    /* release supplier's ruv */
    if ((*iterator)->supplierRuvObj) {
        object_release ((*iterator)->supplierRuvObj);
        (*iterator)->supplierRuvObj = NULL;
	}

	slapi_ch_free ((void **)iterator);

	/* this thread no longer holds a db reference, release it */
	_cl5RemoveThread();
}

/* Name:		cl5DeleteOnClose
   Description:	marks changelog for deletion when it is closed
   Parameters:  flag; if flag = 1 then delete else don't
   Return:		none
 */
void cl5DeleteOnClose (PRBool rm)
{
	s_cl5Desc.dbRmOnClose = rm;
}

/* Name:		cl5GetDir
   Description:	returns changelog directory
   Parameters:  none
   Return:		copy of the directory; caller needs to free the string
 */
 char *cl5GetDir ()
{
	if (s_cl5Desc.dbDir == NULL)
	{
		return NULL;
	}
	else
	{
		return slapi_ch_strdup (s_cl5Desc.dbDir);
	}
}

/* Name: cl5Exist
   Description: checks if a changelog exists in the specified directory;	
				We consider changelog to exist if it contains the dbversion file.
   Parameters: clDir - directory to check
   Return: 1 - if changelog exists; 0 - otherwise
 */
PRBool cl5Exist (const char *clDir)
{
	char fName [MAXPATHLEN + 1];
	int rc;

	PR_snprintf (fName, MAXPATHLEN, "%s/%s", clDir, VERSION_FILE);
	rc = PR_Access (fName, PR_ACCESS_EXISTS);

	return (rc == PR_SUCCESS);
}

/* Name: cl5GetOperationCount
   Description: returns number of entries in the changelog. The changelog must be
				open for the value to be meaningful.
   Parameters:  replica - optional parameter that specifies the replica whose operations
                we wish to count; if NULL all changelog entries are counted
   Return:		number of entries in the changelog
 */

int cl5GetOperationCount (Object *replica)
{
	Object *obj;
	CL5DBFile *file; 
	int count = 0;
	int rc;

	if (s_cl5Desc.dbState == CL5_STATE_NONE)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"cl5GetOperationCount: changelog is not initialized\n");
		return -1;
	}	
	
	/* make sure that changelog is open while operation is in progress */
	rc = _cl5AddThread ();
	if (rc != CL5_SUCCESS)
		return -1;

	if (replica == NULL) /* compute total entry count */
	{
		obj = objset_first_obj (s_cl5Desc.dbFiles);
		while (obj)
		{
			file = (CL5DBFile*)object_get_data (obj);
			PR_ASSERT (file);
			count += file->entryCount;
			obj = objset_next_obj (s_cl5Desc.dbFiles, obj);
		}
	}
	else	/* return count for particular db */
	{
		/* select correct db file */
		rc = _cl5GetDBFile (replica, &obj);
		if (rc == CL5_SUCCESS)
		{
			file = (CL5DBFile*)object_get_data (obj);
			PR_ASSERT (file);

			count = file->entryCount; 
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"cl5GetOperationCount: found DB object %p\n", obj);
			object_release (obj);
		}
		else
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"cl5GetOperationCount: could not get DB object for replica\n");
			count = 0;
		}
	}

	_cl5RemoveThread ();
	return count;
}

/***** Helper Functions *****/

/* this call happens under state lock */
static int _cl5Open (const char *dir, const CL5DBConfig *config, CL5OpenMode openMode)
{
	int rc;

	PR_ASSERT (dir);

	/* setup db configuration parameters */
	if (config)
	{
		_cl5SetDBConfig (config);		
	}
	else
	{
		_cl5SetDefaultDBConfig ();
	}

	/* initialize trimming */
	rc = _cl5TrimInit ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Open: failed to initialize trimming\n");
		goto done;
	}

	/* create the changelog directory if it does not exist */
	rc = cl5CreateDirIfNeeded (dir);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Open: failed to create changelog directory (%s)\n", dir);
		goto done;
	}

	if (s_cl5Desc.dbDir) {
		slapi_ch_free_string (&s_cl5Desc.dbDir);
	}
	s_cl5Desc.dbDir = slapi_ch_strdup (dir);

	/* check database version */
	rc = _cl5CheckDBVersion ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5Open: invalid db version\n");
		goto done;
	}

	s_cl5Desc.dbOpenMode = openMode;

	/* initialize db environment */
	rc = _cl5AppInit ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Open: failed to initialize db environment\n");
		goto done;
	}

	/* init the clcache */
	if (( clcache_init (&s_cl5Desc.dbEnv) != 0 )) {
		rc = CL5_SYSTEM_ERROR;
		goto done;
	}

	/* open database files */
	rc = _cl5DBOpen ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Open: failed to open changelog database\n");
		
		goto done;
	}

done:;
	
	if (rc != CL5_SUCCESS)
	{
		_cl5Close ();
	}

	return rc;
}

int cl5CreateDirIfNeeded (const char *dirName)
{
	int rc;
	char buff [MAXPATHLEN + 1];
	char *t;

	PR_ASSERT (dirName);

    rc = PR_Access(dirName, PR_ACCESS_EXISTS);
    if (rc == PR_SUCCESS)
	{
        return CL5_SUCCESS;
    }

	/* directory does not exist - try to create */
	PL_strncpyz (buff, dirName, sizeof(buff)-1);
	t = strchr (buff, '/'); 

	/* skip first slash */
	if (t)
	{
		t = strchr (t+1, '/');	
	}

	while (t)
	{
		*t = '\0';
		if (PR_Access (buff, PR_ACCESS_EXISTS) != PR_SUCCESS)
		{
			rc = PR_MkDir (buff, DIR_CREATE_MODE);
			if (rc != PR_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
					"cl5CreateDirIfNeeded: failed to create dir (%s); NSPR error - %d\n",
					dirName, PR_GetError ());
				return CL5_SYSTEM_ERROR;
			}
		}

		*t++ = FILE_PATHSEP;

		t = strchr (t, '/');
	}

	/* last piece */
	rc = PR_MkDir (buff, DIR_CREATE_MODE);
	if (rc != PR_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
			"cl5CreateDirIfNeeded: failed to create dir; NSPR error - %d\n",
			PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}

	return CL5_SUCCESS;
}

static int _cl5AppInit (void)
{
	int rc = -1; /* initialize to failure */
	DB_ENV *dbEnv = NULL;
	size_t pagesize = 0;
	int openflags = 0;
	char *cookie = NULL;
	Slapi_Backend *be = slapi_get_first_backend(&cookie);
	while (be) {
		rc = slapi_back_get_info(be, BACK_INFO_DBENV, (void **)&dbEnv);
		if ((LDAP_SUCCESS == rc) && dbEnv) {
			rc = slapi_back_get_info(be,
							BACK_INFO_INDEXPAGESIZE, (void **)&pagesize);
			if ((LDAP_SUCCESS == rc) && pagesize) {
				rc = slapi_back_get_info(be,
							BACK_INFO_DBENV_OPENFLAGS, (void **)&openflags);
				if (LDAP_SUCCESS == rc) {
					break; /* Successfully fetched */
				}
			}
		}
		be = slapi_get_next_backend(cookie);
	}
	slapi_ch_free((void **)&cookie);

	if (rc == 0 && dbEnv && pagesize)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl,
			"_cl5AppInit: fetched backend dbEnv (%p)\n", dbEnv);
		s_cl5Desc.dbEnv = dbEnv;
		s_cl5Desc.dbEnvOpenFlags = openflags;
		s_cl5Desc.dbConfig.pageSize = pagesize;
		return CL5_SUCCESS;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"_cl5AppInit: failed to fetch backend dbenv (%p) and/or "
				"index page size (%lu)\n", dbEnv, pagesize);
		return CL5_DB_ERROR;
	}
}

static int _cl5DBOpen ()
{
    PRBool dbFile;
	PRDir *dir;
	PRDirEntry *entry = NULL;
	int rc = -1; /* initialize to failure */
	Object *replica;
	int count = 0;

	/* create lock that guarantees that each file is only added once to the list */
	s_cl5Desc.fileLock = PR_NewLock ();

	/* loop over all db files and open them; file name format is cl5_<dbid>.<dbext>	*/
	dir = PR_OpenDir(s_cl5Desc.dbDir);
	if (dir == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5DBOpen: failed to open changelog dir; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;

	}

	/* initialize set of db file objects */
	s_cl5Desc.dbFiles = objset_new(NULL);
	while (NULL != (entry = PR_ReadDir(dir, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
	{
		if (NULL == entry->name)
		{
			break;
		}

        dbFile = _cl5FileName2Replica (entry->name, &replica);
        if (dbFile) /* this is db file, not a log or dbversion; those are just skipped */
        {
            /* we only open files for existing replicas */
		    if (replica)
		    {
			    rc = _cl5DBOpenFile (replica, NULL /* file object */, 
				                     PR_FALSE /* check for duplicates */);
			    if (rc != CL5_SUCCESS)
			    {
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBOpen: "
									"Error opening file %s\n",
									entry->name);
				    return rc;
			    }

                object_release (replica);
				count++;
		    }
            else /* there is no matching replica for the file - remove */
            {
                char fullpathname[MAXPATHLEN];
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBOpen: "
                          "file %s has no matching replica; removing\n", entry->name);

                PR_snprintf(fullpathname, MAXPATHLEN, "%s/%s", s_cl5Desc.dbDir, entry->name);
                rc = s_cl5Desc.dbEnv->dbremove(s_cl5Desc.dbEnv,
                                               0, fullpathname, 0,
                                               DEFAULT_DB_ENV_OP_FLAGS);
                if (rc != 0)
                {
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl,
                                    "_cl5DBOpen: failed to remove (%s) file; "
                                    "libdb error - %d (%s)\n",
                                    fullpathname, rc, db_strerror(rc));
                    if (PR_Delete(fullpathname) != PR_SUCCESS) {
                        PRErrorCode prerr = PR_GetError();
                        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
                                    "_cl5DBOpen: failed to remove (%s) file; "
                                    "nspr error - %d (%s)\n",
                                    fullpathname, prerr, slapd_pr_strerror(prerr));
                    }
                }
            }
        }
	}
		
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBOpen: "
					"opened %d existing databases in %s\n", count, s_cl5Desc.dbDir);
	PR_CloseDir(dir);

	return CL5_SUCCESS;
}

/* this function assumes that the entry was validated
   using IsValidOperation 

   Data in db format:
   ------------------
   <1 byte version><1 byte change_type><sizeof time_t time><null terminated csn>
   <null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]
   
   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/
static int _cl5Entry2DBData (const CL5Entry *entry, char **data, PRUint32 *len)
{
	int size = 1 /* version */ + 1 /* operation type */ + sizeof (time_t);
	char *pos;
	PRUint32 t;
	slapi_operation_parameters *op;
	LDAPMod **add_mods = NULL;
	char *rawDN = NULL;
	char s[CSN_STRSIZE];		

	PR_ASSERT (entry && entry->op && data && len);
	op = entry->op;
	PR_ASSERT (op->target_address.uniqueid);

	/* compute size of the buffer needed to hold the data */
	size += CSN_STRSIZE;
	size += strlen (op->target_address.uniqueid) + 1;

	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:		if (op->p.p_add.parentuniqueid)
											size += strlen (op->p.p_add.parentuniqueid) + 1;
										else
											size ++; /* we just store NULL char */
										slapi_entry2mods (op->p.p_add.target_entry, &rawDN/* dn */, &add_mods);										
										size += strlen (rawDN) + 1;
										/* Need larger buffer for the encrypted changelog */
										if (s_cl5Desc.clcrypt_handle) {
											size += (_cl5GetModsSize (add_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
										} else {
											size += _cl5GetModsSize (add_mods);
										}
										break;

		case SLAPI_OPERATION_MODIFY:	size += REPL_GET_DN_LEN(&op->target_address) + 1;
										/* Need larger buffer for the encrypted changelog */
										if (s_cl5Desc.clcrypt_handle) {
											size += (_cl5GetModsSize (op->p.p_modify.modify_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
										} else {
											size += _cl5GetModsSize (op->p.p_modify.modify_mods);
										}
										break;

		case SLAPI_OPERATION_MODRDN:	size += REPL_GET_DN_LEN(&op->target_address) + 1;
										/* 1 for deleteoldrdn */
										size += strlen (op->p.p_modrdn.modrdn_newrdn) + 2; 
										if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
											size += REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address) + 1;
										else
											size ++; /* for NULL char */
										if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)
											size += strlen (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid) + 1;
										else
											size ++; /* for NULL char */
										/* Need larger buffer for the encrypted changelog */
										if (s_cl5Desc.clcrypt_handle) {
											size += (_cl5GetModsSize (op->p.p_modrdn.modrdn_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
										} else {
											size += _cl5GetModsSize (op->p.p_modrdn.modrdn_mods);
										}
										break;

		case SLAPI_OPERATION_DELETE:	size += REPL_GET_DN_LEN(&op->target_address) + 1;
										break;
	}	

	/* allocate data buffer */
	(*data) = slapi_ch_malloc (size);
	if ((*data) == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Entry2DBData: failed to allocate data buffer\n");
		return CL5_MEMORY_ERROR;
	}

	/* fill in the data buffer */
	pos = *data;
	/* write a byte of version */
	(*pos) = V_5;
	pos ++;
	/* write change type */																			 
	(*pos) = (unsigned char)op->operation_type;
	pos ++;
	/* write time */
	t = PR_htonl((PRUint32)entry->time);
	memcpy (pos, &t, sizeof (t)); 
	pos += sizeof (t);
	/* write csn */
	_cl5WriteString (csn_as_string(op->csn,PR_FALSE,s), &pos);
	/* write UniqueID */
	_cl5WriteString (op->target_address.uniqueid, &pos);
	
	/* figure out what else we need to write depending on the operation type */
	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:		_cl5WriteString (op->p.p_add.parentuniqueid, &pos);
										_cl5WriteString (rawDN, &pos);										
										_cl5WriteMods (add_mods, &pos);
										slapi_ch_free ((void**)&rawDN);
										ldap_mods_free (add_mods, 1);
										break;

		case SLAPI_OPERATION_MODIFY:	_cl5WriteString (REPL_GET_DN(&op->target_address), &pos);
										_cl5WriteMods (op->p.p_modify.modify_mods, &pos);
										break;

		case SLAPI_OPERATION_MODRDN:	_cl5WriteString (REPL_GET_DN(&op->target_address), &pos);
										_cl5WriteString (op->p.p_modrdn.modrdn_newrdn, &pos);
										*pos = (PRUint8)op->p.p_modrdn.modrdn_deloldrdn;	 
										pos ++;
										_cl5WriteString (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address), &pos);
										_cl5WriteString (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid, &pos);
										_cl5WriteMods (op->p.p_modrdn.modrdn_mods, &pos);
										break;

		case SLAPI_OPERATION_DELETE:	_cl5WriteString (REPL_GET_DN(&op->target_address), &pos);
										break;
	}
	
	/* (*len) != size in case encrypted */
	(*len) = pos - *data;

	if (*len > size) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Entry2DBData: real len %d > estimated size %d\n",
						*len, size);
		return CL5_MEMORY_ERROR;
	}

    return CL5_SUCCESS;
}

/* 
   Data in db format:
   ------------------
   <1 byte version><1 byte change_type><sizeof time_t time><null terminated dbid>
   <null terminated csn><null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]
   
   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/


int
cl5DBData2Entry (const char *data, PRUint32 len, CL5Entry *entry)
{
	int rc;
	PRUint8 version;
	char *pos = (char *)data;
	char *strCSN;
	PRUint32 thetime;
	slapi_operation_parameters *op;	
	LDAPMod **add_mods;
	char *rawDN = NULL;
	char s[CSN_STRSIZE];		

	PR_ASSERT (data && entry && entry->op);

	/* ONREPL - check that we do not go beyond the end of the buffer */

	/* read byte of version */
	version = (PRUint8)(*pos);
	if (version != V_5)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"cl5DBData2Entry: invalid data version\n");
		return CL5_BAD_FORMAT;
	}

	op = entry->op;

	pos += sizeof(version);

	/* read change type */
	op->operation_type = (PRUint8)(*pos);
	pos ++;
	
	/* need to do the copy first, to skirt around alignment problems on
	   certain architectures */
	memcpy((char *)&thetime,pos,sizeof(thetime));
	entry->time = (time_t)PR_ntohl(thetime);
	pos += sizeof (thetime);

	/* read csn */
	_cl5ReadString (&strCSN, &pos);
	if (op->csn == NULL || strcmp (strCSN, csn_as_string(op->csn,PR_FALSE,s)) != 0)
	{
		op->csn = csn_new_by_string (strCSN);	
	}
	slapi_ch_free ((void**)&strCSN);

	/* read UniqueID */
	_cl5ReadString (&op->target_address.uniqueid, &pos);	
	
	/* figure out what else we need to read depending on the operation type */
	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:		_cl5ReadString (&op->p.p_add.parentuniqueid, &pos);
			/* richm: need to free parentuniqueid */
										_cl5ReadString (&rawDN, &pos);
										op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
										/* convert mods to entry */
										rc = _cl5ReadMods (&add_mods, &pos);
										slapi_mods2entry (&(op->p.p_add.target_entry), rawDN, add_mods);
										ldap_mods_free (add_mods, 1);
										break;

		case SLAPI_OPERATION_MODIFY:    _cl5ReadString (&rawDN, &pos);
										op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
										rc = _cl5ReadMods (&op->p.p_modify.modify_mods, &pos);
										break;

		case SLAPI_OPERATION_MODRDN:	_cl5ReadString (&rawDN, &pos);
										op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
										_cl5ReadString (&op->p.p_modrdn.modrdn_newrdn, &pos);
										op->p.p_modrdn.modrdn_deloldrdn = *pos;	 
										pos ++;
										_cl5ReadString (&rawDN, &pos);
										op->p.p_modrdn.modrdn_newsuperior_address.sdn = slapi_sdn_new_dn_passin(rawDN);
										_cl5ReadString (&op->p.p_modrdn.modrdn_newsuperior_address.uniqueid, &pos);
										rc = _cl5ReadMods (&op->p.p_modrdn.modrdn_mods, &pos);
										break;

		case SLAPI_OPERATION_DELETE:	_cl5ReadString (&rawDN, &pos);
										op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
										rc = CL5_SUCCESS;
										break;

		default:							rc = CL5_BAD_FORMAT;
										slapi_log_error(SLAPI_LOG_FATAL, 
											repl_plugin_name_cl, 
											"cl5DBData2Entry: failed to format entry\n");
										break;
	}

	return rc;
}

/* thread management functions */
static int _cl5DispatchDBThreads ()
{
	PRThread *pth = NULL;

	pth = PR_CreateThread(PR_USER_THREAD, (VFP)(void*)_cl5TrimMain,
						  NULL, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, 
						  PR_UNJOINABLE_THREAD, DEFAULT_THREAD_STACKSIZE);
	if (NULL == pth)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5DispatchDBThreads: failed to create trimming "
						"thread; NSPR error - %d\n", PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}
	
	return CL5_SUCCESS;
}

static int _cl5AddThread ()
{
	/* lock the state lock so that nobody can change the state
	   while backup is in progress 
	 */
	slapi_rwlock_rdlock (s_cl5Desc.stLock);

	/* open changelog if it is not already open */
	if (s_cl5Desc.dbState != CL5_STATE_OPEN)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"_cl5AddThread: invalid changelog state - %d\n", s_cl5Desc.dbState);
		slapi_rwlock_unlock (s_cl5Desc.stLock);
		return CL5_BAD_STATE;			
	}

	slapi_rwlock_unlock (s_cl5Desc.stLock);

	/* increment global thread count to make sure that changelog does not close while
	   backup is in progress */
	PR_AtomicIncrement (&s_cl5Desc.threadCount);

	return CL5_SUCCESS;
}

static void _cl5RemoveThread ()
{
	PR_ASSERT (s_cl5Desc.threadCount > 0);
	PR_AtomicDecrement (&s_cl5Desc.threadCount);
}

/* data conversion functions */
static void _cl5WriteString (const char *str, char **buff)
{
	if (str)
	{
		strcpy (*buff, str); 
		(*buff) += strlen (str) + 1;
	}
	else /* just write NULL char */
	{
		(**buff) = '\0';
		(*buff) ++;
	}
}

static void _cl5ReadString (char **str, char **buff)
{
	if (str)
	{
		int len = strlen (*buff);
		
		if (len)
		{ 
			*str = slapi_ch_strdup (*buff);
			(*buff) += len + 1;
		}
		else /* just null char - skip it */
		{
			*str = NULL;
			(*buff) ++;
		}
	}
	else /* just skip this string */
	{
		(*buff) += strlen (*buff) + 1;		
	}
}

/* mods format:
   -----------
   <4 byte mods count><mod1><mod2>...

   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte count>
   <4 byte size><value1><4 byte size><value2>... 
 */
static void _cl5WriteMods (LDAPMod **mods, char **buff)
{	
	PRInt32 i;
	char *mod_start;
	PRInt32 count;

	if (mods == NULL)
		return;
	
	/* skip mods count */
	mod_start = (*buff) + sizeof (count);

	/* write mods*/
	for (i=0; mods[i]; i++)
	{
		_cl5WriteMod (mods[i], &mod_start);
	}

	count = PR_htonl(i);
	memcpy (*buff, &count, sizeof (count));	
	
	(*buff) = mod_start;
}

static void _cl5WriteMod (LDAPMod *mod, char **buff)
{
	char *pos;
	PRInt32 count;
	struct berval *bv;
	struct berval *encbv;
	struct berval *bv_to_use;
	Slapi_Mod smod;
	int rc = 0;

	slapi_mod_init_byref(&smod, mod);

	pos = *buff;
	/* write mod op */
	*pos = (PRUint8)slapi_mod_get_operation (&smod);
	pos ++;
	/* write attribute name	*/
	_cl5WriteString (slapi_mod_get_type (&smod), &pos);
	
	/* write value count */
	count = PR_htonl(slapi_mod_get_num_values(&smod));
	memcpy (pos, &count, sizeof (count));
	pos += sizeof (PRInt32);	

	bv = slapi_mod_get_first_value (&smod);
	while (bv)
	{
		encbv = NULL;
		rc = 0;
		rc = clcrypt_encrypt_value(s_cl5Desc.clcrypt_handle, 
		                           bv, &encbv);
		if (rc > 0) {
			/* no encryption needed. use the original bv */
			bv_to_use = bv;
		} else if ((0 == rc) && encbv) {
			/* successfully encrypted. use the encrypted bv */
			bv_to_use = encbv;
		} else { /* failed */
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5WriteMod: encrypting \"%s: %s\" failed\n",
						slapi_mod_get_type(&smod), bv->bv_val);
			bv_to_use = NULL;
		}
		if (bv_to_use) {
			_cl5WriteBerval (bv_to_use, &pos);
		}
		slapi_ch_bvfree(&encbv);
		bv = slapi_mod_get_next_value (&smod);
	}

	(*buff) = pos;

	slapi_mod_done (&smod);
}

/* mods format:
   -----------
   <4 byte mods count><mod1><mod2>...

   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte count>
   {<4 byte size><value1><4 byte size><value2>... || 
	<null terminated str1> <null terminated str2>...}
 */

static int _cl5ReadMods (LDAPMod ***mods, char **buff)
{
	char *pos = *buff;
	int i;
	int rc;
	PRInt32 mod_count;
	Slapi_Mods smods;
	Slapi_Mod smod;

	/* need to copy first, to skirt around alignment problems on certain
	   architectures */
	memcpy((char *)&mod_count,*buff,sizeof(mod_count));
	mod_count = PR_ntohl(mod_count);
	pos += sizeof (mod_count);
	
	slapi_mods_init (&smods , mod_count);

	for (i = 0; i < mod_count; i++)
	{		
		rc = _cl5ReadMod (&smod, &pos);
		if (rc != CL5_SUCCESS)
		{
			slapi_mods_done(&smods);
			return rc;
		}

		slapi_mods_add_smod(&smods, &smod);
	}
 
	*buff = pos;

	*mods = slapi_mods_get_ldapmods_passout (&smods);
	slapi_mods_done(&smods);	

	return CL5_SUCCESS;
}

static int _cl5ReadMod (Slapi_Mod *smod, char **buff)
{
	char *pos = *buff;
	int i;
	PRInt32 val_count;
	char *type;
	int op;
	struct berval bv;
	struct berval *decbv;
	struct berval *bv_to_use;
	int rc = 0;

	op = (*pos) & 0x000000FF;
	pos ++;
	_cl5ReadString (&type, &pos);

	/* need to do the copy first, to skirt around alignment problems on
	   certain architectures */
	memcpy((char *)&val_count,pos,sizeof(val_count));
	val_count = PR_ntohl(val_count);
	pos += sizeof (PRInt32);

	slapi_mod_init(smod, val_count);
	slapi_mod_set_operation (smod, op|LDAP_MOD_BVALUES); 
	slapi_mod_set_type (smod, type);
	slapi_ch_free ((void**)&type);

	for (i = 0; i < val_count; i++)
	{
		_cl5ReadBerval (&bv, &pos);
		decbv = NULL;
		rc = 0;
		rc = clcrypt_decrypt_value(s_cl5Desc.clcrypt_handle,
		                           &bv, &decbv);
		if (rc > 0) {
			/* not encrypted. use the original bv */
			bv_to_use = &bv;
		} else if ((0 == rc) && decbv) {
			/* successfully decrypted. use the decrypted bv */
			bv_to_use = decbv;
		} else { /* failed */
			char encstr[128];
			char *encend = encstr + 128;
			char *ptr;
			int i;
			for (i = 0, ptr = encstr; (i < bv.bv_len) && (ptr < encend - 4);
				 i++, ptr += 3) {
				sprintf(ptr, "%x", 0xff & bv.bv_val[i]);
			}
			if (ptr >= encend - 4) {
				sprintf(ptr, "...");
				ptr += 3;
			}
			*ptr = '\0';
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5ReadMod: decrypting \"%s: %s\" failed\n",
						slapi_mod_get_type(smod), encstr);
			bv_to_use = NULL;
		}
		if (bv_to_use) {
			slapi_mod_add_value (smod, bv_to_use);
		}
		slapi_ch_bvfree(&decbv);
		slapi_ch_free((void **) &bv.bv_val);
	}

	(*buff) = pos;

	return CL5_SUCCESS;
}

static int _cl5GetModsSize (LDAPMod **mods)
{
	int size;
	int i;

	if (mods == NULL)
		return 0;

	size = sizeof (PRInt32);
	for (i=0; mods[i]; i++)
	{
		size += _cl5GetModSize (mods[i]);
	}

	return size;
}

static int _cl5GetModSize (LDAPMod *mod)
{
	int size;
	int i;

	size = 1 + strlen (mod->mod_type) + 1 + sizeof (mod->mod_op);
	i = 0;
	if (mod->mod_op & LDAP_MOD_BVALUES) /* values are in binary form */
	{
		while (mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL)
		{
			size += mod->mod_bvalues[i]->bv_len + sizeof (mod->mod_bvalues[i]->bv_len); 			
			i++;
		}
	}
	else /* string data */
	{
		PR_ASSERT(0); /* ggood string values should never be used in the server */
	}

	return size;
}

static void _cl5ReadBerval (struct berval *bv, char** buff)
{
	PRUint32 length = 0;
	PRUint32 net_length = 0;

	PR_ASSERT (bv && buff);

    /***PINAKI need to do the copy first, to skirt around alignment problems on
		   certain architectures */
	/* DBDB : struct berval.bv_len is defined as unsigned long 
	 * But code here expects it to be 32-bits in size.
	 * On 64-bit machines, this is not the case.
	 * I changed the code to consistently use 32-bit (4-byte)
	 * values on the encoded side. This means that it's
	 * possible to generate a huge berval that will not
	 * be encoded properly. However, this seems unlikely
	 * to happen in reality, and I felt that retaining the
	 * old on-disk format for the changely in the 64-bit
	 * version of the server was important.
	 */

    memcpy((char *)&net_length, *buff, sizeof(net_length));
    length = PR_ntohl(net_length);
    *buff += sizeof(net_length);
	bv->bv_len = length;

    if (bv->bv_len > 0) {
		bv->bv_val = slapi_ch_malloc (bv->bv_len);
		memcpy (bv->bv_val, *buff, bv->bv_len);
		*buff += bv->bv_len;
    }
    else {
		bv->bv_val = NULL;
    }
}

static void _cl5WriteBerval (struct berval *bv, char** buff)
{
	PRUint32 length = 0;
	PRUint32 net_length = 0;

	length = (PRUint32) bv->bv_len;
    net_length = PR_htonl(length);

	memcpy(*buff, &net_length, sizeof (net_length));
	*buff += sizeof (net_length);
	memcpy (*buff, bv->bv_val, length);
	*buff += length;
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
static int _cl5ReadBervals (struct berval ***bv, char** buff, unsigned int size)
{
    PRInt32 count;
    int i;
    char *pos;

    PR_ASSERT (bv && buff);

    /* ONREPL - need to check that we don't go beyond the end of the buffer */

    pos = *buff;
    memcpy((char *)&count, pos, sizeof(count)); 
    count = PR_htonl (count);
    pos += sizeof(count);

    /* allocate bervals */
    *bv = (struct berval **)slapi_ch_malloc ((count + 1) * sizeof (struct berval*));
    if (*bv == NULL)
    {
        return CL5_MEMORY_ERROR;
    }

    for (i = 0; i < count; i++)
    {
        (*bv)[i] = (struct berval *)slapi_ch_malloc (sizeof (struct berval));
        if ((*bv)[i] == NULL)
        {
            ber_bvecfree(*bv);
            return CL5_MEMORY_ERROR;
        }

        _cl5ReadBerval ((*bv)[i], &pos);
    } 

    (*bv)[count] = NULL;
    *buff = pos;      

    return CL5_SUCCESS;
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
static int _cl5WriteBervals (struct berval **bv, char** buff, unsigned int *size)
{
    PRInt32 count, net_count;
    char *pos;
    int i;

    PR_ASSERT (bv && buff && size);    

    /* compute number of values and size of the buffer to hold them */
    *size = sizeof (count); 
    for (count = 0; bv[count]; count ++)
    {
        *size += sizeof (bv[count]->bv_len) + bv[count]->bv_len;
    }

    /* allocate buffer */
    *buff = (char*) slapi_ch_malloc (*size);
    if (*buff == NULL)
    {
        *size = 0;
        return CL5_MEMORY_ERROR;
    }

    /* fill the buffer */
    pos = *buff;
    net_count = PR_htonl(count);
    memcpy (pos, &net_count, sizeof (net_count));
    pos += sizeof (net_count);
    for (i = 0; i < count; i ++)
    {
        _cl5WriteBerval (bv[i], &pos);
    }
    
    return CL5_SUCCESS;
}

/* upgrade from db33 to db41
 * 1. Run recovery on the database environment using the DB_ENV->open method
 * 2. Remove any Berkeley DB environment using the DB_ENV->remove method 
 * 3. Remove any Berkeley DB transaction log files
 * 4. extention .db3 -> .db4 
 */
static int _cl5Upgrade3_4(char *fromVersion, char *toVersion)
{
	PRDir *dir = NULL;
	PRDirEntry *entry = NULL;
	DB *thisdb = NULL;
	CL5OpenMode	backup;
	int rc = 0;

	backup = s_cl5Desc.dbOpenMode;
	s_cl5Desc.dbOpenMode = CL5_OPEN_CLEAN_RECOVER;
	/* CL5_OPEN_CLEAN_RECOVER does 1 and 2 */
	rc = _cl5AppInit ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Upgrade3_4: failed to open the db env\n");
		return rc;
	}
	s_cl5Desc.dbOpenMode = backup;

	dir = PR_OpenDir(s_cl5Desc.dbDir);
	if (dir == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
		  "_cl5Upgrade3_4: failed to open changelog dir %s; NSPR error - %d\n",
		  s_cl5Desc.dbDir, PR_GetError ());
		goto out;
	}

	while (NULL != (entry = PR_ReadDir(dir, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
	{
		if (NULL == entry->name)
		{
			break;
		}
		if (_cl5FileEndsWith(entry->name, DB_EXTENSION_DB3))
		{
			char oName [MAXPATHLEN + 1];
			char nName [MAXPATHLEN + 1];
			char *p = NULL;
			char c;
			int baselen = 0;
			PR_snprintf(oName, MAXPATHLEN, "%s/%s", s_cl5Desc.dbDir, entry->name);
			p = strstr(oName, DB_EXTENSION_DB3);
			if (NULL == p)
			{
				continue;
			}
			/* db->rename closes DB; need to create every time */
			rc = db_create(&thisdb, s_cl5Desc.dbEnv, 0);
			if (0 != rc) {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Upgrade3_4: failed to get db handle\n");
				goto out;
			}

			baselen = p - oName;
			c = *p;
			*p = '\0';
			PR_snprintf(nName, MAXPATHLEN+1, "%s", oName);
			PR_snprintf(nName + baselen, MAXPATHLEN+1-baselen, "%s", DB_EXTENSION);
			*p = c;
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
				"_cl5Upgrade3_4: renaming %s to %s\n", oName, nName);
			rc = thisdb->rename(thisdb, (const char *)oName, NULL /* subdb */,
											  (const char *)nName, 0);
			if (rc != PR_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
					"_cl5Upgrade3_4: failed to rename file (%s -> %s); "
					"db error - %d %s\n", oName, nName, rc, db_strerror(rc));
				break;
			}
		}
	}
	/* update the version file */
	_cl5WriteDBVersion ();
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
		"Upgrading from %s to %s is successfully done (%s)\n",
		fromVersion, toVersion, s_cl5Desc.dbDir);
out:
	if (NULL != dir)
	{
		PR_CloseDir(dir);
	}
	return rc;
}

/* upgrade from db41 -> db42 -> db43 -> db44 -> db45
 * 1. Run recovery on the database environment using the DB_ENV->open method
 * 2. Remove any Berkeley DB environment using the DB_ENV->remove method 
 * 3. Remove any Berkeley DB transaction log files
 */
static int _cl5Upgrade4_4(char *fromVersion, char *toVersion)
{
	CL5OpenMode	backup;
	int rc = 0;

	backup = s_cl5Desc.dbOpenMode;
	s_cl5Desc.dbOpenMode = CL5_OPEN_CLEAN_RECOVER;
	/* CL5_OPEN_CLEAN_RECOVER does 1 and 2 */
	rc = _cl5AppInit ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Upgrade4_4: failed to open the db env\n");
		return rc;
	}
	s_cl5Desc.dbOpenMode = backup;

	/* update the version file */
	_cl5WriteDBVersion ();
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
		"Upgrading from %s to %s is successfully done (%s)\n",
		fromVersion, toVersion, s_cl5Desc.dbDir);

	return rc;
}

static int _cl5CheckDBVersion ()
{
	char clVersion [VERSION_SIZE + 1];
	char dbVersion [VERSION_SIZE + 1];
	int rc;

	if (!cl5Exist (s_cl5Desc.dbDir))
	{
		/* this is new changelog - write DB version and guardian file */
		rc = _cl5WriteDBVersion ();
	}
	else
	{
		char *versionp = NULL;
		char *versionendp = NULL;
		char *dotp = NULL;
		int dbmajor = 0;
		int dbminor = 0;

		PR_snprintf (clVersion, VERSION_SIZE, "%s/%d.%d/%s",
				BDB_IMPL, DB_VERSION_MAJOR, DB_VERSION_MINOR, BDB_REPLPLUGIN);

		rc = _cl5ReadDBVersion (s_cl5Desc.dbDir, dbVersion, sizeof(dbVersion));

		if (rc != CL5_SUCCESS)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5CheckDBVersion: invalid dbversion\n");
			rc = CL5_BAD_DBVERSION;
			goto bailout;
		}
		versionendp = dbVersion + strlen(dbVersion);
		/* get the version number */
		/* old DBVERSION string: CL5_TYPE/REPL_PLUGIN_NAME/#.# */
		if (PL_strncmp(dbVersion, CL5_TYPE, strlen(CL5_TYPE)) == 0)
		{
			versionp = strrchr(dbVersion, '/');
		}
		/* new DBVERSION string: bdb/#.#/libreplication-plugin */
		else if (PL_strncmp(dbVersion, BDB_IMPL, strlen(BDB_IMPL)) == 0)
		{
			versionp = strchr(dbVersion, '/');
		}
		if (NULL == versionp || versionp == versionendp)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"_cl5CheckDBVersion: invalid dbversion: %s\n", dbVersion);
			rc = CL5_BAD_DBVERSION;
			goto bailout;
		}
		dotp = strchr(++versionp, '.');
		if (NULL != dotp)
		{
			*dotp = '\0';
			dbmajor = strtol(versionp, (char **)NULL, 10);
			dbminor = strtol(dotp+1, (char **)NULL, 10);
			*dotp = '.';
		}
		else
		{
			dbmajor = strtol(versionp, (char **)NULL, 10);
		}

		if (dbmajor < DB_VERSION_MAJOR)
		{
			/* upgrade */
			rc = _cl5Upgrade3_4(dbVersion, clVersion);
			if (rc != CL5_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5CheckDBVersion: upgrade %s -> %s failed\n",
						dbVersion, clVersion);
				rc = CL5_BAD_DBVERSION;
			}
		}
		else if (dbminor < DB_VERSION_MINOR)
		{
			/* minor upgrade */
			rc = _cl5Upgrade4_4(dbVersion, clVersion);
			if (rc != CL5_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5CheckDBVersion: upgrade %s -> %s failed\n",
						dbVersion, clVersion);
				rc = CL5_BAD_DBVERSION;
			}
		}
	}
bailout:
	return rc;
}

static int _cl5ReadDBVersion (const char *dir, char *clVersion, int buflen)
{
	int rc;
	PRFileDesc *file;
	char fName [MAXPATHLEN + 1];
	char buff [BUFSIZ];
	PRInt32 size;
	char *tok;
	char * iter = NULL;

	if (clVersion)
	{
		clVersion [0] = '\0';
	}

	PR_snprintf (fName, MAXPATHLEN, "%s/%s", dir, VERSION_FILE);

	file = PR_Open (fName, PR_RDONLY, 777);
	if (file == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5ReadDBVersion: failed to open DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}
	
	size = slapi_read_buffer (file, buff, BUFSIZ);
	if (size < 0)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5ReadDBVersion: failed to read DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		PR_Close (file);
		return CL5_SYSTEM_ERROR;
	}

	/* parse the data */
	buff[size]= '\0';
	tok = ldap_utf8strtok_r (buff, "\n", &iter);	
	if (tok)
	{
		if (clVersion)
		{
    		PL_strncpyz(clVersion, tok, buflen);
		}
	}

	rc = PR_Close (file);
	if (rc != PR_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5ReadDBVersion: failed to close DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}

	return CL5_SUCCESS;		
}

static int _cl5WriteDBVersion ()
{
	int rc;
	PRFileDesc *file;
	char fName [MAXPATHLEN + 1];
	char clVersion [VERSION_SIZE + 1];
	PRInt32 len, size;

	PR_snprintf (fName, MAXPATHLEN, "%s/%s", s_cl5Desc.dbDir, VERSION_FILE);

	file = PR_Open (fName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
					s_cl5Desc.dbConfig.fileMode);
	if (file == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5WriteDBVersion: failed to open DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}

	/* write changelog version */
	PR_snprintf (clVersion, VERSION_SIZE, "%s/%d.%d/%s\n", 
				BDB_IMPL, DB_VERSION_MAJOR, DB_VERSION_MINOR, BDB_REPLPLUGIN);

	len = strlen(clVersion);
	size = slapi_write_buffer (file, clVersion, len);
	if (size != len)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5WriteDBVersion: failed to write DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		PR_Close (file);
		return CL5_SYSTEM_ERROR;
	}

	rc = PR_Close (file);
	if (rc != PR_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5WriteDBVersion: failed to close DBVERSION; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}

	return CL5_SUCCESS;	
}

/* must be called under the state lock */
static void _cl5Close ()
{
	PRIntervalTime interval;

	if (s_cl5Desc.dbState != CL5_STATE_CLOSED) /* Don't try to close twice */
	{

		/* cl5Close() set the state flag to CL5_STATE_CLOSING, which should
		   trigger all of the db housekeeping threads to exit, and which will
		   eventually cause no new update threads to start - so we wait here
		   for those other threads to finish before we proceed */
		interval = PR_MillisecondsToInterval(100);			
		while (s_cl5Desc.threadCount > 0)
		{
			slapi_log_error( SLAPI_LOG_REPL, repl_plugin_name_cl,
							"_cl5Close: waiting for threads to exit: %d thread(s) still active\n",
							s_cl5Desc.threadCount);
			DS_Sleep(interval);
		}
		
		/* There should now be no threads accessing any of the changelog databases -
		   it is safe to remove those databases */
		_cl5DBClose ();

		/* cleanup trimming */
		_cl5TrimCleanup ();

		/* remove changelog if requested */
		if (s_cl5Desc.dbRmOnClose)
		{
			
			if (_cl5Delete (s_cl5Desc.dbDir, 1) != CL5_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
								"cl5Close: failed to remove changelog\n");
			}
			s_cl5Desc.dbRmOnClose = PR_FALSE;
		}

		slapi_ch_free ((void **)&s_cl5Desc.dbDir);
		memset (&s_cl5Desc.dbConfig, 0, sizeof (s_cl5Desc.dbConfig));
		s_cl5Desc.fatalError = PR_FALSE;
		s_cl5Desc.threadCount = 0;
		s_cl5Desc.dbOpenMode = CL5_OPEN_NONE;
	}
}

static void _cl5DBClose ()
{
	if (NULL != s_cl5Desc.dbFiles)
	{
		Object *obj;
		for (obj = objset_first_obj(s_cl5Desc.dbFiles); obj;
			 obj = objset_next_obj(s_cl5Desc.dbFiles, obj)) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5DBClose: deleting DB object %p\n", obj);
		}
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5DBClose: closing databases in %s\n", s_cl5Desc.dbDir);
		objset_delete (&s_cl5Desc.dbFiles);
	}
	if (NULL != s_cl5Desc.fileLock)
	{
		PR_DestroyLock (s_cl5Desc.fileLock);
		s_cl5Desc.fileLock = NULL;
	}
}

/* see if the given file is a changelog db file */
static int
_cl5IsDbFile(const char *fname)
{
	if (!fname || !*fname) {
		return 0;
	}

	if (!strcmp(fname, VERSION_FILE)) {
		return 1;
	}

	if (_cl5FileEndsWith(fname, DB_EXTENSION)) {
		return 1;
	}

	return 0; /* not a filename we recognize as being associated with the db */
}

/* state lock must be locked */
static int  _cl5Delete (const char *clDir, int rmDir)
{
	PRDir *dir;
	char  filename[MAXPATHLEN + 1];
	PRDirEntry *entry = NULL;
	int rc;
	int dirisempty = 1;

	/* remove all files in the directory and the directory */
	dir = PR_OpenDir(clDir);
	if (dir == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Delete: failed to open changelog dir; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;

	}

	while (NULL != (entry = PR_ReadDir(dir, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
	{
		if (NULL == entry->name)
		{
			break;
		}
		if (!_cl5IsDbFile(entry->name)) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5Delete: Skipping file [%s/%s] because it is not a changelogdb file.\n",
							clDir, entry->name);
			dirisempty = 0; /* skipped at least one file - dir not empty */
			continue;
		}
		PR_snprintf(filename, MAXPATHLEN, "%s/%s", clDir, entry->name);
		/* _cl5Delete deletes the whole changelog directory with all the files 
		 * underneath.  Thus, we can just remove them physically. */
		if (0 == strcmp(entry->name, VERSION_FILE)) {
			/* DBVERSION */
			rc = PR_Delete(filename) != PR_SUCCESS;
			if (PR_SUCCESS != rc) {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"_cl5Delete: failed to remove \"%s\"; NSPR error - %d\n",
					filename, PR_GetError ());
			}
		} else {
			/* DB files */
			rc = s_cl5Desc.dbEnv->dbremove(s_cl5Desc.dbEnv, 0, filename, 0,
                                           DEFAULT_DB_ENV_OP_FLAGS);
			if (rc) {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				                "_cl5Delete: failed to remove \"%s\"; "
				                "libdb error - %d (%s)\n",
				                filename, rc, db_strerror(rc));
			}
		}
	}
		
	rc = PR_CloseDir(dir);
	if (rc != PR_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Delete: failed to close changelog dir (%s); NSPR error - %d\n",
						clDir, PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}
		
	if (rmDir && dirisempty)
	{
		rc = PR_RmDir (clDir);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
					"_cl5Delete: failed to remove changelog dir (%s); errno = %d\n", 
					clDir, errno);
			return CL5_SYSTEM_ERROR;
		}
	} else if (rmDir && !dirisempty) {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5Delete: changelog dir (%s) is not empty - cannot remove\n",
						clDir);
	}

	/* invalidate the clcache */
	clcache_destroy();
			
	return CL5_SUCCESS;
}

static void _cl5SetDefaultDBConfig ()
{
  s_cl5Desc.dbConfig.maxConcurrentWrites= CL5_DEFAULT_CONFIG_MAX_CONCURRENT_WRITES;
  s_cl5Desc.dbConfig.fileMode           = FILE_CREATE_MODE;
}

static void _cl5SetDBConfig (const CL5DBConfig *config)
{
  /* s_cl5Desc.dbConfig.pageSize is retrieved from backend */
  /* Some other configuration parameters are hardcoded... */
  s_cl5Desc.dbConfig.maxConcurrentWrites = config->maxConcurrentWrites;
  s_cl5Desc.dbConfig.fileMode = FILE_CREATE_MODE;
}

/* Trimming helper functions */
static int _cl5TrimInit ()
{
	/* just create the lock while we are singlethreaded */
	s_cl5Desc.dbTrim.lock = PR_NewLock();

	if (s_cl5Desc.dbTrim.lock == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5InitTrimming: failed to create lock; NSPR error - %d\n",
						PR_GetError ());
		return CL5_SYSTEM_ERROR;
	}
	else
	{
		return CL5_SUCCESS;
	}
}

static void _cl5TrimCleanup ()
{
	if (s_cl5Desc.dbTrim.lock)
		PR_DestroyLock (s_cl5Desc.dbTrim.lock);

	memset (&s_cl5Desc.dbTrim, 0, sizeof (s_cl5Desc.dbTrim));
}

static int _cl5TrimMain (void *param)
{
	PRIntervalTime    interval; 
	time_t timePrev = current_time ();
	time_t timeNow;

	PR_AtomicIncrement (&s_cl5Desc.threadCount);
	interval = PR_SecondsToInterval(CHANGELOGDB_TRIM_INTERVAL);

	while (s_cl5Desc.dbState != CL5_STATE_CLOSING)
	{
		timeNow = current_time ();
		if (timeNow - timePrev >= CHANGELOGDB_TRIM_INTERVAL)
		{
			/* time to trim */
			timePrev = timeNow; 
			_cl5DoTrimming (0 /* there's no cleaned rid */);
		}
		if (NULL == s_cl5Desc.clLock)
		{
			/* most likely, emergency */
			break;
		}
		
		PR_Lock(s_cl5Desc.clLock);
		PR_WaitCondVar(s_cl5Desc.clCvar, interval);		
		PR_Unlock(s_cl5Desc.clLock);
	}

	PR_AtomicDecrement (&s_cl5Desc.threadCount);
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5TrimMain: exiting\n");

    return 0;
}

/* We remove an entry if it has been replayed to all consumers and
   and the number of entries in the changelog is larger than maxEntries 
   or age of the entry is larger than maxAge. 
   Also we can't purge entries which correspond to max csns in the
   supplier's ruv. Here is a example where we can get into trouble:
   The server is setup with time based trimming and no consumer's
   At some point all the entries are trimmed from the changelog.
   At a later point a consumer is added and initialized online
   Then a change is made on the supplier.
   To update the consumer, the supplier would attempt to locate
   the last change sent to the consumer in the changelog and will
   fail because the change was removed.
    
 */

static void _cl5DoTrimming (ReplicaId rid)
{
	Object *obj;
	long numToTrim;

	PR_Lock (s_cl5Desc.dbTrim.lock);

	/* ONREPL We trim file by file which means that some files will be 
	   trimmed more often than other. We might have to fix that by, for 
	   example, randomizing starting point */
	obj = objset_first_obj (s_cl5Desc.dbFiles);
	while (obj && _cl5CanTrim ((time_t)0, &numToTrim))
	{	
		_cl5TrimFile (obj, &numToTrim, rid);
		obj = objset_next_obj (s_cl5Desc.dbFiles, obj);	
	}

    if (obj)
        object_release (obj);

	PR_Unlock (s_cl5Desc.dbTrim.lock);

	return;
}

/* Note that each file contains changes for a single replicated area.
   trimming algorithm:
*/
#define CL5_TRIM_MAX_PER_TRANSACTION 10

static void _cl5TrimFile (Object *obj, long *numToTrim, ReplicaId cleaned_rid)
{
	DB_TXN *txnid;
	RUV *ruv = NULL;
	CL5Entry entry;
	slapi_operation_parameters op = {0};
	ReplicaId csn_rid;
	void *it;
	int finished = 0, totalTrimmed = 0, count;
	PRBool abort;
	char strCSN[CSN_STRSIZE];
	int rc;
	
	PR_ASSERT (obj);

	/* construct the ruv up to which we can purge */
	rc = _cl5GetRUV2Purge2 (obj, &ruv);
	if (rc != CL5_SUCCESS || ruv == NULL)
	{
		return;
	}

	entry.op = &op;

	while ( !finished && !slapi_is_shutting_down() )
	{
		it = NULL;
		count = 0;
		txnid = NULL;
		abort = PR_FALSE;

		/* DB txn lock accessed pages until the end of the transaction. */
		
		rc = TXN_BEGIN(s_cl5Desc.dbEnv, NULL, &txnid, 0);
		if (rc != 0) 
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"_cl5TrimFile: failed to begin transaction; db error - %d %s\n", 
				rc, db_strerror(rc));
			finished = PR_TRUE;
			break;
		}

		finished = _cl5GetFirstEntry (obj, &entry, &it, txnid);
		while ( !finished )
		{
        	/*
			 * This change can be trimmed if it exceeds purge
			 * parameters and has been seen by all consumers.
			 */
			if(op.csn == NULL){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "_cl5TrimFile: "
						"Operation missing csn, moving on to next entry.\n");
				cl5_operation_parameters_done (&op);
				finished =_cl5GetNextEntry (&entry, it);
				continue;
			}
			csn_rid = csn_get_replicaid (op.csn);
			if ( (*numToTrim > 0 || _cl5CanTrim (entry.time, numToTrim)) &&
				 ruv_covers_csn_strict (ruv, op.csn) )
			{
				rc = _cl5CurrentDeleteEntry (it);
				if ( rc == CL5_SUCCESS && cleaned_rid != csn_rid)
				{
					rc = _cl5UpdateRUV (obj, op.csn, PR_FALSE, PR_TRUE);				
				}
				if ( rc == CL5_SUCCESS)
				{
					if (*numToTrim > 0) (*numToTrim)--;
					count++;
				}
				else
				{
					/* The above two functions have logged the error */
					abort = PR_TRUE;
				}

			}
			else
			{
				/* The changelog DB is time ordered. If we can not trim
				 * a CSN, we will not be allowed to trim the rest of the
				 * CSNs generally. However, the maxcsn of each replica ID
				 * is always kept in the changelog as an anchor for
				 * replaying future changes. We have to skip those anchor
				 * CSNs, otherwise a non-active replica ID could block
				 * the trim forever.
				 */
				CSN *maxcsn = NULL;
				ruv_get_largest_csn_for_replica (ruv, csn_rid, &maxcsn);
				if ( csn_compare (op.csn, maxcsn) != 0 )
				{
					/* op.csn is not anchor CSN */
					finished = 1;
				}
				else
				{
					if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
						slapi_log_error (SLAPI_LOG_REPL, NULL,
										 "Changelog purge skipped anchor csn %s\n",
										 csn_as_string (maxcsn, PR_FALSE, strCSN));
					}

					/* extra read to skip the current record */
					cl5_operation_parameters_done (&op);
					finished =_cl5GetNextEntry (&entry, it);
				}
				if (maxcsn) csn_free (&maxcsn);
			}
			cl5_operation_parameters_done (&op);
			if (finished || abort || count >= CL5_TRIM_MAX_PER_TRANSACTION)
			{
				/* If we reach CL5_TRIM_MAX_PER_TRANSACTION, 
				 * we close the cursor, 
				 * commit the transaction and restart a new transaction
				 */
				break;
			}
			finished = _cl5GetNextEntry (&entry, it);
		}

		/* MAB: We need to close the cursor BEFORE the txn commits/aborts.
		 * If we don't respect this order, we'll screw up the database,
		 * placing it in DB_RUNRECOVERY mode
		 */
		cl5DestroyIterator (it);

		if (abort)
		{
			finished = 1;
			rc = TXN_ABORT (txnid);
			if (rc != 0)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"_cl5TrimFile: failed to abort transaction; db error - %d %s\n",
					rc, db_strerror(rc));	
			}
		}
		else
		{
			rc = TXN_COMMIT (txnid, 0);
			if (rc != 0)
			{
				finished = 1;
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"_cl5TrimFile: failed to commit transaction; db error - %d %s\n",
					rc, db_strerror(rc));
			}
			else
			{
				totalTrimmed += count;
			}
		}

	} /* While (!finished) */

	if (ruv)
		ruv_destroy (&ruv);

	if (totalTrimmed)
	{
		slapi_log_error (SLAPI_LOG_REPL, NULL, "Trimmed %d changes from the changelog\n", totalTrimmed);
	}
}

static PRBool _cl5CanTrim (time_t time, long *numToTrim)
{
	*numToTrim = 0;

    if (s_cl5Desc.dbTrim.maxAge == 0 && s_cl5Desc.dbTrim.maxEntries == 0)
		return PR_FALSE;

	if (s_cl5Desc.dbTrim.maxAge == 0)
	{
		*numToTrim = cl5GetOperationCount (NULL) - s_cl5Desc.dbTrim.maxEntries;
		return ( *numToTrim > 0 );
	}

    if (s_cl5Desc.dbTrim.maxEntries > 0 &&
		(*numToTrim = cl5GetOperationCount (NULL) - s_cl5Desc.dbTrim.maxEntries) > 0)
    	return PR_TRUE;

	if (time)
		return (current_time () - time > s_cl5Desc.dbTrim.maxAge);
    else			
	    return PR_TRUE;
}  

static int _cl5ReadRUV (const char *replGen, Object *obj, PRBool purge)
{
    int rc;
	char csnStr [CSN_STRSIZE];
	DBT key={0}, data={0};
    struct berval **vals = NULL;
    CL5DBFile *file;
	char *pos;
	char *agmt_name;


	PR_ASSERT (replGen && obj);

    file = (CL5DBFile*)object_get_data (obj);
    PR_ASSERT (file);

	agmt_name = get_thread_private_agmtname();
	
    if (purge) /* read purge vector entry */
	    key.data = _cl5GetHelperEntryKey (PURGE_RUV_TIME, csnStr);
    else /* read upper bound vector */
        key.data = _cl5GetHelperEntryKey (MAX_RUV_TIME, csnStr);

	key.size = CSN_STRSIZE;

	data.flags = DB_DBT_MALLOC;

	rc = file->db->get(file->db, NULL/*txn*/, &key, &data, 0);
    switch (rc)
	{
		case 0:				pos = data.data;
							rc = _cl5ReadBervals (&vals, &pos, data.size);
                            slapi_ch_free (&(data.data));
                            if (rc != CL5_SUCCESS)
				goto done;
                            
                            if (purge)
                                rc = ruv_init_from_bervals(vals, &file->purgeRUV);							
                            else
                                rc = ruv_init_from_bervals(vals, &file->maxRUV);	    

                            if (rc != RUV_SUCCESS)
                            {
                                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						            "%s: _cl5ReadRUV: failed to initialize %s ruv; "
                                    "RUV error %d\n", agmt_name, purge? "purge" : "upper bound", rc);
						
                                rc = CL5_RUV_ERROR;
				goto done;
                            }

                            /* delete the entry; it is re-added when file
							   is successfully closed */
							file->db->del (file->db, NULL, &key, DEFAULT_DB_OP_FLAGS(NULL));
							
							rc = CL5_SUCCESS;
							goto done;

		case DB_NOTFOUND:	/* RUV is lost - need to construct */
                            rc = _cl5ConstructRUV (replGen, obj, purge);
							goto done;
		
		default:			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
								"%s: _cl5ReadRUV: failed to get purge RUV; "
								"db error - %d %s\n", agmt_name, rc, db_strerror(rc));
							rc = CL5_DB_ERROR;
							goto done;
	}	

done:
	ber_bvecfree(vals);
	return rc;
}

static int _cl5WriteRUV (CL5DBFile *file, PRBool purge)
{
	int rc;
	DBT key={0}, data={0};
	char csnStr [CSN_STRSIZE];
	struct berval **vals;
	DB_TXN *txnid = NULL;
	char *buff;

	if ((purge && file->purgeRUV == NULL) || (!purge && file->maxRUV == NULL))
		return CL5_SUCCESS;

	if (purge)
	{
		/* Set the minimum CSN of each vector to a dummy CSN that contains
		 * just a replica ID, e.g. 00000000000000010000.
		 * The minimum CSN in a purge RUV is not used so the value doesn't
		 * matter, but it needs to be set to something so that it can be
		 * flushed to changelog at shutdown and parsed at startup with the
		 * regular string-to-RUV parsing routines. */
		ruv_insert_dummy_min_csn(file->purgeRUV);
		key.data = _cl5GetHelperEntryKey (PURGE_RUV_TIME, csnStr);
		rc = ruv_to_bervals(file->purgeRUV, &vals);
	}
	else
	{
		key.data = _cl5GetHelperEntryKey (MAX_RUV_TIME, csnStr);
		rc = ruv_to_bervals(file->maxRUV, &vals);
	}

	key.size = CSN_STRSIZE;
    
	rc = _cl5WriteBervals (vals, &buff, &data.size);
	data.data = buff;
	ber_bvecfree(vals);
	if (rc != CL5_SUCCESS)
	{
		return rc;
	}

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
	rc = txn_begin(s_cl5Desc.dbEnv, NULL, &txnid, 0);
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"_cl5WriteRUV: failed to begin transaction; db error - %d %s\n",
						rc, db_strerror(rc));
		return CL5_DB_ERROR;
	}
#endif
	rc = file->db->put(file->db, txnid, &key, &data, DEFAULT_DB_OP_FLAGS(txnid));

	slapi_ch_free (&(data.data));
	if ( rc == 0 )
	{
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
		rc = txn_commit (txnid, 0);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5WriteRUV: failed to commit transaction; db error - %d %s\n", 
						rc, db_strerror(rc));
			return CL5_DB_ERROR;
		}
#endif
		return CL5_SUCCESS;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"_cl5WriteRUV: failed to write %s RUV for file %s; db error - %d (%s)\n",
			purge? "purge" : "upper bound", file->name, rc, db_strerror(rc));

		if (CL5_OS_ERR_IS_DISKFULL(rc))
		{
			cl5_set_diskfull();
			return CL5_DB_ERROR;
		}
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
		rc = txn_abort (txnid);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
							"_cl5WriteRUV: failed to abort transaction; db error - %d %s\n",
							rc, db_strerror(rc));
		}
#endif
		return CL5_DB_ERROR;
	}
}

/* This is a very slow process since we have to read every changelog entry.
   Hopefully, this function is not called too often */
static int  _cl5ConstructRUV (const char *replGen, Object *obj, PRBool purge)
{
    int rc;
    CL5Entry entry;
    void *iterator = NULL;
    slapi_operation_parameters op = {0};
    CL5DBFile *file;
    ReplicaId rid;

    PR_ASSERT (replGen && obj);

    file = (CL5DBFile*)object_get_data (obj);
    PR_ASSERT (file);

    /* construct the RUV */
    if (purge)
        rc = ruv_init_new (replGen, 0, NULL, &file->purgeRUV);
    else
        rc = ruv_init_new (replGen, 0, NULL, &file->maxRUV);
    if (rc != RUV_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUV: "
				"failed to initialize %s RUV for file %s; ruv error - %d\n", 
                purge? "purge" : "upper bound", file->name, rc);
        return CL5_RUV_ERROR;
    }    

    entry.op = &op;
    rc = _cl5GetFirstEntry (obj, &entry, &iterator, NULL);
    while (rc == CL5_SUCCESS)
    {
        if(op.csn){
            rid = csn_get_replicaid (op.csn);
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "_cl5ConstructRUV: "
                "Operation missing csn, moving on to next entry.\n");
            cl5_operation_parameters_done (&op);
            rc = _cl5GetNextEntry (&entry, iterator);
            continue;
        }
        if(is_cleaned_rid(rid)){
            /* skip this entry as the rid is invalid */
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUV: "
                "skipping entry because its csn contains a cleaned rid(%d)\n", rid);
            cl5_operation_parameters_done (&op);
            rc = _cl5GetNextEntry (&entry, iterator);
            continue;
        }
        if (purge)
            rc = ruv_set_csns_keep_smallest(file->purgeRUV, op.csn); 
        else
            rc = ruv_set_csns (file->maxRUV, op.csn, NULL);

        cl5_operation_parameters_done (&op);
        if (rc != RUV_SUCCESS)
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUV: "
				"failed to updated %s RUV for file %s; ruv error - %d\n", 
                purge ? "purge" : "upper bound", file->name, rc);
            rc = CL5_RUV_ERROR;
            continue;
        }
 
        rc = _cl5GetNextEntry (&entry, iterator);
    }

    cl5_operation_parameters_done (&op);

    if (iterator)
        cl5DestroyIterator (iterator);

    if (rc == CL5_NOTFOUND)
    {
        rc = CL5_SUCCESS;
    }
    else
    {
        if (purge)
            ruv_destroy (&file->purgeRUV);
        else
            ruv_destroy (&file->maxRUV);
    }

    return rc;
}

static int _cl5UpdateRUV (Object *obj, CSN *csn, PRBool newReplica, PRBool purge)
{
    ReplicaId rid;
    int rc = RUV_SUCCESS; /* initialize rc to avoid erroneous logs */
    CL5DBFile *file;

    PR_ASSERT (obj && csn);

    file = (CL5DBFile*)object_get_data (obj);

    /*
     *  if purge is TRUE, file->purgeRUV must be set;
     *  if purge is FALSE, maxRUV must be set
     */
    PR_ASSERT (file && ((purge && file->purgeRUV) || (!purge && file->maxRUV)));
    rid = csn_get_replicaid(csn);

    /* update vector only if this replica is not yet part of RUV */
    if (purge && newReplica)
    {
        if (ruv_contains_replica (file->purgeRUV, rid)){
            return CL5_SUCCESS;
        } else {
            /* if the replica is not part of the purgeRUV yet, add it unless it's from a cleaned rid */
            ruv_add_replica (file->purgeRUV, rid, multimaster_get_local_purl());
        }
    }
    else
    {
        if (purge){
            rc = ruv_set_csns(file->purgeRUV, csn, NULL);
        } else {
            rc = ruv_set_csns(file->maxRUV, csn, NULL);
        }
    }
 
    if (rc != RUV_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5UpdatePurgeRUV: "
				"failed to update %s RUV for file %s; ruv error - %d\n", 
                purge ? "purge" : "upper bound", file->name, rc);
        return CL5_RUV_ERROR;
    }

    return CL5_SUCCESS;
}

static int _cl5EnumConsumerRUV (const ruv_enum_data *element, void *arg)
{
    int rc;
    RUV *ruv;
    CSN *csn = NULL;

    PR_ASSERT (element && element->csn && arg);

    ruv = (RUV*)arg;

    rc = ruv_get_largest_csn_for_replica(ruv, csn_get_replicaid (element->csn), &csn);
    if (rc != RUV_SUCCESS || csn == NULL || csn_compare (element->csn, csn) < 0)
    {
        ruv_set_max_csn(ruv, element->csn, NULL);
    }

    if (csn)
        csn_free (&csn);
  
    return 0;   
}

static int _cl5GetRUV2Purge2 (Object *fileObj, RUV **ruv)
{
    int rc = CL5_SUCCESS;
    CL5DBFile *dbFile;
    Object *rObj = NULL;
    Replica *r = NULL;
    Object *agmtObj = NULL;
    Repl_Agmt *agmt;
    Object *consRUVObj, *supRUVObj;
    RUV *consRUV, *supRUV;
    CSN *csn;

    PR_ASSERT (fileObj && ruv);

    if (!ruv) {
        rc = CL5_UNKNOWN_ERROR;
        goto done;
    }

    dbFile = (CL5DBFile*)object_get_data (fileObj);
    PR_ASSERT (dbFile);
    
    rObj = replica_get_by_name (dbFile->replName);
    PR_ASSERT (rObj);

    if (!rObj) {
        rc = CL5_NOTFOUND;
        goto done;
    }

    r = (Replica*)object_get_data (rObj);
    PR_ASSERT (r);

    /* We start with this replica's RUV. See note in _cl5DoTrimming */
    supRUVObj = replica_get_ruv (r);
    PR_ASSERT (supRUVObj);

    supRUV = (RUV*)object_get_data (supRUVObj);
    PR_ASSERT (supRUV);

    *ruv = ruv_dup (supRUV);

    object_release (supRUVObj);

    agmtObj = agmtlist_get_first_agreement_for_replica (r);
    while (agmtObj)
    {
        agmt = (Repl_Agmt*)object_get_data (agmtObj);
        PR_ASSERT (agmt);

        if(!agmt_is_enabled(agmt)){
        	agmtObj = agmtlist_get_next_agreement_for_replica(r, agmtObj);
        	continue;
        }

        consRUVObj = agmt_get_consumer_ruv (agmt);        
        if (consRUVObj)
        {
            consRUV = (RUV*)object_get_data (consRUVObj);
            rc = ruv_enumerate_elements (consRUV, _cl5EnumConsumerRUV, *ruv);
            if (rc != RUV_SUCCESS)
            {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetRUV2Purge2: "
				       "failed to construct ruv; ruv error - %d\n", rc);
                rc = CL5_RUV_ERROR;
                object_release (consRUVObj);
                object_release (agmtObj);
                break;
            }

            object_release (consRUVObj);
        }

        agmtObj = agmtlist_get_next_agreement_for_replica (r, agmtObj);
    }

    /* check if there is any data in the constructed ruv - otherwise get rid of it */
    if (ruv_get_max_csn(*ruv, &csn) != RUV_SUCCESS || csn == NULL)
    {
        ruv_destroy (ruv);
    }
    else
    {
        csn_free (&csn);
    }
done:
    if (rObj)
        object_release (rObj);

    if (rc != CL5_SUCCESS && ruv)
        ruv_destroy (ruv);    

    return rc;       
}

static int _cl5GetEntryCount (CL5DBFile *file)
{
	int rc;
	char csnStr [CSN_STRSIZE];
	DBT key={0}, data={0};
	DB_BTREE_STAT *stats = NULL;

	PR_ASSERT (file);

	/* read entry count. if the entry is there - the file was successfully closed
       last time it was used */
	key.data = _cl5GetHelperEntryKey (ENTRY_COUNT_TIME, csnStr);
	key.size = CSN_STRSIZE;

	data.flags = DB_DBT_MALLOC;

	rc = file->db->get(file->db, NULL/*txn*/, &key, &data, 0);
	switch (rc)
	{
		case 0:				file->entryCount = *(int*)data.data;
							slapi_ch_free (&(data.data));

							/* delete the entry. the entry is re-added when file
							   is successfully closed */
							file->db->del (file->db, NULL, &key, DEFAULT_DB_OP_FLAGS(NULL));
                            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
									"_cl5GetEntryCount: %d changes for replica %s\n", 
                                    file->entryCount, file->replName);
							return CL5_SUCCESS;

		case DB_NOTFOUND:	file->entryCount = 0;

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4300
                            rc = file->db->stat(file->db, NULL, (void*)&stats, 0);
#elif 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
                            rc = file->db->stat(file->db, (void*)&stats, 0);
#else
                            rc = file->db->stat(file->db, (void*)&stats, (void *)slapi_ch_malloc, 0);
#endif
							if (rc != 0)
							{
								slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
									"_cl5GetEntryCount: failed to get changelog statistics; "
									"db error - %d %s\n", rc, db_strerror(rc));
								return CL5_DB_ERROR;
							}

#ifdef DB30
							file->entryCount = stats->bt_nrecs;
#else	 /* DB31 */
							file->entryCount = stats->bt_ndata;
#endif
                            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
									"_cl5GetEntryCount: %d changes for replica %s\n", 
                                    file->entryCount, file->replName);

							slapi_ch_free ((void **)&stats);                             
							return CL5_SUCCESS;
		
		default:			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
								"_cl5GetEntryCount: failed to get count entry; "
								"db error - %d %s\n", rc, db_strerror(rc));
							return CL5_DB_ERROR;
	}	
}

static int _cl5WriteEntryCount (CL5DBFile *file)
{
	int rc;
	DBT key={0}, data={0};
	char csnStr [CSN_STRSIZE];
	DB_TXN *txnid = NULL;

	key.data = _cl5GetHelperEntryKey (ENTRY_COUNT_TIME, csnStr);
	key.size = CSN_STRSIZE;
	data.data = (void*)&file->entryCount;
	data.size = sizeof (file->entryCount);

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
	rc = txn_begin(s_cl5Desc.dbEnv, NULL, &txnid, 0);
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"_cl5WriteEntryCount: failed to begin transaction; db error - %d %s\n",
						rc, db_strerror(rc));
		return CL5_DB_ERROR;
	}
#endif
	rc = file->db->put(file->db, txnid, &key, &data, DEFAULT_DB_OP_FLAGS(txnid));
	if (rc == 0)
	{
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
		rc = txn_commit (txnid, 0);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5WriteEntryCount: failed to commit transaction; db error - %d %s\n", 
						rc, db_strerror(rc));
			return CL5_DB_ERROR;
		}
#endif
		return CL5_SUCCESS;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
				"_cl5WriteEntryCount: "
				"failed to write count entry for file %s; db error - %d %s\n", 
				file->name, rc, db_strerror(rc));
		if (CL5_OS_ERR_IS_DISKFULL(rc))
		{
			cl5_set_diskfull();
			return CL5_DB_ERROR;
		}
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
		rc = txn_abort (txnid);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
							"_cl5WriteEntryCount: failed to abort transaction; db error - %d %s\n",
							rc, db_strerror(rc));
		}
#endif
		return CL5_DB_ERROR;
	}	
}

static const char* _cl5OperationType2Str (int type)
{
	switch (type)
	{
		case SLAPI_OPERATION_ADD:		return T_ADDCTSTR;
		case SLAPI_OPERATION_MODIFY:	return T_MODIFYCTSTR;
		case SLAPI_OPERATION_MODRDN:	return T_MODRDNCTSTR;
		case SLAPI_OPERATION_DELETE:	return T_DELETECTSTR;		
		default:						return NULL;
	}
}

static int _cl5Str2OperationType (const char *str)
{
	if (strcasecmp (str, T_ADDCTSTR) == 0)
		return SLAPI_OPERATION_ADD;

	if (strcasecmp (str, T_MODIFYCTSTR) == 0)
		return SLAPI_OPERATION_MODIFY;

	if (strcasecmp (str, T_MODRDNCTSTR) == 0)
		return SLAPI_OPERATION_MODRDN;

	if (strcasecmp (str, T_DELETECTSTR) == 0)
		return SLAPI_OPERATION_DELETE;

	return -1;
}

static int _cl5Operation2LDIF (const slapi_operation_parameters *op, const char *replGen,
                               char **ldifEntry, PRInt32 *lenLDIF)
{
	int len = 2;
	lenstr *l = NULL;
	const char *strType;
	const char *strDeleteOldRDN = "false";
	char *buff, *start;
	LDAPMod **add_mods;
	char *rawDN = NULL;
	char strCSN[CSN_STRSIZE];		

	PR_ASSERT (op && replGen && ldifEntry && IsValidOperation (op));

	strType = _cl5OperationType2Str (op->operation_type);
	csn_as_string(op->csn,PR_FALSE,strCSN);
	
	/* find length of the buffer */	
	len += LDIF_SIZE_NEEDED(strlen (T_CHANGETYPESTR), strlen (strType));
	len += LDIF_SIZE_NEEDED(strlen (T_REPLGEN), strlen (replGen));
	len += LDIF_SIZE_NEEDED(strlen (T_CSNSTR),  strlen (strCSN));
	len += LDIF_SIZE_NEEDED(strlen (T_UNIQUEIDSTR), strlen (op->target_address.uniqueid));

	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:	if (NULL == op->p.p_add.target_entry) {
										slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
											"_cl5Operation2LDIF(ADD): entry is NULL\n");
										return CL5_BAD_FORMAT;
									}
									if (op->p.p_add.parentuniqueid)
										len += LDIF_SIZE_NEEDED(strlen (T_PARENTIDSTR), 
																strlen (op->p.p_add.parentuniqueid));
									slapi_entry2mods (op->p.p_add.target_entry, &rawDN, &add_mods);				
									len += LDIF_SIZE_NEEDED(strlen (T_DNSTR), strlen (rawDN));
									l = make_changes_string(add_mods, NULL);
									len += LDIF_SIZE_NEEDED(strlen (T_CHANGESTR), l->ls_len);
									ldap_mods_free (add_mods, 1);
									break;

		case SLAPI_OPERATION_MODIFY: if (NULL == op->p.p_modify.modify_mods) {
										slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
											"_cl5Operation2LDIF(MODIFY): mods are NULL\n");
										return CL5_BAD_FORMAT;
									 }
									 len += LDIF_SIZE_NEEDED(strlen (T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
									 l = make_changes_string(op->p.p_modify.modify_mods, NULL);
									 len += LDIF_SIZE_NEEDED(strlen (T_CHANGESTR), l->ls_len);
									 break;

		case SLAPI_OPERATION_MODRDN: if (NULL == op->p.p_modrdn.modrdn_mods) {
										slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
											"_cl5Operation2LDIF(MODRDN): mods are NULL\n");
										return CL5_BAD_FORMAT;
									 }
									 len += LDIF_SIZE_NEEDED(strlen (T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
									 len += LDIF_SIZE_NEEDED(strlen (T_NEWRDNSTR), 
															 strlen (op->p.p_modrdn.modrdn_newrdn));
									 strDeleteOldRDN = (op->p.p_modrdn.modrdn_deloldrdn ? "true" : "false");
									 len += LDIF_SIZE_NEEDED(strlen (T_DRDNFLAGSTR),
															 strlen (strDeleteOldRDN));
									 if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
										len += LDIF_SIZE_NEEDED(strlen (T_NEWSUPERIORDNSTR),
													REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address));
									 if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)
										len += LDIF_SIZE_NEEDED(strlen (T_NEWSUPERIORIDSTR),
													strlen (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid));
									 l = make_changes_string(op->p.p_modrdn.modrdn_mods, NULL);
									 len += LDIF_SIZE_NEEDED(strlen (T_CHANGESTR), l->ls_len);
									 break;		  

		case SLAPI_OPERATION_DELETE: if (NULL == REPL_GET_DN(&op->target_address)) {
										slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
											"_cl5Operation2LDIF(DELETE): target dn is NULL\n");
										return CL5_BAD_FORMAT;
									 }
									 len += LDIF_SIZE_NEEDED(strlen (T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
									 break;	
		
		default:					 slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
									"_cl5Operation2LDIF: invalid operation type - %lu\n", op->operation_type);

									 return CL5_BAD_FORMAT;
	}

	/* allocate buffer */
	buff = slapi_ch_malloc (len);
	start = buff;
	if (buff == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5Operation2LDIF: memory allocation failed\n");
		return CL5_MEMORY_ERROR;
	}

	/* fill buffer */
	slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGETYPESTR, (char*)strType, strlen (strType), 0);
	slapi_ldif_put_type_and_value_with_options(&buff, T_REPLGEN, (char*)replGen, strlen (replGen), 0);
	slapi_ldif_put_type_and_value_with_options(&buff, T_CSNSTR, (char*)strCSN, strlen (strCSN), 0);
	slapi_ldif_put_type_and_value_with_options(&buff, T_UNIQUEIDSTR, op->target_address.uniqueid, 
							strlen (op->target_address.uniqueid), 0);

	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:		if (op->p.p_add.parentuniqueid)
											slapi_ldif_put_type_and_value_with_options(&buff, T_PARENTIDSTR, 
												op->p.p_add.parentuniqueid, strlen (op->p.p_add.parentuniqueid), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, rawDN, strlen (rawDN), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
										slapi_ch_free ((void**)&rawDN);
										break;

		case SLAPI_OPERATION_MODIFY:	slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address), 
																REPL_GET_DN_LEN(&op->target_address), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
										break;

		case SLAPI_OPERATION_MODRDN:	slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address), 
																REPL_GET_DN_LEN(&op->target_address), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_NEWRDNSTR, op->p.p_modrdn.modrdn_newrdn,
						  										strlen (op->p.p_modrdn.modrdn_newrdn), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_DRDNFLAGSTR, strDeleteOldRDN, 
																strlen (strDeleteOldRDN), 0);
										if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
											slapi_ldif_put_type_and_value_with_options(&buff, T_NEWSUPERIORDNSTR, 
																REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address),
																REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address), 0);
										if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)							
											slapi_ldif_put_type_and_value_with_options(&buff, T_NEWSUPERIORIDSTR, 
																op->p.p_modrdn.modrdn_newsuperior_address.uniqueid, 
																strlen (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid), 0);
										slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
										break;	

		case SLAPI_OPERATION_DELETE:	slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address), 
										REPL_GET_DN_LEN(&op->target_address), 0);
										break;	  
	}

	*buff = '\n';
	buff ++;
	*buff = '\0';
	
	*ldifEntry = start;
	*lenLDIF = buff - start;

	if (l)
		lenstr_free(&l);

	return CL5_SUCCESS;	
}

static int
_cl5LDIF2Operation (char *ldifEntry, slapi_operation_parameters *op, char **replGen)
{
	int rc;
	int rval = CL5_BAD_FORMAT;
	char *next, *line;
	struct berval type, value;
	struct berval bv_null = {0, NULL};
	int freeval = 0;
	Slapi_Mods *mods;
	char *rawDN = NULL;
	char *ldifEntryWork = slapi_ch_strdup(ldifEntry);

	PR_ASSERT (op && ldifEntry && replGen);
	
	memset (op, 0, sizeof (*op));
	
	next = ldifEntryWork;
	while ((line = ldif_getline(&next)) != NULL) 
	{
		if ( *line == '\n' || *line == '\0' ) 
		{
			break;
		}

		/* this call modifies ldifEntry */
		type = bv_null;
		value = bv_null;
		rc = slapi_ldif_parse_line(line, &type, &value, &freeval);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5LDIF2Operation: warning - failed to parse ldif line\n");
			continue;
		}
		if (strncasecmp (type.bv_val, T_CHANGETYPESTR,
			             strlen(T_CHANGETYPESTR)>type.bv_len?
						 strlen(T_CHANGETYPESTR):type.bv_len) == 0)
		{
			op->operation_type = _cl5Str2OperationType (value.bv_val);	
		}
		else if (strncasecmp (type.bv_val, T_REPLGEN, type.bv_len) == 0)
        {
            *replGen = slapi_ch_strdup (value.bv_val);
        }
		else if (strncasecmp (type.bv_val, T_CSNSTR, type.bv_len) == 0)
		{
			op->csn = csn_new_by_string(value.bv_val);
		}
		else if (strncasecmp (type.bv_val, T_UNIQUEIDSTR, type.bv_len) == 0)
		{
			op->target_address.uniqueid = slapi_ch_strdup (value.bv_val);
		}
		else if (strncasecmp (type.bv_val, T_DNSTR, type.bv_len) == 0)
		{
			PR_ASSERT (op->operation_type);

			if (op->operation_type == SLAPI_OPERATION_ADD)
			{
				rawDN = slapi_ch_strdup (value.bv_val);
				op->target_address.sdn = slapi_sdn_new_dn_byval(rawDN);
			}
			else
				op->target_address.sdn = slapi_sdn_new_dn_byval(value.bv_val);
		}
		else if (strncasecmp (type.bv_val, T_PARENTIDSTR, type.bv_len) == 0)
		{
			op->p.p_add.parentuniqueid = slapi_ch_strdup (value.bv_val);	
		}
		else if (strncasecmp (type.bv_val, T_NEWRDNSTR, type.bv_len) == 0)
		{			
			op->p.p_modrdn.modrdn_newrdn = slapi_ch_strdup (value.bv_val);
		}
		else if (strncasecmp (type.bv_val, T_DRDNFLAGSTR, type.bv_len) == 0)
		{
			op->p.p_modrdn.modrdn_deloldrdn = (strncasecmp (value.bv_val, "true", value.bv_len) ? PR_FALSE : PR_TRUE);
		}
		else if (strncasecmp (type.bv_val, T_NEWSUPERIORDNSTR, type.bv_len) == 0)
		{
			op->p.p_modrdn.modrdn_newsuperior_address.sdn = slapi_sdn_new_dn_byval(value.bv_val);
		}		
		else if (strncasecmp (type.bv_val, T_NEWSUPERIORIDSTR, type.bv_len) == 0)
		{
			op->p.p_modrdn.modrdn_newsuperior_address.uniqueid = slapi_ch_strdup (value.bv_val);
		}
		else if (strncasecmp (type.bv_val, T_CHANGESTR,
			                  strlen(T_CHANGESTR)>type.bv_len?
						      strlen(T_CHANGESTR):type.bv_len) == 0)
		{
			PR_ASSERT (op->operation_type);

			switch (op->operation_type)
			{
				case SLAPI_OPERATION_ADD:		/* 
												 * When it comes here, case T_DNSTR is already
												 * passed and rawDN is supposed to set.
												 * But it's a good idea to make sure it is
												 * not NULL.
												 */ 
												if (NULL == rawDN) {
													slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
																"_cl5LDIF2Operation: corrupted format "
																"for operation type - %lu\n", 
																 op->operation_type);
													slapi_ch_free_string(&ldifEntryWork);
													return CL5_BAD_FORMAT;
												}
												mods = parse_changes_string(value.bv_val);
												PR_ASSERT (mods);
												slapi_mods2entry (&(op->p.p_add.target_entry), rawDN, 
																  slapi_mods_get_ldapmods_byref(mods));
												slapi_ch_free ((void**)&rawDN);
												slapi_mods_free (&mods);
												break;
												
				case SLAPI_OPERATION_MODIFY:	mods = parse_changes_string(value.bv_val);
												PR_ASSERT (mods);
												op->p.p_modify.modify_mods = slapi_mods_get_ldapmods_passout (mods);
												slapi_mods_free (&mods);
												break;

				case SLAPI_OPERATION_MODRDN:	mods = parse_changes_string(value.bv_val);
												PR_ASSERT (mods);
												op->p.p_modrdn.modrdn_mods = slapi_mods_get_ldapmods_passout (mods);
												slapi_mods_free (&mods);
												break;	

				default:						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
																"_cl5LDIF2Operation: invalid operation type - %lu\n", 
																 op->operation_type);
												if (freeval) {
													slapi_ch_free_string(&value.bv_val);
												}
												slapi_ch_free_string(&ldifEntryWork);
												return CL5_BAD_FORMAT;
			}
		}	
		if (freeval) {
			slapi_ch_free_string(&value.bv_val);
		}
	}

	if ((0 != strncmp(ldifEntryWork, "clpurgeruv", 10)) && /* skip RUV; */
	    (0 != strncmp(ldifEntryWork, "clmaxruv", 8)))      /* RUV has NULL op */
	{
		if (IsValidOperation (op))
		{
			rval = CL5_SUCCESS;
		}
		else
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			                "_cl5LDIF2Operation: invalid data format\n");
		}
	}
	slapi_ch_free_string(&ldifEntryWork);
	return rval;
}

static int _cl5WriteOperationTxn(const char *replName, const char *replGen, 
                                 const slapi_operation_parameters *op, PRBool local, void *txn)
{
	int rc;
	int cnt;
	DBT key={0};
	DBT * data=NULL;
	char csnStr [CSN_STRSIZE];
	PRIntervalTime interval;
	CL5Entry entry;
	CL5DBFile *file = NULL;
	Object *file_obj = NULL;
	DB_TXN *txnid = NULL;
	DB_TXN *parent_txnid = (DB_TXN *)txn;

	rc = _cl5GetDBFileByReplicaName (replName, replGen, &file_obj);
	if (rc == CL5_NOTFOUND)
	{
		rc = _cl5DBOpenFileByReplicaName (replName, replGen, &file_obj, 
                                          PR_TRUE /* check for duplicates */);
		if (rc != CL5_SUCCESS)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5WriteOperationTxn: failed to find or open DB object for replica %s\n", replName);
			return rc;
		}
	}
	else if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5WriteOperationTxn: failed to get db file for target dn (%s)", 
						REPL_GET_DN(&op->target_address));
		return CL5_OBJSET_ERROR;
	}

	/* assign entry time - used for trimming */
	entry.time = current_time (); 
	entry.op = (slapi_operation_parameters *)op;

	/* construct the key */
	key.data = csn_as_string(op->csn, PR_FALSE, csnStr);
	key.size = CSN_STRSIZE;

	/* construct the data */
	data = (DBT *) slapi_ch_calloc(1, sizeof(DBT));
	rc = _cl5Entry2DBData (&entry, (char**)&data->data, &data->size);
	if (rc != CL5_SUCCESS)
	{
		char s[CSN_STRSIZE];		
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5WriteOperationTxn: failed to convert entry with csn (%s) "
                        "to db format\n", csn_as_string(op->csn,PR_FALSE,s));
		goto done;
	}

	file = (CL5DBFile*)object_get_data (file_obj);
	PR_ASSERT (file);

	/* if this is part of ldif2cl - just write the entry without transaction */
	if (s_cl5Desc.dbOpenMode == CL5_OPEN_LDIF2CL)
	{
		rc = file->db->put(file->db, NULL, &key, data, 0);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"_cl5WriteOperationTxn: failed to write entry; db error - %d %s\n", 
				rc, db_strerror(rc));
			if (CL5_OS_ERR_IS_DISKFULL(rc))
			{
				cl5_set_diskfull();
			}
			rc = CL5_DB_ERROR;
		}
		goto done;
	}

	/* write the entry */
	rc = EAGAIN;
	cnt = 0;

	while ((rc == EAGAIN || rc == DB_LOCK_DEADLOCK) && cnt < MAX_TRIALS)
	{ 
		if (cnt != 0)
		{
#if USE_DB_TXN
			/* abort previous transaction */
			rc = TXN_ABORT (txnid);
			if (rc != 0)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
							"_cl5WriteOperationTxn: failed to abort transaction; db error - %d %s\n",
							rc, db_strerror(rc));
				rc = CL5_DB_ERROR;
				goto done;
			}
#endif
			/* back off */			
    		interval = PR_MillisecondsToInterval(slapi_rand() % 100);
    		DS_Sleep(interval);		
		}
#if USE_DB_TXN
		/* begin transaction */
		rc = TXN_BEGIN(s_cl5Desc.dbEnv, parent_txnid, &txnid, 0);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
						"_cl5WriteOperationTxn: failed to start transaction; db error - %d %s\n",
						rc, db_strerror(rc));
			rc = CL5_DB_ERROR;
			goto done;
		}
#endif

		if ( file->sema )
		{
			PR_WaitSemaphore(file->sema);
		}
		rc = file->db->put(file->db, txnid, &key, data, DEFAULT_DB_OP_FLAGS(txnid));
		if ( file->sema )
		{
			PR_PostSemaphore(file->sema);
		}
		if (CL5_OS_ERR_IS_DISKFULL(rc))
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
				"_cl5WriteOperationTxn: changelog (%s) DISK FULL; db error - %d %s\n",
				s_cl5Desc.dbDir, rc, db_strerror(rc));
			cl5_set_diskfull();
			rc = CL5_DB_ERROR;
			goto done;
		}
		if (cnt != 0)
		{
			if (rc == 0)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "_cl5WriteOperationTxn: retry (%d) the transaction (csn=%s) succeeded\n", cnt, (char*)key.data);
			}
			else if ((cnt + 1) >= MAX_TRIALS)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "_cl5WriteOperationTxn: retry (%d) the transaction (csn=%s) failed (rc=%d (%s))\n", cnt, (char*)key.data, rc, db_strerror(rc));
			}
		}
		cnt ++;
	}
    
	if (rc == 0) /* we successfully added entry */
	{
#if USE_DB_TXN
		rc = TXN_COMMIT (txnid, 0);
#endif
	}
	else	
	{
		char s[CSN_STRSIZE];		
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5WriteOperationTxn: failed to write entry with csn (%s); "
						"db error - %d %s\n", csn_as_string(op->csn,PR_FALSE,s), 
						rc, db_strerror(rc));
#if USE_DB_TXN
		rc = TXN_ABORT (txnid);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
							"_cl5WriteOperationTxn: failed to abort transaction; db error - %d %s\n",
							rc, db_strerror(rc));
		}
#endif
		rc = CL5_DB_ERROR;
		goto done;
	}

	/* update entry count - we assume that all entries are new */
	PR_AtomicIncrement (&file->entryCount);

    /* update purge vector if we have not seen any changes from this replica before */
    _cl5UpdateRUV (file_obj, op->csn, PR_TRUE, PR_TRUE);

	slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
			"cl5WriteOperationTxn: successfully written entry with csn (%s)\n", csnStr);
	rc = CL5_SUCCESS;
done:
	if (data->data)
		slapi_ch_free (&(data->data));
	slapi_ch_free((void**) &data);

	if (file_obj)
		object_release (file_obj);

	return rc;
}

static int _cl5WriteOperation(const char *replName, const char *replGen, 
                              const slapi_operation_parameters *op, PRBool local)
{
    return _cl5WriteOperationTxn(replName, replGen, op, local, NULL);
}

static int _cl5GetFirstEntry (Object *obj, CL5Entry *entry, void **iterator, DB_TXN *txnid)
{
	int rc;
	DBC *cursor = NULL;
	DBT	key={0}, data={0};
	CL5Iterator *it;
	CL5DBFile *file;

	PR_ASSERT (obj && entry && iterator);

	file = (CL5DBFile*)object_get_data (obj);
	PR_ASSERT (file);
	/* create cursor */
	rc = file->db->cursor(file->db, txnid, &cursor, 0);
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"_cl5GetFirstEntry: failed to create cursor; db error - %d %s\n", rc, db_strerror(rc));
		rc = CL5_DB_ERROR;
		goto done;
	}

	key.flags = DB_DBT_MALLOC;
	data.flags = DB_DBT_MALLOC;	
	while ((rc = cursor->c_get(cursor, &key, &data, DB_NEXT)) == 0) 
	{						
		/* skip service entries */
		if (cl5HelperEntry ((char*)key.data, NULL))
		{
			slapi_ch_free (&(key.data));
			slapi_ch_free (&(data.data));
			continue;
		}

		/* format entry */
		slapi_ch_free (&(key.data));
		rc = cl5DBData2Entry (data.data, data.size, entry);
		slapi_ch_free (&(data.data));
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
				"_cl5GetFirstOperation: failed to format entry: %d\n", rc);
			goto done;
		}

		it = (CL5Iterator*)slapi_ch_malloc (sizeof (CL5Iterator));
		it->cursor  = cursor;
		object_acquire (obj);
		it->file = obj;
		*(CL5Iterator**)iterator = it;

		return CL5_SUCCESS;
	}
	/*
	 * Bug 430172 - memory leaks after db "get" deadlocks, e.g. in CL5 trim
	 * Even when db->c_get() does not return success, memory may have been
	 * allocated in the DBT.  This seems to happen when DB_DBT_MALLOC was set, 
	 * the data being retrieved is larger than the page size, and we got 
	 * DB_LOCK_DEADLOCK. libdb allocates the memory and then finds itself 
	 * deadlocked trying to go through the overflow page list.  It returns 
	 * DB_LOCK_DEADLOCK which we've assumed meant that no memory was allocated 
	 * for the DBT.
	 *
	 * The following slapi_ch_free frees the memory only when the value is 
	 * non NULL, which is true if the situation described above occurs.
	 */
	slapi_ch_free ((void **)&key.data);
	slapi_ch_free ((void **)&data.data);

	/* walked of the end of the file */
	if (rc == DB_NOTFOUND)
	{
		rc = CL5_NOTFOUND;
		goto done;
	}

	/* db error occured while iterating */
	/* On this path, the condition "rc != 0" cannot be false */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"_cl5GetFirstEntry: failed to get entry; db error - %d %s\n",
				rc, db_strerror(rc));
	rc = CL5_DB_ERROR;

done:
	/* error occured */
	/* We didn't success in assigning this cursor to the iterator,
	 * so we need to free the cursor here */
	if (cursor)
		cursor->c_close(cursor);

	return rc;		
}

static int _cl5GetNextEntry (CL5Entry *entry, void *iterator)
{
	int rc;
	CL5Iterator *it;
	DBT	key={0}, data={0};

	PR_ASSERT (entry && iterator);

	it = (CL5Iterator*) iterator;

	key.flags = DB_DBT_MALLOC;
	data.flags = DB_DBT_MALLOC;
	while ((rc = it->cursor->c_get(it->cursor, &key, &data, DB_NEXT)) == 0)
	{
		if (cl5HelperEntry ((char*)key.data, NULL))
		{
			slapi_ch_free (&(key.data));
			slapi_ch_free (&(data.data));
			continue;
		}

		slapi_ch_free (&(key.data));
		/* format entry */
		rc = cl5DBData2Entry (data.data, data.size, entry);
		slapi_ch_free (&(data.data));
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
				"_cl5GetNextEntry: failed to format entry: %d\n", rc);
		}

		return rc;
	}
	/*
	 * Bug 430172 - memory leaks after db "get" deadlocks, e.g. in CL5 trim
	 * Even when db->c_get() does not return success, memory may have been
	 * allocated in the DBT.  This seems to happen when DB_DBT_MALLOC was set, 
	 * the data being retrieved is larger than the page size, and we got 
	 * DB_LOCK_DEADLOCK. libdb allocates the memory and then finds itself 
	 * deadlocked trying to go through the overflow page list.  It returns 
	 * DB_LOCK_DEADLOCK which we've assumed meant that no memory was allocated 
	 * for the DBT.
	 *
	 * The following slapi_ch_free frees the memory only when the value is 
	 * non NULL, which is true if the situation described above occurs.
	 */
	slapi_ch_free ((void **)&key.data);
	slapi_ch_free ((void **)&data.data);

	/* walked of the end of the file or entry is out of range */
	if (rc == 0 || rc == DB_NOTFOUND)
	{
		return CL5_NOTFOUND;
	}

	/* cursor operation failed */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"_cl5GetNextEntry: failed to get entry; db error - %d %s\n", 
			rc, db_strerror(rc));

	return CL5_DB_ERROR;
}

static int _cl5CurrentDeleteEntry (void *iterator)
{
	int rc;
	CL5Iterator *it;
    CL5DBFile *file;

    PR_ASSERT (iterator);

	it = (CL5Iterator*)iterator;

	rc = it->cursor->c_del (it->cursor, 0);

	if (rc == 0) {        
            /* decrement entry count */
            file = (CL5DBFile*)object_get_data (it->file);
            PR_AtomicDecrement (&file->entryCount);
            return CL5_SUCCESS;
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                            "_cl5CurrentDeleteEntry failed, err=%d %s\n", 
                            rc, db_strerror(rc));
	    /* We don't free(close) the cursor here, as the caller will free it by a call to cl5DestroyIterator */
	    /* Freeing it here is a potential bug, as the cursor can't be referenced later once freed */
            return CL5_DB_ERROR;
        }
}

static PRBool _cl5IsValidIterator (const CL5Iterator *iterator)
{
	return (iterator && iterator->cursor && iterator->file);
}

static int _cl5GetOperation (Object *replica, slapi_operation_parameters *op)
{
	int rc;
	DBT key={0}, data={0};
	CL5DBFile *file;
	CL5Entry entry;
	Object *obj = NULL;
	char csnStr[CSN_STRSIZE];		

	rc = _cl5GetDBFile (replica, &obj);
	if (rc != CL5_SUCCESS || !obj)
	{
		goto done;
	}

	file = (CL5DBFile*)object_get_data (obj);
	PR_ASSERT (file);

	/* construct the key */
	key.data = csn_as_string(op->csn, PR_FALSE, csnStr);
	key.size = CSN_STRSIZE;

	data.flags = DB_DBT_MALLOC;

	rc = file->db->get(file->db, NULL/*txn*/, &key, &data, 0);
	switch (rc)
	{
		case 0:				entry.op = op;
		/* Callers of this function should cl5_operation_parameters_done(op) */
							rc = cl5DBData2Entry (data.data, data.size, &entry);
							if (rc == CL5_SUCCESS)
							{
								slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
									"_cl5GetOperation: successfully retrieved operation with csn (%s)\n",
									csnStr);
							}
							else
							{
								slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
									"_cl5GetOperation: failed to convert db data to operation;"
                                    " csn - %s\n", csnStr);
							}
							goto done;

		case DB_NOTFOUND:	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
									"_cl5GetOperation: operation for csn (%s) is not found in db that should contain dn (%s)\n",
									csnStr, REPL_GET_DN(&op->target_address));
							rc = CL5_NOTFOUND;
							goto done;

		default:			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
								"_cl5GetOperation: failed to get entry for csn (%s); "
								"db error - %d %s\n", csnStr, rc, db_strerror(rc));
							rc = CL5_DB_ERROR;
							goto done;
	}

done:
	if (obj)
		object_release (obj);

	slapi_ch_free (&(data.data));
	
	return rc;
}

PRBool cl5HelperEntry (const char *csnstr, CSN *csnp)
{
	CSN *csn;
	time_t csnTime;
	PRBool retval = PR_FALSE;
	
	if (csnp)
	{
		csn = csnp;
	}
	else
	{
		csn= csn_new_by_string(csnstr);
	}
	if (csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
				"cl5HelperEntry: failed to get csn time; csn error\n");
		return PR_FALSE;
	}
	csnTime= csn_get_time(csn);

	if (csnTime == ENTRY_COUNT_TIME || csnTime == PURGE_RUV_TIME)
	{
		retval = PR_TRUE;
	}

	if (NULL == csnp)
		csn_free(&csn);
	return retval;
}

#ifdef FOR_DEBUGGING
/* Replay iteration helper functions */
static PRBool _cl5ValidReplayIterator (const CL5ReplayIterator *iterator)
{
	if (iterator == NULL || 
		iterator->consumerRuv == NULL || iterator->supplierRuvObj == NULL || 
        iterator->fileObj == NULL)
		return PR_FALSE;

	return PR_TRUE;
}
#endif

/* Algorithm: ONREPL!!!
 */
struct replica_hash_entry
{
    ReplicaId rid;       /* replica id */
    PRBool sendChanges;  /* indicates whether changes should be sent for this replica */
};


static int _cl5PositionCursorForReplay (ReplicaId consumerRID, const RUV *consumerRuv,
		Object *replica, Object *fileObj, CL5ReplayIterator **iterator)
{
	CLC_Buffer *clcache = NULL;
	CL5DBFile *file;
    int i;
    CSN **csns = NULL;
    CSN *startCSN = NULL;
    char csnStr [CSN_STRSIZE];
    int rc = CL5_SUCCESS;
    Object *supplierRuvObj = NULL;
    RUV *supplierRuv = NULL;
    PRBool newReplica;
    PRBool haveChanges = PR_FALSE;
	char *agmt_name;
	ReplicaId rid;

    PR_ASSERT (consumerRuv && replica && fileObj && iterator);
	csnStr[0] = '\0';

	file = (CL5DBFile*)object_get_data (fileObj);

    /* get supplier's RUV */
    supplierRuvObj = replica_get_ruv((Replica*)object_get_data(replica));
    PR_ASSERT (supplierRuvObj);

    if (!supplierRuvObj) {
        rc = CL5_UNKNOWN_ERROR;
        goto done;
    }

    supplierRuv = (RUV*)object_get_data (supplierRuvObj);            
    PR_ASSERT (supplierRuv);

	agmt_name = get_thread_private_agmtname();

	if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
		slapi_log_error(SLAPI_LOG_REPL, NULL, "_cl5PositionCursorForReplay (%s): Consumer RUV:\n", agmt_name);
		ruv_dump (consumerRuv, agmt_name, NULL);
		slapi_log_error(SLAPI_LOG_REPL, NULL, "_cl5PositionCursorForReplay (%s): Supplier RUV:\n", agmt_name);
		ruv_dump (supplierRuv, agmt_name, NULL);
	}
   
	/*
	 * get the sorted list of SupplierMinCSN (if no ConsumerMaxCSN)
	 * and ConsumerMaxCSN for those RIDs where consumer is not
	 * up-to-date.
	 */
    csns = cl5BuildCSNList (consumerRuv, supplierRuv);
    if (csns == NULL)
    {
        rc = CL5_NOTFOUND;
        goto done;
    }

	/* iterate over elements of consumer's (and/or supplier's) ruv */
    for (i = 0; csns[i]; i++)
    {
        CSN *consumerMaxCSN = NULL;

		rid = csn_get_replicaid(csns[i]);

		/*
		 * Skip CSN that is originated from the consumer.
		 * If RID==65535, the CSN is originated from a
		 * legacy consumer. In this case the supplier
		 * and the consumer may have the same RID.
		 */
		if ((rid == consumerRID && rid != MAX_REPLICA_ID) || (is_cleaned_rid(rid)) )
			continue;

        startCSN = csns[i];

		rc = clcache_get_buffer ( &clcache, file->db, consumerRID, consumerRuv, supplierRuv );
		if ( rc != 0 ) goto done;

	    /* This is the first loading of this iteration. For replicas
		 * already known to the consumer, we exclude the last entry
		 * sent to the consumer by using DB_NEXT. However, for
		 * replicas new to the consumer, we include the first change
		 * ever generated by that replica.
		 */
		newReplica = ruv_get_largest_csn_for_replica (consumerRuv, rid, &consumerMaxCSN);
		csn_free(&consumerMaxCSN);
		rc = clcache_load_buffer (clcache, startCSN, (newReplica ? DB_SET : DB_NEXT));

		/* there is a special case which can occur just after migration - in this case,
		the consumer RUV will contain the last state of the supplier before migration,
		but the supplier will have an empty changelog, or the supplier changelog will
		not contain any entries within the consumer min and max CSN - also, since
		the purge RUV contains no CSNs, the changelog has never been purged
		ASSUMPTIONS - it is assumed that the supplier had no pending changes to send
		to any consumers; that is, we can assume that no changes were lost due to
		either changelog purging or database reload - bug# 603061 - richm@netscape.com
		*/
        if ((rc == DB_NOTFOUND) && !ruv_has_csns(file->purgeRUV))
        {
            /* use the supplier min csn for the buffer start csn - we know
               this csn is in our changelog */
            if ((RUV_SUCCESS == ruv_get_min_csn(supplierRuv, &startCSN)) &&
                startCSN)
            { /* must now free startCSN */
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    csn_as_string(startCSN, PR_FALSE, csnStr); 
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
                                    "%s: CSN %s not found and no purging, probably a reinit\n",
                                    agmt_name, csnStr);
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
                                    "%s: Will try to use supplier min CSN %s to load changelog\n",
                                    agmt_name, csnStr);
                }
                rc = clcache_load_buffer (clcache, startCSN, DB_SET);
            }
            else
            {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    csn_as_string(startCSN, PR_FALSE, csnStr); 
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
                                    "%s: CSN %s not found and no purging, probably a reinit\n",
                                    agmt_name, csnStr);
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                                    "%s: Could not get the min csn from the supplier RUV\n",
                                    agmt_name);
                }
                rc = CL5_RUV_ERROR;
                goto done;
            }
        }

        if (rc == 0) {
            haveChanges = PR_TRUE;
            rc = CL5_SUCCESS;
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                csn_as_string(startCSN, PR_FALSE, csnStr); 
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
                                "%s: CSN %s found, position set for replay\n", agmt_name, csnStr);
            }
            if (startCSN != csns[i]) {
                csn_free(&startCSN);
            }
            break;
        }
        else if (rc == DB_NOTFOUND)  /* entry not found */
        {
            /* check whether this csn should be present */
            rc = _cl5CheckMissingCSN (startCSN, supplierRuv, file);
            if (rc == CL5_MISSING_DATA)  /* we should have had the change but we don't */
            {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    csn_as_string(startCSN, PR_FALSE, csnStr); 
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
                                    "%s: CSN %s not found, seems to be missing\n", agmt_name, csnStr);
                }
            }
            else /* we are not as up to date or we purged */
            {
                csn_as_string(startCSN, PR_FALSE, csnStr); 
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                                "%s: CSN %s not found, we aren't as up to date, or we purged\n", 
                                agmt_name, csnStr);
            }
            if (startCSN != csns[i]) {
                csn_free(&startCSN);
            }
            if (rc == CL5_MISSING_DATA)  /* we should have had the change but we don't */
            {
                break;
            }
            else /* we are not as up to date or we purged */
            {
                continue;
            } 
        }
        else
        {
            csn_as_string(startCSN, PR_FALSE, csnStr); 
            /* db error */
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                            "%s: Failed to retrieve change with CSN %s; db error - %d %s\n", 
                            agmt_name, csnStr, rc, db_strerror(rc));
            if (startCSN != csns[i]) {
                csn_free(&startCSN);
            }

            rc = CL5_DB_ERROR;
            break;
        }

    } /* end for */

    /* setup the iterator */
    if (haveChanges)
    {
	    *iterator = (CL5ReplayIterator*) slapi_ch_calloc (1, sizeof (CL5ReplayIterator));

	    if (*iterator == NULL)
	    {
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"%s: _cl5PositionCursorForReplay: failed to allocate iterator\n", agmt_name);
		    rc = CL5_MEMORY_ERROR;
		    goto done;
	    }

        /* ONREPL - should we make a copy of both RUVs here ?*/
		(*iterator)->fileObj = fileObj;
		(*iterator)->clcache = clcache; clcache = NULL;
		(*iterator)->consumerRID = consumerRID;
	    (*iterator)->consumerRuv = consumerRuv;
        (*iterator)->supplierRuvObj = supplierRuvObj;
    }
    else if (rc == CL5_SUCCESS)
    {
        /* we have no changes to send */
        rc = CL5_NOTFOUND;
    }

done:
	if ( clcache )
		clcache_return_buffer ( &clcache );

    if (csns)
        cl5DestroyCSNList (&csns);

    if (rc != CL5_SUCCESS)
    {
        if (supplierRuvObj)
            object_release (supplierRuvObj);
    }

    return rc;    
}

struct ruv_it
{
    CSN **csns; /* csn list */
    int alloc;  /* allocated size */
    int pos;    /* position in the list */
};

static int ruv_consumer_iterator (const ruv_enum_data *enum_data, void *arg)
{
    struct ruv_it *data = (struct ruv_it*)arg;

    PR_ASSERT (data);

    /* check if we have space for one more element */
    if (data->pos >= data->alloc - 2)            
    {
        data->alloc += 4;
        data->csns = (CSN**) slapi_ch_realloc ((void*)data->csns, data->alloc * sizeof (CSN*));    
    }

    data->csns [data->pos] = csn_dup (enum_data->csn);
    data->pos ++;

    return 0;
}


static int ruv_supplier_iterator (const ruv_enum_data *enum_data, void *arg)
{
	int i;
	PRBool found = PR_FALSE;
	ReplicaId rid;
	struct ruv_it *data = (struct ruv_it*)arg;

	PR_ASSERT (data);

	rid = csn_get_replicaid (enum_data->min_csn);
	/* check if the replica that generated the csn is already in the list */
	for (i = 0; i < data->pos; i++)
	{
		if (rid == csn_get_replicaid (data->csns[i]))
		{
			found = PR_TRUE;

			/* remove datacsn[i] if it is greater or equal to the supplier's maxcsn */
			if ( csn_compare ( data->csns[i], enum_data->csn ) >= 0 )
			{
				int j;

				csn_free ( & data->csns[i] );
				for (j = i+1; j < data->pos; j++)
				{
					data->csns [j-1] = data->csns [j];
				}
				data->pos --;
			}
			break;
		}
	}

	if (!found)
	{
		/* check if we have space for one more element */
		if (data->pos >= data->alloc - 2)
		{
			data->alloc += 4;
			data->csns = (CSN**)slapi_ch_realloc ((void*)data->csns,
					data->alloc * sizeof (CSN*));
		}

		data->csns [data->pos] = csn_dup (enum_data->min_csn);
		data->pos ++;
	}
	return 0;
}



static int
my_csn_compare(const void *arg1, const void *arg2)
{
	return(csn_compare(*((CSN **)arg1), *((CSN **)arg2)));
}



/* builds CSN ordered list of all csns in the RUV */
CSN** cl5BuildCSNList (const RUV *consRuv, const RUV *supRuv)
{
    struct ruv_it data;
    int       count, rc;
    CSN       **csns;

    PR_ASSERT (consRuv);

    count = ruv_replica_count (consRuv);
    csns = (CSN**)slapi_ch_calloc (count + 1, sizeof (CSN*));
    
    data.csns = csns;
    data.alloc = count + 1;
    data.pos = 0;
    
	/* add consumer elements to the list */
    rc = ruv_enumerate_elements (consRuv, ruv_consumer_iterator, &data);
	if (rc == 0 && supRuv)
	{
		/* add supplier elements to the list */
		rc = ruv_enumerate_elements (supRuv, ruv_supplier_iterator, &data);
	}

    /* we have no csns */
    if (data.csns[0] == NULL)
    {
		/* csns might have been realloced in ruv_supplier_iterator() */
        slapi_ch_free ((void**)&data.csns);
		csns = NULL;
    }
    else
    {
        csns = data.csns;
        data.csns [data.pos] = NULL;
        if (rc == 0)
        {        
            qsort (csns, data.pos, sizeof (CSN*), my_csn_compare);
        }
        else
        {
            cl5DestroyCSNList (&csns);
        }
    }

    return csns;
}

void cl5DestroyCSNList (CSN*** csns)
{
    if (csns && *csns)
    {
        int i;

        for (i = 0; (*csns)[i]; i++)
        {
            csn_free (&(*csns)[i]);
        }

        slapi_ch_free ((void**)csns);
    }
}

/* A csn should be in the changelog if it is larger than purge vector csn for the same
   replica and is smaller than the csn in supplier's ruv for the same replica.
   The functions returns
        CL5_PURGED      if data was purged from the changelog or was never logged
                        because it was loaded as part of replica initialization
        CL5_MISSING     if the data erouneously missing
        CL5_SUCCESS     if that has not and should not been seen by the server
 */
static int _cl5CheckMissingCSN (const CSN *csn, const RUV *supplierRuv, CL5DBFile *file)
{
    ReplicaId rid;
    CSN *supplierCsn = NULL;
    CSN *purgeCsn = NULL;
    int rc = CL5_SUCCESS;
    char csnStr [CSN_STRSIZE];

    PR_ASSERT (csn && supplierRuv && file);

    rid = csn_get_replicaid (csn);
    ruv_get_largest_csn_for_replica (supplierRuv, rid, &supplierCsn);
    if (supplierCsn == NULL)
    {
        /* we have not seen any changes from this replica so it is
           ok not to have this csn */
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN: "
                            "can't locate %s csn: we have not seen any changes for replica %d\n",
                            csn_as_string (csn, PR_FALSE, csnStr), rid);
        }
        return CL5_SUCCESS;
    }

    ruv_get_largest_csn_for_replica (file->purgeRUV, rid, &purgeCsn);
    if (purgeCsn == NULL)
    {
        /* changelog never contained any changes for this replica */
        if (csn_compare (csn, supplierCsn) <= 0)
        {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN: "
                                "the change with %s csn was never logged because it was imported "
                                "during replica initialization\n", csn_as_string (csn, PR_FALSE, csnStr));
            }
            rc = CL5_PURGED_DATA; /* XXXggood is that the correct return value? */
        }
        else
        {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN: "
                                "change with %s csn has not yet been seen by this server; "
                                " last csn seen from that replica is %s\n", 
                                csn_as_string (csn, PR_FALSE, csnStr), 
                                csn_as_string (supplierCsn, PR_FALSE, csnStr));                
            }
            rc = CL5_SUCCESS;
        }
    }
    else /* we have both purge and supplier csn */
    {
        if (csn_compare (csn, purgeCsn) < 0) /* the csn is below the purge point */
        {
            rc = CL5_PURGED_DATA;
        }
        else
        {
            if (csn_compare (csn, supplierCsn) <= 0) /* we should have the data but we don't */
            {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN: "
                                    "change with %s csn has been purged by this server; "
                                    "the current purge point for that replica is %s\n", 
                                    csn_as_string (csn, PR_FALSE, csnStr), 
                                    csn_as_string (purgeCsn, PR_FALSE, csnStr));
                }
                rc = CL5_MISSING_DATA;
            }
            else
            {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN: "
                                    "change with %s csn has not yet been seen by this server; "
                                    " last csn seen from that replica is %s\n", 
                                    csn_as_string (csn, PR_FALSE, csnStr), 
                                    csn_as_string (supplierCsn, PR_FALSE, csnStr));
                }
                rc = CL5_SUCCESS;    
            }
        }
    }

    if (supplierCsn)
        csn_free (&supplierCsn);

    if (purgeCsn)
        csn_free (&purgeCsn);

    return rc;
}

/* Helper functions that work with individual changelog files */

/* file name format : <replica name>_<replica generation>db{2,3,...} */
static PRBool _cl5FileName2Replica (const char *file_name, Object **replica)
{
    Replica *r;
    char *repl_name, *file_gen, *repl_gen;
    int len;

	PR_ASSERT (file_name && replica);

    *replica = NULL;

    /* this is database file */
    if (_cl5FileEndsWith (file_name, DB_EXTENSION) ||
        _cl5FileEndsWith (file_name, DB_EXTENSION_DB3) )
    {
        repl_name = slapi_ch_strdup (file_name);	
        file_gen = strstr(repl_name, FILE_SEP);
        if (file_gen)
        {
			int extlen = strlen(DB_EXTENSION);
            *file_gen = '\0';
            file_gen += strlen (FILE_SEP);
            len = strlen (file_gen);
            if (len <= extlen + 1)
            {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5FileName2Replica "
				                "invalid file name (%s)\n", file_name);                                        
            }
            else
            {
                /* get rid of the file extension */
                file_gen [len - extlen - 1] = '\0';
                *replica = replica_get_by_name (repl_name);
                if (*replica)
                {
                    /* check that generation matches the one in replica object */
                    r = (Replica*)object_get_data (*replica);
                    repl_gen = replica_get_generation (r);
                    PR_ASSERT (repl_gen);
                    if (strcmp (file_gen, repl_gen) != 0)
                    {
                        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5FileName2Replica "
				                "replica generation mismatch for replica at (%s), "
                                "file generation %s, new replica generation %s\n",
                                slapi_sdn_get_dn (replica_get_root (r)), file_gen, repl_gen);            
                                    
                        object_release (*replica);
                        *replica = NULL;
                    }
                    slapi_ch_free ((void**)&repl_gen);    
                }
            }
            slapi_ch_free ((void**)&repl_name);
        }
        else
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5FileName2Replica "
				            "malformed file name - %s\n", file_name);
        }

        return PR_TRUE;
    }
    else
        return PR_FALSE;
}

/* file name format : <replica name>_<replica generation>db{2,3} */
static char* _cl5Replica2FileName (Object *replica)
{
    const char *replName;
    char *replGen, *fileName;
    Replica *r;

	PR_ASSERT (replica);

    r = (Replica*)object_get_data (replica);
    PR_ASSERT (r);

    replName = replica_get_name (r);
    replGen = replica_get_generation (r);

    fileName = _cl5MakeFileName (replName, replGen) ;

    slapi_ch_free ((void**)&replGen);

    return fileName;
}

static char* _cl5MakeFileName (const char *replName, const char *replGen)
{
    char *fileName = slapi_ch_smprintf("%s/%s%s%s.%s", 
                                       s_cl5Desc.dbDir, replName,
                                       FILE_SEP, replGen, DB_EXTENSION);

    return fileName;
}

/* open file that corresponds to a particular database */
static int _cl5DBOpenFile (Object *replica, Object **obj, PRBool checkDups)
{
    int rc;
    const char *replName;
    char *replGen;
    Replica *r;

    PR_ASSERT (replica);

    r = (Replica*)object_get_data (replica);
    replName = replica_get_name (r);
    PR_ASSERT (replName);
    replGen = replica_get_generation (r);
    PR_ASSERT (replGen);

    rc = _cl5DBOpenFileByReplicaName (replName, replGen, obj, checkDups);

    slapi_ch_free ((void**)&replGen);

    return rc;
}

static int _cl5DBOpenFileByReplicaName (const char *replName, const char *replGen, 
                                        Object **obj, PRBool checkDups)
{
    int rc = CL5_SUCCESS;
	Object *tmpObj;
	CL5DBFile *file;
    char *file_name;

	PR_ASSERT (replName && replGen);

	if (checkDups)
	{
		PR_Lock (s_cl5Desc.fileLock);
        file_name = _cl5MakeFileName (replName, replGen);
		tmpObj = objset_find (s_cl5Desc.dbFiles, _cl5CompareDBFile, file_name);
		slapi_ch_free((void **)&file_name);
		if (tmpObj)	/* this file already exist */
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5DBOpenFileByReplicaName: Found DB object %p for replica %s\n", tmpObj, replName);
			/* if we were asked for file handle - keep the handle */
			if (obj)
			{
				*obj = tmpObj;
			}
			else
			{
				object_release (tmpObj);
			}
			
			rc = CL5_SUCCESS;
			goto done;
		}
	}

	rc = _cl5NewDBFile (replName, replGen, &file);
	if (rc == CL5_SUCCESS)
	{		
		/* This creates the file but doesn't set the init flag
		 * The flag is set later when the purge and max ruvs are set.
		 * This is to prevent some thread to get file access before the
		 * structure is fully initialized */
		rc = _cl5AddDBFile (file, &tmpObj);
		if (rc == CL5_SUCCESS)
		{
			/* read purge RUV - done here because it needs file object rather than file pointer */
			rc = _cl5ReadRUV (replGen, tmpObj, PR_TRUE);	
			if (rc != CL5_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5DBOpenFileByReplicaName: failed to get purge RUV\n");
				goto done;
			}

			/* read ruv that represents the upper bound of the changes stored in the file */
			rc = _cl5ReadRUV (replGen, tmpObj, PR_FALSE);	
			if (rc != CL5_SUCCESS)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5DBOpenFileByReplicaName: failed to get upper bound RUV\n");
				goto done;
			}
			
			/* Mark the DB File initialize */
			_cl5DBFileInitialized(tmpObj);
			
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5DBOpenFileByReplicaName: created new DB object %p\n", tmpObj);
			if (obj)
			{
				*obj = tmpObj;
			}
			else
			{
				object_release (tmpObj);	
			}
		}
	}

done:;
	if (rc != CL5_SUCCESS)
	{
		if (file)
			_cl5DBCloseFile ((void**)&file);
	}

	if (checkDups)
	{
		PR_Unlock (s_cl5Desc.fileLock);		
	}	

	return rc;		
}

/* adds file to the db file list */
static int _cl5AddDBFile (CL5DBFile *file, Object **obj)
{
	int rc;
	Object *tmpObj;

	PR_ASSERT (file);	

	tmpObj = object_new (file, _cl5DBCloseFile);
	rc = objset_add_obj(s_cl5Desc.dbFiles, tmpObj);
	if (rc != OBJSET_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5AddDBFile: failed to add db file to the list; "
						"repl_objset error - %d\n", rc);
		object_release (tmpObj);
		return CL5_OBJSET_ERROR;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5AddDBFile: Added new DB object %p\n", tmpObj);
	}

	if (obj)
	{
		*obj = tmpObj;
	}
	else
		object_release (tmpObj);

	return CL5_SUCCESS;
} 

static int _cl5NewDBFile (const char *replName, const char *replGen, CL5DBFile** dbFile)
{	
	int rc;
	DB *db = NULL;
	char *name;
	char *semadir;
#ifdef HPUX
	char cwd [PATH_MAX+1];
#endif
	
	PR_ASSERT (replName && replGen && dbFile);

	if (!dbFile) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5NewDBFile: NULL dbFile\n");
		return CL5_UNKNOWN_ERROR;
	}

	(*dbFile) = (CL5DBFile *)slapi_ch_calloc (1, sizeof (CL5DBFile));	
	if (*dbFile == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5NewDBFile: memory allocation failed\n");
		return CL5_MEMORY_ERROR;
	}

	name = _cl5MakeFileName (replName, replGen);	 
	{
	/* The subname argument allows applications to have
	 * subdatabases, i.e., multiple databases inside of a single
	 * physical file. This is useful when the logical databases
	 * are both numerous and reasonably small, in order to
	 * avoid creating a large number of underlying files.
	 */
	char *subname = NULL;
	DB_ENV *dbEnv = s_cl5Desc.dbEnv;

	rc = db_create(&db, dbEnv, 0);
	if (0 != rc) {
		goto out;
	}

	rc = db->set_pagesize(
		db,
		s_cl5Desc.dbConfig.pageSize);

	if (0 != rc) {
		goto out;
	}

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 3300
	rc = db->set_malloc(db, (void *)slapi_ch_malloc);
	if (0 != rc) {
		goto out;
	}
#endif

	DB_OPEN(s_cl5Desc.dbEnvOpenFlags,
			db, NULL /* txnid */, name, subname, DB_BTREE,
			DB_CREATE | DB_THREAD, s_cl5Desc.dbConfig.fileMode, rc);
	}
out:
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5NewDBFile: db_open failed; db error - %d %s\n", 
						rc, db_strerror(rc));
		rc = CL5_DB_ERROR;
		goto done;
	}

    (*dbFile)->db = db;
    (*dbFile)->name = name;  
    (*dbFile)->replName = slapi_ch_strdup (replName);  
    (*dbFile)->replGen = slapi_ch_strdup (replGen);  

	/*
	 * Considerations for setting up cl semaphore:
	 * (1) The NT version of SleepyCat uses test-and-set mutexes
	 *     at the DB page level instead of blocking mutexes. That has
	 *     proven to be a killer for the changelog DB, as this DB is
	 *     accessed by multiple a reader threads (the repl thread) and
	 *     writer threads (the server ops threads) usually at the last
	 *     pages of the DB, due to the sequential nature of the changelog
	 *     keys. To avoid the test-and-set mutexes, we could use semaphore
	 *     to serialize the writers and avoid the high mutex contention
	 *     that SleepyCat is unable to avoid.
	 * (2) DS 6.2 introduced the semaphore on all platforms (replaced
	 *     the serial lock used on Windows and Linux described above). 
	 *     The number of the concurrent writes now is configurable by
	 *     nsslapd-changelogmaxconcurrentwrites (the server needs to
	 *     be restarted).
	 */

	semadir = s_cl5Desc.dbDir;
#ifdef HPUX
	/*
	 * HP sem_open() does not allow pathname component "./" or "../"
	 * in the semaphore name. For simplicity and to avoid doing
	 * chdir() in multi-thread environment, current working dir
	 * (log dir) is used to replace the original semaphore dir
	 * if it contains "./".
	 */
	if ( strstr ( semadir, "./" ) != NULL && getcwd ( cwd, PATH_MAX+1 ) != NULL )
	{
		semadir = cwd;
	}
#endif

	if ( semadir != NULL )
	{
		(*dbFile)->semaName = slapi_ch_smprintf("%s/%s.sema", semadir, replName);
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl,
			"_cl5NewDBFile: semaphore %s\n", (*dbFile)->semaName);
		(*dbFile)->sema = PR_OpenSemaphore((*dbFile)->semaName,
                        PR_SEM_CREATE | PR_SEM_EXCL, 0666,
                        s_cl5Desc.dbConfig.maxConcurrentWrites );
		slapi_log_error (SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5NewDBFile: maxConcurrentWrites=%d\n", s_cl5Desc.dbConfig.maxConcurrentWrites );
	}

	if ((*dbFile)->sema == NULL )
	{
		/* If the semaphore was left around due
		 * to an unclean exit last time, remove
		 * and re-create it.
		 */ 
		if (PR_GetError() == PR_FILE_EXISTS_ERROR) {
			PR_DeleteSemaphore((*dbFile)->semaName);
			(*dbFile)->sema = PR_OpenSemaphore((*dbFile)->semaName,
					PR_SEM_CREATE | PR_SEM_EXCL, 0666,
					s_cl5Desc.dbConfig.maxConcurrentWrites );
		}

		/* If we still failed to create the semaphore,
		 * we should just error out. */
		if ((*dbFile)->sema == NULL )
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
				"_cl5NewDBFile: failed to create semaphore %s; NSPR error - %d\n",
				(*dbFile)->semaName ? (*dbFile)->semaName : "(nil)", PR_GetError());
			rc = CL5_SYSTEM_ERROR;
			goto done;
		}
	}

	/* compute number of entries in the file */
	/* ONREPL - to improve performance, we keep entry count in memory
			    and write it down during shutdown. Problem: this will not
				work with multiple processes. Do we have to worry about that?
     */
	if (s_cl5Desc.dbOpenMode == CL5_OPEN_NORMAL)
	{
		rc = _cl5GetEntryCount (*dbFile);
		if (rc != CL5_SUCCESS)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
							"_cl5NewDBFile: failed to get entry count\n");
			goto done;
		}
    }

done:
	if (rc != CL5_SUCCESS)
	{
		_cl5DBCloseFile ((void**)dbFile);
		/* slapi_ch_free accepts NULL pointer */
		slapi_ch_free ((void**)&name);

		slapi_ch_free ((void**)dbFile);
	}

	return rc;
}

static void _cl5DBCloseFile (void **data)
{ 
	CL5DBFile *file;
	int rc = 0;

	PR_ASSERT (data);

	file = *(CL5DBFile**)data;

	PR_ASSERT (file);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBCloseFile: "
					"Closing database %s\n", file->name);

	/* close the file */
	/* if this is normal close or close after import, update entry count */	
	if ((s_cl5Desc.dbOpenMode == CL5_OPEN_NORMAL && s_cl5Desc.dbState == CL5_STATE_CLOSING) ||
		s_cl5Desc.dbOpenMode == CL5_OPEN_LDIF2CL)
	{
		_cl5WriteEntryCount (file);
		_cl5WriteRUV (file, PR_TRUE);
		_cl5WriteRUV (file, PR_FALSE); 
	}

	/* close the db */
	if (file->db) {
	    rc = file->db->close(file->db, 0);
	    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
						"_cl5DBCloseFile: "
						"Closed the changelog database handle for %s "
						"(rc: %d)\n", file->name, rc);
	    file->db = NULL;
	}

	if (file->flags & DB_FILE_DELETED)
    {
		/* We need to use the libdb API to delete the files, otherwise we'll
		 * run into problems when we try to checkpoint transactions later. */
	    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBCloseFile: "
						"removing the changelog %s (flag %d)\n",
						file->name, DEFAULT_DB_ENV_OP_FLAGS);
		rc = s_cl5Desc.dbEnv->dbremove(s_cl5Desc.dbEnv, 0, file->name, 0,
                                       DEFAULT_DB_ENV_OP_FLAGS);
		if (rc != 0)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBCloseFile: "
							"failed to remove (%s) file; libdb error - %d (%s)\n", 
							file->name, rc, db_strerror(rc));
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBCloseFile: "
							"Deleted the changelog database file %s\n", file->name);

        }
	}

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&file->name);
	slapi_ch_free ((void**)&file->replName);
	slapi_ch_free ((void**)&file->replGen);
	ruv_destroy(&file->maxRUV);
	ruv_destroy(&file->purgeRUV);
	file->db = NULL;
	if (file->sema) {
		PR_CloseSemaphore (file->sema);
		PR_DeleteSemaphore (file->semaName);
		file->sema = NULL;
	}
	slapi_ch_free ((void**)&file->semaName);

	slapi_ch_free (data);
}

static int _cl5GetDBFile (Object *replica, Object **obj)
{
    char *fileName;

    PR_ASSERT (replica && obj);

    fileName = _cl5Replica2FileName (replica);

	*obj = objset_find(s_cl5Desc.dbFiles, _cl5CompareDBFile, fileName);
	if (*obj)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetDBFile: "
						"found DB object %p for database %s\n", *obj, fileName);
		slapi_ch_free_string(&fileName);
		return CL5_SUCCESS;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetDBFile: "
						"no DB object found for database %s\n", fileName);
		slapi_ch_free_string(&fileName);
		return CL5_NOTFOUND;
	}
}

static int _cl5GetDBFileByReplicaName (const char *replName, const char *replGen, 
                                       Object **obj)
{
    char *fileName;

    PR_ASSERT (replName && replGen && obj);

    fileName = _cl5MakeFileName (replName, replGen);

	*obj = objset_find(s_cl5Desc.dbFiles, _cl5CompareDBFile, fileName);
	if (*obj)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetDBFileByReplicaName: "
						"found DB object %p for database %s\n", *obj, fileName);
		slapi_ch_free_string(&fileName);
		return CL5_SUCCESS;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetDBFileByReplicaName: "
						"no DB object found for database %s\n", fileName);
		slapi_ch_free_string(&fileName);
		return CL5_NOTFOUND;
	}
}

static void _cl5DBDeleteFile (Object *obj)
{
    CL5DBFile *file;
    int rc = 0;

    PR_ASSERT (obj);

    file = (CL5DBFile*)object_get_data (obj);
	PR_ASSERT (file);
	file->flags |= DB_FILE_DELETED;
	rc = objset_remove_obj(s_cl5Desc.dbFiles, obj);
	if (rc != OBJSET_SUCCESS) {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBDeleteFile: "
						"could not find DB object %p\n", obj);
	} else {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5DBDeleteFile: "
						"removed DB object %p\n", obj);
	}
	object_release (obj);
}

static void _cl5DBFileInitialized (Object *obj) 
{
	CL5DBFile *file;
	
	PR_ASSERT (obj);
	
	file = (CL5DBFile*)object_get_data (obj);
	PR_ASSERT (file);
	file->flags |= DB_FILE_INIT;
}

static int _cl5CompareDBFile (Object *el1, const void *el2)
{
	CL5DBFile *file;
	const char *name;

	PR_ASSERT (el1 && el2);

	file = (CL5DBFile*) object_get_data (el1);
	name = (const char*) el2;
	return ((file->flags & DB_FILE_INIT) ? strcmp (file->name, name) : 1);
}

/*
 * return 1: true (the "filename" ends with "ext")
 * return 0: false
 */
static int _cl5FileEndsWith(const char *filename, const char *ext)
{
	char *p = NULL;
	int flen = strlen(filename);
	int elen = strlen(ext);
	if (0 == flen || 0 == elen)
	{
		return 0;
	}
	p = strstr(filename, ext);
	if (NULL == p)
	{
		return 0;
	}
	if (p - filename + elen == flen)
	{
		return 1;
	}
	return 0;
}

static int _cl5ExportFile (PRFileDesc *prFile, Object *obj)
{
	int rc;
	void *iterator = NULL;
	slapi_operation_parameters op = {0};
	char *buff;
	PRInt32 len, wlen;
	CL5Entry entry;
    CL5DBFile *file;

    PR_ASSERT (prFile && obj);

    file = (CL5DBFile*)object_get_data (obj);
    PR_ASSERT (file);

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        ruv_dump (file->purgeRUV, "clpurgeruv", prFile);
        ruv_dump (file->maxRUV, "clmaxruv", prFile);
    }
	slapi_write_buffer (prFile, "\n", strlen("\n"));

	entry.op = &op;
	rc = _cl5GetFirstEntry (obj, &entry, &iterator, NULL); 
	while (rc == CL5_SUCCESS)
	{
		rc = _cl5Operation2LDIF (&op, file->replGen, &buff, &len);
		if (rc != CL5_SUCCESS)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5ExportFile: failed to convert operation to ldif\n");
			operation_parameters_done (&op);
			break;
		}

		wlen = slapi_write_buffer (prFile, buff, len);
		slapi_ch_free((void **)&buff);
		if (wlen < len)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5ExportFile: failed to write to ldif file\n");
			rc = CL5_SYSTEM_ERROR;
			operation_parameters_done (&op);
			break;
		}

		cl5_operation_parameters_done (&op);

		rc = _cl5GetNextEntry (&entry, iterator);				
	}

	cl5_operation_parameters_done (&op);

	if (iterator)
		cl5DestroyIterator (iterator);

	if (rc != CL5_NOTFOUND)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"_cl5ExportFile: failed to retrieve changelog entry\n");
	}
	else
	{
		rc = CL5_SUCCESS;
	}

	return rc;
}

static PRBool _cl5ReplicaInList (Object *replica, Object **replicas)
{
	int i;

	PR_ASSERT (replica && replicas);

    /* ONREPL I think it should be sufficient to just compare replica pointers */
	for (i=0; replicas[i]; i++)
	{
		if (replica == replicas[i])
			return PR_TRUE;
	}

	return PR_FALSE;
}

static char* _cl5GetHelperEntryKey (int type, char *csnStr)
{
	CSN *csn= csn_new();
	char *rt;

	csn_set_time(csn, (time_t)type);
	csn_set_replicaid(csn, 0);

	rt = csn_as_string(csn, PR_FALSE, csnStr);
	csn_free(&csn);

	return rt;
}

static Object* _cl5GetReplica (const slapi_operation_parameters *op, const char* replGen)
{
    Slapi_DN *sdn;
    Object *replObj;
    Replica *replica;
    char *newGen;

    PR_ASSERT (op && replGen);

    sdn = op->target_address.sdn;
    
    replObj = replica_get_replica_from_dn (sdn);
    if (replObj)
    {
        /* check to see if replica generation has not change */
        replica = (Replica*)object_get_data (replObj);
        PR_ASSERT (replica);
        newGen = replica_get_generation (replica);
        PR_ASSERT (newGen);
        if (strcmp (replGen, newGen) != 0)
        {
            object_release (replObj);
            replObj = NULL;
        }

        slapi_ch_free ((void**)&newGen);
    }
    
    return replObj;
}

int
cl5_is_diskfull()
{
	int rc;
	PR_Lock(cl5_diskfull_lock);
    rc = cl5_diskfull_flag;
	PR_Unlock(cl5_diskfull_lock);
    return rc;
}

static void
cl5_set_diskfull()
{
	PR_Lock(cl5_diskfull_lock);
    cl5_diskfull_flag = 1;
	PR_Unlock(cl5_diskfull_lock);
}

static void
cl5_set_no_diskfull()
{
	PR_Lock(cl5_diskfull_lock);
    cl5_diskfull_flag = 0;
	PR_Unlock(cl5_diskfull_lock);
}

int
cl5_diskspace_is_available()
{
    int rval = 1;

#if defined( OS_solaris ) || defined( hpux )
    struct statvfs fsbuf;
    if (statvfs(s_cl5Desc.dbDir, &fsbuf) < 0)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
            "cl5_diskspace_is_available: Cannot get file system info\n");
        rval = 0;
    }
    else
    {
        unsigned long fsiz = fsbuf.f_bavail * fsbuf.f_frsize;
        if (fsiz < NO_DISK_SPACE)
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
                "cl5_diskspace_is_available: No enough diskspace for changelog: (%u bytes free)\n", fsiz);
            rval = 0;
        }
        else if (fsiz > MIN_DISK_SPACE)
        {
            /* assume recovered */
            cl5_set_no_diskfull();
        }
    }
#endif
#if defined( linux )
    struct statfs fsbuf;
    if (statfs(s_cl5Desc.dbDir, &fsbuf) < 0)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
            "cl5_diskspace_is_available: Cannot get file system info\n");
        rval = 0;
    }
    else
    {
        unsigned long fsiz = fsbuf.f_bavail * fsbuf.f_bsize;
        if (fsiz < NO_DISK_SPACE)
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
                "cl5_diskspace_is_available: No enough diskspace for changelog: (%lu bytes free)\n", fsiz);
            rval = 0;
        }
        else if (fsiz > MIN_DISK_SPACE)
        {
            /* assume recovered */
            cl5_set_no_diskfull();
        }
    }
#endif
    return rval;
}

int
cl5DbDirIsEmpty(const char *dir)
{
	PRDir *prDir;
	PRDirEntry *prDirEntry;
	int isempty = 1;

	if (!dir || !*dir) {
		return isempty;
	}
	/* assume failure means it does not exist - other failure
	   cases will be handled by code which attempts to create the
	   db in this directory */
	if (PR_Access(dir, PR_ACCESS_EXISTS)) {
		return isempty;
	}
	prDir = PR_OpenDir(dir);
	if (prDir == NULL) {
		return isempty; /* assume failure means does not exist */
	}
	while (NULL != (prDirEntry = PR_ReadDir(prDir, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
		if (NULL == prDirEntry->name) {	/* NSPR doesn't behave like the docs say it should */
			break;
		}
		isempty = 0; /* found at least one "real" file */
		break;
	}
	PR_CloseDir(prDir);

	return isempty;
}

/*
 * Write RUVs into the changelog;
 * implemented for backup to make sure the backed up changelog contains RUVs
 * Return values: 0 -- success
 *                1 -- failure
 */
int
cl5WriteRUV()
{
    int rc = 0;
    Object *file_obj = NULL;
    CL5DBFile *dbfile = NULL;
    int closeit = 0;
    int slapd_pid = 0;

    changelog5Config config;

    /* read changelog configuration */
    changelog5_read_config (&config);
    if (config.dir == NULL) {
        /* Changelog is not configured; Replication is not enabled.
         * we don't have to update RUVs.
         * bail out - return success */
        goto bail;
    }

    slapd_pid = is_slapd_running();
    if (slapd_pid <= 0) {
        /* I'm not a server, rather a utility.
         * And the server is NOT running.
         * RUVs should be in the changelog.
         * we don't have to update RUVs.
         * bail out - return success */
        goto bail;
    }

    if (getpid() != slapd_pid) {
        /* I'm not a server, rather a utility.
         * And the server IS running.
         * RUVs are not in the changelog and no easy way to retrieve them.
         * bail out - return failure */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                     "cl5WriteRUV: server (pid %d) is already running; bail.\n",
                     slapd_pid);
        rc = 1;
        goto bail;
    }

    /* file is stored in the changelog directory and is named
     *        <replica name>.ldif */
    if (CL5_STATE_OPEN != s_cl5Desc.dbState) {
        rc = _cl5Open(config.dir, &config.dbconfig, CL5_OPEN_NORMAL);
        if (rc != CL5_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                            "cl5WriteRUV: failed to open changelog\n");
            goto bail;
        }
        s_cl5Desc.dbState = CL5_STATE_OPEN; /* force to change the state */
        closeit = 1; /* It had not been opened; close it */
    }
                
    file_obj = objset_first_obj(s_cl5Desc.dbFiles);
    while (file_obj) {
        dbfile = (CL5DBFile *)object_get_data(file_obj);
        if (dbfile) {
            _cl5WriteEntryCount(dbfile);
            _cl5WriteRUV(dbfile, PR_TRUE);
            _cl5WriteRUV(dbfile, PR_FALSE); 
        }
        file_obj = objset_next_obj(s_cl5Desc.dbFiles, file_obj);
    }
bail:
    if (closeit && (CL5_STATE_OPEN == s_cl5Desc.dbState)) {
        _cl5Close ();
        s_cl5Desc.dbState = CL5_STATE_CLOSED; /* force to change the state */
    }
    changelog5_config_done(&config);
    return rc;
}

/*
 * Delete RUVs from the changelog;
 * implemented for backup to clean up RUVs
 * Return values: 0 -- success
 *                1 -- failure
 */
int
cl5DeleteRUV()
{
    int rc = 0;
    Object *file_obj = NULL;
    CL5DBFile *dbfile = NULL;
    int slapd_pid = 0;
    int closeit = 0;

    changelog5Config config;

    /* read changelog configuration */
    changelog5_read_config (&config);
    if (config.dir == NULL) {
        /* Changelog is not configured; Replication is not enabled.
         * we don't have to update RUVs.
         * bail out - return success */
        goto bail;
    }

    slapd_pid = is_slapd_running();
    if (slapd_pid <= 0) {
        /* I'm not a server, rather a utility.
         * And the server is NOT running.
         * RUVs should be in the changelog.
         * we don't have to update RUVs.
         * bail out - return success */
        goto bail;
    }

    if (getpid() != slapd_pid) {
        /* I'm not a server, rather a utility.
         * And the server IS running.
         * RUVs are not in the changelog.
         * bail out - return success */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                    "cl5DeleteRUV: server (pid %d) is already running; bail.\n",
                    slapd_pid);
        goto bail;
    }

    /* file is stored in the changelog directory and is named
     *        <replica name>.ldif */
    if (CL5_STATE_OPEN != s_cl5Desc.dbState) {
        rc = _cl5Open(config.dir, &config.dbconfig, CL5_OPEN_NORMAL);
        if (rc != CL5_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                            "cl5DeleteRUV: failed to open changelog\n");
            goto bail;
        }
        s_cl5Desc.dbState = CL5_STATE_OPEN; /* force to change the state */
        closeit = 1; /* It had been opened; no need to close */
    }

    file_obj = objset_first_obj(s_cl5Desc.dbFiles);
    while (file_obj) {
        dbfile = (CL5DBFile *)object_get_data(file_obj);

        /* _cl5GetEntryCount deletes entry count after reading it */
        rc = _cl5GetEntryCount(dbfile);
        if (rc != CL5_SUCCESS)
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
                            "cl5DeleteRUV: failed to get/delete entry count\n");
            goto bail;
        }
        /* _cl5ReadRUV deletes RUV after reading it */
        rc = _cl5ReadRUV (dbfile->replGen, file_obj, PR_TRUE);    
        if (rc != CL5_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                       "cl5DeleteRUV: failed to read/delete purge RUV\n");
            goto bail;
        }
        rc = _cl5ReadRUV (dbfile->replGen, file_obj, PR_FALSE);    
        if (rc != CL5_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
                       "cl5DeleteRUV: failed to read/delete upper bound RUV\n");
            goto bail;
        }
        file_obj = objset_next_obj(s_cl5Desc.dbFiles, file_obj);
    }
bail:
    if (file_obj) {
        object_release (file_obj);
    }
    if (closeit && (CL5_STATE_OPEN == s_cl5Desc.dbState)) {
        _cl5Close ();
        s_cl5Desc.dbState = CL5_STATE_CLOSED; /* force to change the state */
    }
    changelog5_config_done(&config);
    return rc;
}

/*
 *  Clean the in memory RUV, at shutdown we will write the update to the db
 */
void
cl5CleanRUV(ReplicaId rid){
    CL5DBFile *file;
    Object *obj = NULL;

    slapi_rwlock_wrlock (s_cl5Desc.stLock);

    obj = objset_first_obj(s_cl5Desc.dbFiles);
    while (obj){
        file = (CL5DBFile *)object_get_data(obj);
        ruv_delete_replica(file->purgeRUV, rid);
        ruv_delete_replica(file->maxRUV, rid);
        obj = objset_next_obj(s_cl5Desc.dbFiles, obj);
    }

    slapi_rwlock_unlock (s_cl5Desc.stLock);
}

void trigger_cl_trimming(ReplicaId rid){
    PRThread *trim_tid = NULL;

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, "trigger_cl_trimming: rid (%d)\n",(int)rid);
    trim_tid = PR_CreateThread(PR_USER_THREAD, (VFP)(void*)trigger_cl_trimming_thread,
                   (void *)&rid, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                   PR_UNJOINABLE_THREAD, DEFAULT_THREAD_STACKSIZE);
    if (NULL == trim_tid){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
            "trigger_cl_trimming: failed to create trimming "
            "thread; NSPR error - %d\n", PR_GetError ());
    } else {
        /* need a little time for the thread to get started */
        DS_Sleep(PR_SecondsToInterval(1));
    }
}

void
trigger_cl_trimming_thread(void *arg){
    ReplicaId rid = *(ReplicaId *)arg;

    /* make sure we have a change log, and we aren't closing it */
    if(s_cl5Desc.dbState == CL5_STATE_CLOSED || s_cl5Desc.dbState == CL5_STATE_CLOSING){
        return;
    }
    if (CL5_SUCCESS != _cl5AddThread()) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl,
            "trigger_cl_trimming: failed to increment thread count "
            "NSPR error - %d\n", PR_GetError ());
    }
    _cl5DoTrimming(rid);
    _cl5RemoveThread();
}
