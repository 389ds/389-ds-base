/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* cl5_api.h - interface to 5.0 changelog */

#ifndef CL5_API_H
#define CL5_API_H

#include "repl5.h"
#include "repl5_prot_private.h"
#include <errno.h>

#define CL5_TYPE "Changelog5" /* changelog type */
#define VERSION_SIZE 127      /* size of the buffer to hold changelog version */
#define CL5_DEFAULT_CONFIG -1 /* value that indicates to changelog to use default */
#define CL5_STR_IGNORE "-1"   /* tells function to ignore this parameter */
#define CL5_NUM_IGNORE -1     /* tells function to ignore this parameter */
#define CL5_STR_UNLIMITED "0" /* represent unlimited value (trimming ) */
#define CL5_NUM_UNLIMITED 0   /* represent unlimited value (trimming ) */

#define CL5_OS_ERR_IS_DISKFULL(err) ((err) == ENOSPC || (err) == EFBIG)

/***** Data Structures *****/

/* changelog entry format */
typedef struct cl5entry
{
    slapi_operation_parameters *op; /* operation applied to the server */
    time_t time;                    /* time added to the cl; used for trimming */
} CL5Entry;

/* default values for the changelog configuration structure above */
/*
 * For historical reasons, dbcachesize refers to number of bytes at the DB level,
 * whereas cachesize refers to number of entries at the changelog cache level (cachememsize is the
 * one referring to number of bytes at the changelog cache level)
 */
#define CL5_DEFAULT_CONFIG_DB_DBCACHESIZE 10485760 /* 10M bytes */
#define CL5_DEFAULT_CONFIG_DB_DURABLE_TRANSACTIONS 1
#define CL5_DEFAULT_CONFIG_DB_CHECKPOINT_INTERVAL 60
#define CL5_DEFAULT_CONFIG_DB_CIRCULAR_LOGGING 1
#define CL5_DEFAULT_CONFIG_DB_PAGE_SIZE 8 * 1024
#define CL5_DEFAULT_CONFIG_DB_LOGFILE_SIZE 0
#define CL5_DEFAULT_CONFIG_DB_VERBOSE 0
#define CL5_DEFAULT_CONFIG_DB_DEBUG 0
#define CL5_DEFAULT_CONFIG_DB_TRICKLE_PERCENTAGE 40
#define CL5_DEFAULT_CONFIG_DB_SPINCOUNT 0
#define CL5_DEFAULT_CONFIG_DB_TXN_MAX 200
#define CL5_DEFAULT_CONFIG_CACHESIZE 3000       /* number of entries */
#define CL5_DEFAULT_CONFIG_CACHEMEMSIZE 1048576 /* 1 M bytes */
#define CL5_DEFAULT_CONFIG_NB_LOCK 1000         /* Number of locks in the lock table of the DB */

#define CL5_MIN_DB_DBCACHESIZE 524288 /* min 500K bytes */
#define CL5_MIN_CACHESIZE 500         /* min number of entries */
#define CL5_MIN_CACHEMEMSIZE 262144   /* min 250K bytes */
#define CL5_MIN_NB_LOCK 1000          /* The minimal number of locks in the DB (Same as default) */

/* data structure that allows iteration through changelog */
typedef struct cl5replayiterator CL5ReplayIterator;

/* database information for the changelog */
typedef struct cl5DBFileHandle cldb_Handle;

/* changelog state */
typedef enum {
    CL5_STATE_CLOSED,  /* changelog is not opened */
    CL5_STATE_OPEN,    /* changelog is opened */
    CL5_STATE_IMPORT   /* Changelog is being initialized by LDIF */
} CL5State;

/* error codes */
enum
{
    CL5_SUCCESS,       /* successful operation */
    CL5_BAD_DATA,      /* invalid parameter passed to the function */
    CL5_BAD_FORMAT,    /* db data has unexpected format */
    CL5_BAD_STATE,     /* changelog is in an incorrect state for attempted operation */
    CL5_BAD_DBVERSION, /* changelog has invalid dbversion */
    CL5_DB_ERROR,      /* database error */
    CL5_NOTFOUND,      /* requested entry or value was not found */
    CL5_MEMORY_ERROR,  /* memory allocation failed */
    CL5_SYSTEM_ERROR,  /* NSPR error occured, use PR_Error for furhter info */
    CL5_CSN_ERROR,     /* CSN API failed */
    CL5_RUV_ERROR,     /* RUV API failed */
    CL5_OBJSET_ERROR,  /* namedobjset api failed */
    CL5_DB_LOCK_ERROR, /* bdb returns error 12 when the db runs out of locks,
                           this var needs to be in slot 12 of the list.
                           Do not re-order enum above! */
    CL5_PURGED_DATA,   /* requested data has been purged */
    CL5_MISSING_DATA,  /* data should be in the changelog, but is missing */
    CL5_UNKNOWN_ERROR, /* unclassified error */
    CL5_IGNORE_OP,     /* ignore this updated - used by CLEANALLRUV task */
    CL5_DB_RETRY,      /* Retryable database error  */
    CL5_LAST_ERROR_CODE /* Should always be last in this enum */
};

