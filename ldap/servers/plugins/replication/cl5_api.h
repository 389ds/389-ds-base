/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl5_api.h - interface to 5.0 changelog */

#ifndef CL5_API_H
#define CL5_API_H

#include "repl5.h"
#include "repl5_prot_private.h"

#define CL5_TYPE			"Changelog5"	/* changelog type */
#define VERSION_SIZE		127				/* size of the buffer to hold changelog version */
#define CL5_DEFAULT_CONFIG	-1				/* value that indicates to changelog to use default */
#define CL5_STR_IGNORE      "-1"			/* tels function to ignore this parameter */
#define CL5_NUM_IGNORE      -1				/* tels function to ignore this parameter */
#define CL5_STR_UNLIMITED 	"0"				/* represent unlimited value (trimming ) */
#define CL5_NUM_UNLIMITED 	 0				/* represent unlimited value (trimming ) */

#define CL5_OS_ERR_IS_DISKFULL(err) ((err)==ENOSPC || (err)==EFBIG)

/***** Data Structures *****/

/* changelog configuration structure */
typedef struct cl5dbconfig
{
	size_t	cacheSize;			/* cache size in bytes */
	PRBool	durableTrans;		/* flag that tells not to sync log when trans commits */
	PRInt32	checkpointInterval;	/* checkpoint interval in seconds */
	PRBool  circularLogging;	/* flag to archive and trancate log */
	size_t	pageSize;			/* page size in bytes */
	size_t	logfileSize;		/* maximum log size in bytes */
	size_t	maxTxnSize;			/* maximum txn table size in count*/
	PRInt32	fileMode;			/* file mode */
	PRBool	verbose;			/* Get libdb to exhale debugging info */
	PRBool	debug;				/* Will libdb emit debugging info into our log ? */
	PRInt32	tricklePercentage;	/* guaranteed percentage of clean cache pages; 0 - 100 */
	PRInt32	spinCount;			/* DB Mutex spin count */
	PRUint32 nb_lock_config;	/* Number of locks in the DB lock table. New in 5.1 */
/* The next 2 parameters are needed for configuring the changelog cache. New in 5.1 */
	PRUint32 maxChCacheEntries;
	PRUint32 maxChCacheSize;
	PRUint32 maxConcurrentWrites;	/* 6.2 max number of concurrent cl writes */
} CL5DBConfig;

/* changelog entry format */
typedef struct cl5entry
{
	slapi_operation_parameters *op;	 /* operation applied to the server */
	time_t				time; /* time added to the cl; used for trimming */
} CL5Entry;

/* default values for the changelog configuration structure above */
/* 
 * For historical reasons, dbcachesize refers to number of bytes at the DB level, 
 * whereas cachesize refers to number of entries at the changelog cache level (cachememsize is the
 * one refering to number of bytes at the changelog cache level) 
 */
#define CL5_DEFAULT_CONFIG_DB_DBCACHESIZE		10485760 /* 10M bytes */
#define CL5_DEFAULT_CONFIG_DB_DURABLE_TRANSACTIONS	1
#define CL5_DEFAULT_CONFIG_DB_CHECKPOINT_INTERVAL	60
#define CL5_DEFAULT_CONFIG_DB_CIRCULAR_LOGGING		1
#define CL5_DEFAULT_CONFIG_DB_PAGE_SIZE			8*1024
#define CL5_DEFAULT_CONFIG_DB_LOGFILE_SIZE		0
#define CL5_DEFAULT_CONFIG_DB_VERBOSE			0
#define CL5_DEFAULT_CONFIG_DB_DEBUG			0
#define CL5_DEFAULT_CONFIG_DB_TRICKLE_PERCENTAGE	40
#define CL5_DEFAULT_CONFIG_DB_SPINCOUNT			0
#define CL5_DEFAULT_CONFIG_DB_TXN_MAX			200
#define CL5_DEFAULT_CONFIG_CACHESIZE			3000 /* number of entries */
#define CL5_DEFAULT_CONFIG_CACHEMEMSIZE			1048576 /* 1 M bytes */
#define CL5_DEFAULT_CONFIG_NB_LOCK	1000 /* Number of locks in the lock table of the DB */

/*
 * Small number of concurrent writes degradate the throughput.
 * Large one increases deadlock.
 */
#ifdef SOLARIS
#define CL5_DEFAULT_CONFIG_MAX_CONCURRENT_WRITES	10
#else
#define CL5_DEFAULT_CONFIG_MAX_CONCURRENT_WRITES	2
#endif