/***** Module APIs *****/

/* Name:        cl5Init
   Description: initializes changelog module; must be called by a single thread
                before any function of the module.
   Parameters:  none
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid directory is passed;
                CL5_SYSTEM error if NSPR call fails.
 */
int cl5Init(void);

/* Name:        cl5Open
   Description: opens changelog ; must be called after changelog is
                initialized using cl5Init. It is thread safe and the second
                call is ignored.
   Return:      CL5_SUCCESS if successful;
                CL5_BAD_DATA if invalid directory is passed;
                CL5_BAD_DBVERSION if dbversion file is missing or has unexpected data
                CL5_SYSTEM_ERROR if NSPR error occurred (during db directory creation);
                CL5_MEMORY_ERROR if memory allocation fails;
                CL5_DB_ERROR if db initialization or open fails.
 */
int cl5Open(void);

/* Name:        cl5Close
   Description: closes changelog and cleanups changelog module; waits until
                all threads are done using changelog
   Parameters:  none
   Return:      CL5_SUCCESS if successful;
                CL5_BAD_STATE if db is not in the open state;
                CL5_SYSTEM_ERROR if NSPR call fails
 */
int cl5Close(void);

/* Name:        cldb_RemoveReplicaDB
   Description: Clear the cldb information from the replica 
                and delete the database file
*/
int cldb_RemoveReplicaDB(Replica *replica);

/* Name:        cl5GetUpperBoundRUV
   Description: retrieves vector that represent the upper bound of changes
                stored in the changelog for the replica.
   Parameters:  r - replica for which the vector is requested
                ruv - contains a copy of the upper bound ruv if function is successful;
                unchanged otherwise. It is responsibility of the caller to free
                the ruv when it is no longer is in use
   Return:      CL5_SUCCESS if function is successful
                CL5_BAD_STATE if the changelog is not initialized;
                CL5_BAD_DATA - if NULL id is supplied
                CL5_NOTFOUND, if changelog file for replica is not found
 */
int cl5GetUpperBoundRUV(Replica *r, RUV **ruv);

/* Name:        cl5ExportLDIF
   Description: dumps changelog to an LDIF file; changelog can be open or closed.
   Parameters:  clDir - changelog dir
                ldifFile - full path to ldif file to write
                replicas - optional list of replicas whose changes should be exported;
                           if the list is NULL, entire changelog is exported.
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_BAD_STATE if changelog is not initialized;
                CL5_DB_ERROR if db api fails;
                CL5_SYSTEM_ERROR if NSPR call fails;
                CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5ExportLDIF(const char *ldifFile, Replica *replica);

/* Name:        cl5ImportLDIF
   Description: imports ldif file into changelog; changelog must be in the closed state
   Parameters:  clDir - changelog dir
                ldifFile - absolute path to the ldif file to import
                replicas - optional list of replicas whose data should be imported;
                           if the list is NULL, all data in the file is imported.
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_BAD_STATE if changelog is open or not initialized;
                CL5_DB_ERROR if db api fails;
                CL5_SYSTEM_ERROR if NSPR call fails;
                CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5ImportLDIF(const char *clDir, const char *ldifFile, Replica *replica);

/* Name:        cl5ConfigTrimming
   Description: sets changelog trimming parameters
   Parameters:  maxEntries - maximum number of entries in the log;
                maxAge - maximum entry age;
                trimInterval - interval for changelog trimming.
   Return:      CL5_SUCCESS if successful;
                CL5_BAD_STATE if changelog has not been open
 */
int cl5ConfigTrimming(Replica *replica, int maxEntries, const char *maxAge, int trimInterval);

void cl5DestroyIterator(void *iterator);

/* Name:        cl5WriteOperationTxn
   Description: writes operation to changelog as part of a containing transaction
   Parameters:  repl_name - name of the replica to which operation applies
                repl_gen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
                txn - the containing transaction
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid op is passed;
                CL5_BAD_STATE if db has not been initialized;
                CL5_MEMORY_ERROR if memory allocation failed;
                CL5_DB_ERROR if any other db error occurred;
 */
int cl5WriteOperationTxn(cldb_Handle *cldb, const slapi_operation_parameters *op, void *txn);

/* Name:        cl5WriteOperation
   Description: writes operation to changelog
   Parameters:  repl_name - name of the replica to which operation applies
                repl_gen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid op is passed;
                CL5_BAD_STATE if db has not been initialized;
                CL5_MEMORY_ERROR if memory allocation failed;
                CL5_DB_ERROR if any other db error occurred;
 */
int cl5WriteOperation(cldb_Handle *cldb, const slapi_operation_parameters *op);

/* Name:        cl5CreateReplayIterator
   Description: creates an iterator that allows to retrieve changes that should
                to be sent to the consumer identified by ruv The iteration is performed by
                repeated calls to cl5GetNextOperationToReplay.
   Parameters:  replica - replica whose data we wish to iterate;
                ruv - consumer ruv;
                iterator - iterator to be passed to cl5GetNextOperationToReplay call
   Return:      CL5_SUCCESS, if function is successful;
                CL5_MISSING_DATA, if data that should be in the changelog is missing
                CL5_PURGED_DATA, if some data that consumer needs has been purged.
                Note that the iterator can be non null if the supplier contains
                some data that needs to be sent to the consumer
                CL5_NOTFOUND if the consumer is up to data with respect to the supplier
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_BAD_STATE  if db has not been open;
                CL5_DB_ERROR if any other db error occurred;
                CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5CreateReplayIterator(Private_Repl_Protocol *prp, const RUV *ruv, CL5ReplayIterator **iterator);
int cl5CreateReplayIteratorEx(Private_Repl_Protocol *prp, const RUV *consumerRuv, CL5ReplayIterator **iterator, ReplicaId consumerRID);


/* Name:        cl5GetNextOperationToReplay
   Description: retrieves next operation to be sent to the consumer and
                that was created on a particular supplier. Consumer and supplier info
                is encoded in the iterator parameter that must be created by calling
                to cl5CreateIterator.
   Parameters:  iterator - iterator that identifies next entry to retrieve;
                op - operation retrieved if function is successful
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_NOTFOUND if end of iteration list is reached
                CL5_DB_ERROR if any other db error occurred;
                CL5_BADFORMAT if data in db is of unrecognized format;
                CL5_MEMORY_ERROR if memory allocation fails.
 */
int cl5GetNextOperationToReplay(CL5ReplayIterator *iterator,
                                CL5Entry *entry);

/* Name:        cl5DestroyReplayIterator
   Description: destroys iterator
   Parameters:  iterator - iterator to destroy
   Parameters:  replica for changelog info
   Return:      none
 */
void cl5DestroyReplayIterator(CL5ReplayIterator **iterator, Replica *replica);

/* Name:        cl5GetLdifDir
   Description: returns the default ldif directory; must be freed by the caller;
   Parameters:  backend used for export/import
   Return:      copy of the directory; caller needs to free the string
 */

char *cl5GetLdifDir(Slapi_Backend *be);

/* Name: cl5GetOperationCount
   Description: returns number of entries in the changelog. The changelog must be
                open for the value to be meaningful.
   Parameters:  replica - optional parameter that specifies the replica whose operations
                we wish to count; if NULL all changelog entries are counted
   Return:      number of entries in the changelog
 */

int cl5GetOperationCount(Replica *replica);

/* Name: cl5_operation_parameters_done
   Description: frees all parameters that are not freed by operation_parameters_done
                function in the server.

 */

void cl5_operation_parameters_done(struct slapi_operation_parameters *sop);

/* Name: cl5CreateDirIfNeeded
   Description: Create the directory if it doesn't exist yet
   Parameters:  dir - Contains the name of the directory to create. Must not be NULL
   Return:      CL5_SUCCESS if succeeded or existed,
                CL5_SYSTEM_ERROR if failed.
*/

int cl5CreateDirIfNeeded(const char *dir);
int cl5DBData2Entry(const char *data, PRUint32 len, CL5Entry *entry, void *clcrypt_handle);

PRBool cl5HelperEntry(const char *csnstr, CSN *csn);
CSN **cl5BuildCSNList(const RUV *consRuv, const RUV *supRuv);
void cl5DestroyCSNList(CSN ***csns);

int cl5Export(Slapi_PBlock *pb);
int cl5Import(Slapi_PBlock *pb);

int cl5NotifyRUVChange(Replica *replica);

void cl5CleanRUV(ReplicaId rid, Replica *replica);
void cl5NotifyCleanup(int rid);
void trigger_cl_purging(cleanruv_purge_data *purge_data);
int cldb_SetReplicaDB(Replica *replica, void *arg);
int cldb_UnSetReplicaDB(Replica *replica, void *arg);
int cldb_StartTrimming(Replica *replica);
int cldb_StopTrimming(Replica *replica, void *arg);
int cldb_StopThreads(Replica *replica, void *arg);
int32_t cldb_is_open(Replica *replica);

#endif