#define CL5_MIN_DB_DBCACHESIZE		524288 /* min 500K bytes */
#define CL5_MIN_CACHESIZE		500 /* min number of entries */
#define CL5_MIN_CACHEMEMSIZE		262144 /* min 250K bytes */
#define CL5_MIN_NB_LOCK		1000 /* The minimal number of locks in the DB (Same as default) */

/* data structure that allows iteration through changelog */
typedef struct cl5replayiterator CL5ReplayIterator;

/* changelog state */
typedef enum
{
	CL5_STATE_NONE,		/* changelog has not been initialized */
	CL5_STATE_CLOSING,	/* changelog is about to close; all threads must exit */
	CL5_STATE_CLOSED,	/* changelog has been initialized, but not opened, or open and then closed */
	CL5_STATE_OPEN		/* changelog is opened */
} CL5State;

/* error codes */
enum
{
	CL5_SUCCESS,		/* successful operation */
	CL5_BAD_DATA,		/* invalid parameter passed to the function */
	CL5_BAD_FORMAT,		/* db data has unexpected format */
	CL5_BAD_STATE,		/* changelog is in an incorrect state for attempted operation */
	CL5_BAD_DBVERSION,	/* changelog has invalid dbversion */
	CL5_DB_ERROR,		/* database error */
	CL5_NOTFOUND,		/* requested entry or value was not found */			
	CL5_MEMORY_ERROR,	/* memory allocation failed */
	CL5_SYSTEM_ERROR,	/* NSPR error occured, use PR_Error for furhter info */
	CL5_CSN_ERROR,		/* CSN API failed */
	CL5_RUV_ERROR,		/* RUV API failed */
	CL5_OBJSET_ERROR,	/* namedobjset api failed */
    CL5_PURGED_DATA,    /* requested data has been purged */
    CL5_MISSING_DATA,   /* data should be in the changelog, but is missing */
	CL5_UNKNOWN_ERROR	/* unclassified error */
};

/***** Module APIs *****/

/* Name:		cl5Init
   Description:	initializes changelog module; must be called by a single thread
				before any function of the module.
   Parameters:  none
   Return:		CL5_SUCCESS if function is successful;
				CL5_BAD_DATA if invalid directory is passed;
				CL5_SYSTEM error if NSPR call fails.
 */
int cl5Init ();

/* Name:		cl5Cleanup
   Description:	performs cleanup of the changelog module. Must be called by a single
				thread. It will closed db if it is still open.
   Parameters:  none
   Return:      none
 */
void cl5Cleanup ();

/* Name:		cl5Open 
   Description:	opens changelog ; must be called after changelog is
				initialized using cl5Init. It is thread safe and the second
				call is ignored.
   Parameters:  dir - changelog dir
				config - db configuration parameters; currently not used
				openMode - open mode
   Return:		CL5_SUCCESS if successfull;
				CL5_BAD_DATA if invalid directory is passed;
				CL5_BAD_DBVERSION if dbversion file is missing or has unexpected data
				CL5_SYSTEM_ERROR if NSPR error occured (during db directory creation);
				CL5_MEMORY_ERROR if memory allocation fails;
				CL5_DB_ERROR if db initialization or open fails.
 */
int cl5Open (const char *dir, const CL5DBConfig *config);

/* Name:		cl5Close
   Description:	closes changelog and cleanups changelog module; waits until
				all threads are done using changelog
   Parameters:  none
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if db is not in the open state;
				CL5_SYSTEM_ERROR if NSPR call fails
 */
int cl5Close ();

/* Name:		cl5Delete
   Description:	removes changelog
   Parameters:  dir - changelog directory
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if the changelog is not in closed state;
				CL5_BAD_DATA if invalid directory supplied
				CL5_SYSTEM_ERROR if NSPR call fails
 */
int cl5Delete (const char *dir);

/* Name:		cl5OpenDB
   Description: opens changelog file for specified file
   Parameters:	replica - replica whose file we wish to open 
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if the changelog is not initialized;
				CL5_BAD_DATA - if NULL id is supplied
 */		
int cl5OpenDB (Object *replica);

/* Name:		cl5CloseDB
   Description: closes changelog file for the specified replica
   Parameters:	replica - replica whose file we wish to close
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if the changelog is not initialized;
				CL5_BAD_DATA - if NULL id is supplied
				CL5_NOTFOUND - nothing is known about specified database
 */		
int cl5CloseDB (Object *replica);

/* Name:		cl5DeleteDB
   Description: asynchronously removes changelog file for the specified replica.
                The file is physically removed when it is no longer in use.
				This function is called when a backend is removed or reloaded. 
   Parameters:	replica - replica whose file we wish to delete 
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if the changelog is not initialized;
				CL5_BAD_DATA - if NULL id is supplied
				CL5_NOTFOUND - nothing is known about specified database
 */		
int cl5DeleteDB (Object *replica);

/* Name:        cl5DeleteDBSync
   Description: The same as cl5DeleteDB except the function does not return
                until the file is removed.
*/
int cl5DeleteDBSync (Object *replica);

/* Name:        cl5GetUpperBoundRUV
   Description: retrieves vector that represent the upper bound of changes 
                stored in the changelog for the replica. 
   Parameters:  r - replica for which the vector is requested
                ruv - contains a copy of the upper bound ruv if function is successful; 
                unchanged otherwise. It is responsobility pf the caller to free
                the ruv when it is no longer is in use
   Return:      CL5_SUCCESS if function is successfull
                CL5_BAD_STATE if the changelog is not initialized;
				CL5_BAD_DATA - if NULL id is supplied
                CL5_NOTFOUND, if changelog file for replica is not found
 */
int cl5GetUpperBoundRUV (Replica *r, RUV **ruv);

/* Name:		cl5Backup
   Description:	makes a backup of the changelog including *.db2, 
				log files, and dbversion. Can be called with the changelog in either open or
				closed state.
   Parameters:  bkDir - directory to which the data is backed up; 
				created if it does not exist
				replicas - optional list of replicas whose changes should be backed up;
						   if the list is NULL, entire changelog is backed up.
   Return:		CL5_SUCCESS if function is successful;
				CL5_BAD_DATA if invalid directory is passed;
				CL5_BAD_STATE if changelog has not been initialized;
				CL5_DB_ERROR if db call fails;
				CL5_SYSTEM_ERROR if NSPR call or file copy failes.
 */
int cl5Backup (const char *bkDir, Object **replicas);

/* Name:		cl5Restore
   Description:	restores changelog from the backed up copy. Changelog must be ibnitalized and closed.
   Parameters:  clDir - changelog dir
				bkDir - directory that contains the backup
				replicas - optional list of replicas whose changes should be recovered;
						   if the list is NULL, entire changelog is recovered.
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE if changelog is open or not initialized;
				CL5_DB_ERROR if db call fails;
				CL5_SYSTEM_ERROR if NSPR call of file copy fails
 */
int cl5Restore (const char *clDir, const char *bkDir, Object **replicas);

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
int cl5ExportLDIF (const char *ldifFile, Object **replicas);

/* Name:		cl5ImportLDIF
   Description:	imports ldif file into changelog; changelog must be in the closed state
   Parameters:  clDir - changelog dir
				ldifFile - absolute path to the ldif file to import
				replicas - optional list of replicas whose data should be imported;
						   if the list is NULL, all data in the file is imported.
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE if changelog is open or not inititalized;
				CL5_DB_ERROR if db api fails;
				CL5_SYSTEM_ERROR if NSPR call fails;
				CL5_MEMORY_ERROR if memory allocation fials.
 */
int cl5ImportLDIF (const char *clDir, const char *ldifFile, Object **replicas);

/* Name:		cl5GetState
   Description:	returns database state
   Parameters:  none
   Return:		changelog state
 */

int cl5GetState ();

/* Name:		cl5ConfigTrimming
   Description:	sets changelog trimming parameters
   Parameters:  maxEntries - maximum number of entries in the log;
				maxAge - maximum entry age;
   Return:		CL5_SUCCESS if successful;
				CL5_BAD_STATE if changelog has not been open
 */
int cl5ConfigTrimming (int maxEntries, const char *maxAge);

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
int cl5GetOperation (Object *replica, slapi_operation_parameters *op);

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
int cl5GetFirstOperation (Object *replica, slapi_operation_parameters *op, void **iterator);

/* Name:		cl5GetNextOperation
   Description: retrieves the next op from the changelog as defined by the iterator
   Parameters:  replica - replica for which the operation should be retrieved.
                op - returned operation, if function is successful
				iterator - in: identifies op to retrieve; out: identifies next op
   Return:		CL5_SUCCESS, if successful
				CL5_BADDATA, if invalid parameter is supplied
				CL5_BAD_STATE, if changelog is not open
				CL5_NOTFOUND, empty changelog
				CL5_DB_ERROR, if db call fails
 */
int cl5GetNextOperation (slapi_operation_parameters *op, void *iterator);

/* Name:		cl5DestroyIterator
   Description: destroys iterator once iteration through changelog is done
   Parameters:  iterator - iterator to destroy
   Return:		CL5_SUCCESS, if successful
				CL5_BADDATA, if invalid parameters is supplied
				CL5_BAD_STATE, if changelog is not open
				CL5_DB_ERROR, if db call fails
 */
void cl5DestroyIterator (void *iterator);

/* Name:		cl5WriteOperation
   Description:	writes operation to changelog
   Parameters:  repl_name - name of the replica to which operation applies
                repl_gen - replica generation for the operation
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
int cl5WriteOperation(const char *repl_name, const char *repl_gen,
                      const slapi_operation_parameters *op, PRBool local);

/* Name:		cl5CreateReplayIterator
   Description:	creates an iterator that allows to retireve changes that should
				to be sent to the consumer identified by ruv The iteration is peformed by 
                repeated calls to cl5GetNextOperationToReplay.
   Parameters:  replica - replica whose data we wish to iterate;
				ruv - consumer ruv;
				iterator - iterator to be passed to cl5GetNextOperationToReplay call
   Return:		CL5_SUCCESS, if function is successfull;
                CL5_MISSING_DATA, if data that should be in the changelog is missing
                CL5_PURGED_DATA, if some data that consumer needs has been purged.
                Note that the iterator can be non null if the supplier contains
                some data that needs to be sent to the consumer
                CL5_NOTFOUND if the consumer is up to data with respect to the supplier
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_BAD_STATE  if db has not been open;
				CL5_DB_ERROR if any other db error occured;
				CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5CreateReplayIterator (Private_Repl_Protocol *prp, const RUV *ruv, 
							 CL5ReplayIterator **iterator);
int cl5CreateReplayIteratorEx (Private_Repl_Protocol *prp, const RUV *consumerRuv,
				CL5ReplayIterator **iterator,    ReplicaId consumerRID );


/* Name:		cl5GetNextOperationToReplay
   Description:	retrieves next operation to be sent to the consumer and
				that was created on a particular master. Consumer and master info
				is encoded in the iterator parameter that must be created by calling
				to cl5CreateIterator.
   Parameters:  iterator - iterator that identifies next entry to retrieve;
				op - operation retireved if function is successful
   Return:		CL5_SUCCESS if function is successfull;
				CL5_BAD_DATA if invalid parameter is passed;
				CL5_NOTFOUND if end of iteration list is reached
				CL5_DB_ERROR if any other db error occured;
				CL5_BADFORMAT if data in db is of unrecognized format;
				CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5GetNextOperationToReplay (CL5ReplayIterator *iterator, 
								 CL5Entry *entry);

/* Name:		cl5DestroyReplayIterator
   Description:	destorys iterator
   Parameters:  iterator - iterator to destory
   Return:		none
 */
void cl5DestroyReplayIterator (CL5ReplayIterator **iterator);

/* Name:		cl5DeleteOnClose
   Description:	marks changelog for deletion when it is closed
   Parameters:  flag; if flag = 1 then delete else don't
   Return:		none
 */

void cl5DeleteOnClose (PRBool rm);

/* Name:		cl5GetDir
   Description:	returns changelog directory; must be freed by the caller;
   Parameters:  none
   Return:		copy of the directory; caller needs to free the string
 */
 
char *cl5GetDir ();

/* Name: cl5Exist
   Description: checks if a changelog exists in the specified directory
   Parameters: clDir - directory to check; 
   Return: 1 - if changelog exists; 0 - otherwise
 */

PRBool cl5Exist (const char *clDir);

/* Name: cl5GetOperationCount
   Description: returns number of entries in the changelog. The changelog must be
				open for the value to be meaningful.
   Parameters:  replica - optional parameter that specifies the replica whose operations
                we wish to count; if NULL all changelog entries are counted
   Return:		number of entries in the changelog
 */

int cl5GetOperationCount (Object *replica);

/* Name: cl5_operation_parameters_done
   Description: frees all parameters that are not freed by operation_parameters_done
				function in the server.

 */

void cl5_operation_parameters_done (struct slapi_operation_parameters *sop);

/* Name: cl5CreateDirIfNeeded
   Description: Create the directory if it doesn't exist yet
   Parameters:	dir - Contains the name of the directory to create. Must not be NULL
   Return:		CL5_SUCCESS if succeeded or existed,
				CL5_SYSTEM_ERROR if failed.
*/

int cl5CreateDirIfNeeded (const char *dir); 
int cl5DBData2Entry (const char *data, PRUint32 len, CL5Entry *entry);

PRBool cl5HelperEntry (const char *csnstr, CSN *csn);
CSN** cl5BuildCSNList (const RUV *consRuv, const RUV *supRuv);
void cl5DestroyCSNList (CSN*** csns);

int cl5_is_diskfull();
int cl5_diskspace_is_available();

#endif
