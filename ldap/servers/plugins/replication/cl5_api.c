/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2010 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* cl5_api.c - implementation of 5.0 style changelog API */

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>
#if defined(OS_solaris) || defined(hpux)
#include <sys/types.h>
#include <sys/statvfs.h>
#endif
#if defined(linux)
#include <sys/vfs.h>
#endif
#include "slap.h"


#include "cl5.h"
#include "cl_crypt.h"
#include "plhash.h"
#include "plstr.h"
#include <pthread.h>
#include "cl5_clcache.h" /* To use the Changelog Cache */
#include "repl5.h"       /* for agmt_get_consumer_rid() */

#define GUARDIAN_FILE "guardian" /* name of the guardian file */
#define VERSION_FILE "DBVERSION" /* name of the version file  */
#define V_5 5                    /* changelog entry version */
#define V_6 6                    /* changelog entry version that includes encrypted flag */
#define CHUNK_SIZE 64 * 1024
#define DBID_SIZE 64
#define FILE_SEP "_" /* separates parts of the db file name */

#define T_CSNSTR "csn"
#define T_UNIQUEIDSTR "nsuniqueid"
#define T_PARENTIDSTR "parentuniqueid"
#define T_NEWSUPERIORDNSTR "newsuperiordn"
#define T_NEWSUPERIORIDSTR "newsuperioruniqueid"
#define T_REPLGEN "replgen"

#define ENTRY_COUNT_TIME 111 /* this time is used to construct csn \
                                used to store/retrieve entry count */
#define PURGE_RUV_TIME 222   /* this time is used to construct csn \
                                used to store purge RUV vector */
#define MAX_RUV_TIME 333     /* this time is used to construct csn \
                                used to store upper boundary RUV vector */

#define HASH_BACKETS_COUNT 16 /* number of buckets in a hash table */

#define TXN_BEGIN(cldb, parent_txn, tid, flags) \
    dblayer_dbi_txn_begin((cldb)->be, (cldb)->dbEnv, (flags), (parent_txn), tid)
#define TXN_COMMIT(cldb, txn) dblayer_dbi_txn_commit((cldb)->be, (txn))
#define TXN_ABORT(cldb, txn) dblayer_dbi_txn_abort((cldb)->be, (txn))

/*
 * The defult thread stacksize for nspr21 is 64k. For OSF, we require
 * a larger stacksize as actual storage allocation is higher i.e
 * pointers are allocated 8 bytes but lower 4 bytes are used.
 * The value 0 means use the default stacksize.
 */
#if defined(__LP64__) || defined(_LP64) /* 64-bit architectures need bigger stacks */
#if defined(__hpux) && defined(__ia64)
#define DEFAULT_THREAD_STACKSIZE 524288L
#else
#define DEFAULT_THREAD_STACKSIZE 131072L
#endif
#else
#define DEFAULT_THREAD_STACKSIZE 0
#endif

#define DIR_CREATE_MODE 0755

#define NO_DISK_SPACE 1024
#define MIN_DISK_SPACE 10485760 /* 10 MB */

#define MAX_RETRIES 10 /* Maximum number of retry in case of db retryable error */
#define CL5_TRIM_MAX_PER_TRANSACTION 100
#define CL5_TRIM_MAX_LOOKUP_PER_TRANSACTION 10000
#define CL5_PURGE_MAX_PER_TRANSACTION 1000
#define CL5_PURGE_MAX_LOOKUP_PER_TRANSACTION 10000

/***** Data Definitions *****/

/* possible changelog open modes */
typedef enum {
    CL5_OPEN_NONE,            /* nothing specified */
    CL5_OPEN_NORMAL,          /* open for normal read/write use */
    CL5_OPEN_LDIF2CL,         /* open as part of ldif2cl: no locking,
                                 recovery, checkpointing */
} CL5OpenMode;

/* changelog trimming configuration */
typedef struct cl5config
{
    time_t maxAge;       /* maximum entry age in seconds */
    int maxEntries;      /* maximum number of entries across all changelog files */
    int trimInterval;    /* trimming interval */
    char *encryptionAlgorithm; /* nsslapd-encryptionalgorithm */
} CL5Config;

/* this structure represents one changelog file, Each changelog file contains
   changes applied to a single backend. Files are named by the database id */

struct cl5DBFileHandle
/* info about the changelog file in the main database environment */
/* usage as CL5DBFile, but for new implementation use a new struct
 * can be replaced later
 */
{
    dbi_db_t *db;           /* db handle to the changelog file */
    dbi_env_t *dbEnv;       /* db environment shared by all db files */
    char *ident;            /* identifier for changelog, used in error messages */
    int entryCount;         /* number of entries in the file  */
    CL5State dbState;       /* changelog current state */
    pthread_mutex_t stLock; /* lock that controls access to dbState/dbEnv */
    RUV *purgeRUV;          /* ruv to which the file has been purged */
    RUV *maxRUV;            /* ruv that marks the upper boundary of the data */
    CL5Config clConf;       /* trimming and encryption config */
    Slapi_Counter *clThreads; /* track threads operating on the changelog */
    pthread_mutex_t clLock; /* controls access to trimming configuration  and */
                            /* lock associated to clVar, used to notify threads on close */
    int32_t trimmingOnGoing; /* it is a flag to indicate that a trimming thread is started
                              * and to prevent another trimming thread to start
                              */
    pthread_cond_t clCvar; /* Condition Variable used to notify threads on close */
    pthread_condattr_t clCAttr; /* the pthread condition attr */
    void *clcrypt_handle;   /* for cl encryption */
    CL5OpenMode dbOpenMode; /* how we open db */
    int32_t deleteFile;     /* Mark the changelog to be deleted */
    Slapi_Backend *be;      /* Backend (for dbimpl API) */
};

/* structure that allows to iterate through entries to be sent to a consumer
   that originated on a particular supplier. */
struct cl5replayiterator
{
    cldb_Handle	*it_cldb;
    CLC_Buffer *clcache;    /* changelog cache */
    ReplicaId consumerRID;  /* consumer's RID */
    const RUV *consumerRuv; /* consumer's update vector */
    Object *supplierRuvObj; /* supplier's update vector object */
    char starting_csn[CSN_STRSIZE];
};

typedef struct cl5iterator
{
    dbi_cursor_t cursor;  /* current position in the db file */
    cldb_Handle *it_cldb; /* handle to release db file object */
} CL5Iterator;

typedef void (*VFP)(void *);

/*
 * structures used to store transiently some data when working
 * on RUV. They are faster than the standard RUV access because
 * there is no lock involved and rids are sorted and
 * it requires a single malloc block.
 */
typedef struct {
    ReplicaId rid;
    char new;
    CSN mincsn;
    CSN maxcsn;
} RID_INFO;

typedef struct {
    int nb;                          /* numbers of event in current txn */
    int nbmax;                       /* maximum number of event before closing txn */
    long tot;                        /* total numbers of event */
} DBLCI_EVENT_COUNT;

/* context for dblayer cursor iterator callbacks */
typedef struct {
    struct cl5DBFileHandle *cldb;
    PRFileDesc *exportFile;           /* Specific to _cl5ExportFile */
    dbi_cursor_t cursor;
    CSN startcsn;
    CSN csn;
    long numToTrim;                   /* Specific to _cl5TrimReplica */
    Replica *r;                       /* Specific to _cl5TrimReplica */
    RUV *ruv;                         /* Specific to _cl5TrimReplica */
    RID_INFO *rids;                   /* csn per rid list */
    int nb_rids;                      /* csn per rid list size */
    int max_rids;                     /* csn per rid list max size */
    DBLCI_EVENT_COUNT changed;        /* records changed */
    DBLCI_EVENT_COUNT seen;           /* records seen */
    PRBool finished;                  /* Tells whether iteration should stop */
    ReplicaId rid2purge;              /* Specific to _cl5PurgeRid */
} DBLCI_CTX;

/***** Forward Declarations *****/

/* changelog initialization and cleanup */
static int _cldb_CheckAndSetEnv(Slapi_Backend *be, cldb_Handle *cldb);
static void _cl5DBClose(void);

/* thread management */
static int _cl5DispatchTrimThread(Replica *replica);

/* functions that work with individual changelog files */
static int _cl5ExportFile(PRFileDesc *prFile, cldb_Handle *cldb);

/* data storage and retrieval */
static int _cl5Entry2DBData(const CL5Entry *entry, char **data, PRUint32 *len, void *clcrypt_handle);
static int _cl5WriteOperation(cldb_Handle *cldb, const slapi_operation_parameters *op);
static int _cl5WriteOperationTxn(cldb_Handle *cldb, const slapi_operation_parameters *op, void *txn);
static const char *_cl5OperationType2Str(int type);
static int _cl5Str2OperationType(const char *str);
static void _cl5WriteString(const char *str, char **buff);
static void _cl5ReadString(char **str, char **buff);
static void _cl5WriteMods(LDAPMod **mods, char **buff, void *clcrypt_handle);
static int _cl5WriteMod(LDAPMod *mod, char **buff, void *clcrypt_handle);
static int _cl5ReadMods(LDAPMod ***mods, char **buff, void *clcrypt_handle);
static int _cl5ReadMod(Slapi_Mod *mod, char **buff, void *clcrypt_handle);
static int _cl5GetModsSize(LDAPMod **mods);
static int _cl5GetModSize(LDAPMod *mod);
static void _cl5ReadBerval(struct berval *bv, char **buff);
static void _cl5WriteBerval(struct berval *bv, char **buff);
static int _cl5ReadBervals(struct berval ***bv, char **buff, unsigned int size);
static int _cl5WriteBervals(struct berval **bv, char **buff, size_t *size);
static int32_t _cl5CheckMaxRUV(cldb_Handle *cldb, RUV *maxruv);
static int32_t _cl5CheckCSNinCL(const ruv_enum_data *element, void *arg);

/* replay iteration */
#ifdef FOR_DEBUGGING
static PRBool _cl5ValidReplayIterator(const CL5ReplayIterator *iterator);
#endif
static int _cl5PositionCursorForReplay(ReplicaId consumerRID, const RUV *consumerRuv, Replica *replica, CL5ReplayIterator **iterator, int *continue_on_missing);
static int _cl5CheckMissingCSN(const CSN *minCsn, const RUV *supplierRUV, cldb_Handle *cldb);

/* changelog trimming */
static int cldb_IsTrimmingEnabled(cldb_Handle *cldb);
static int _cl5TrimMain(void *param);
void _cl5TrimReplica(Replica *r);
void _cl5PurgeRID(cldb_Handle *cldb,  ReplicaId cleaned_rid);
static PRBool _cl5CanTrim(time_t time, long *numToTrim, Replica *replica, CL5Config *dbTrim);
int _cl5ConstructRUVs (cldb_Handle *cldb);
int _cl5ReadRUVs(cldb_Handle *cldb);
static int _cl5WriteRUV(cldb_Handle *cldb, PRBool purge);
static int _cl5UpdateRUV (cldb_Handle *cldb, CSN *csn, PRBool newReplica, PRBool purge);
static int _cl5GetRUV2Purge2(Replica *r, RUV **ruv);
void trigger_cl_purging_thread(void *rid);

/* bakup/recovery, import/export */
static int _cl5LDIF2Operation(char *ldifEntry, slapi_operation_parameters *op, char **replGen);
static int _cl5Operation2LDIF(const slapi_operation_parameters *op, const char *replGen, char **ldifEntry, PRInt32 *lenLDIF);

/* entry count */
static int _cl5GetEntryCount(cldb_Handle *cldb);
static int _cl5WriteEntryCount(cldb_Handle *cldb);

/* misc */
static char *_cl5GetHelperEntryKey(int type, char *csnStr);


static int _cl5WriteReplicaRUV(Replica *r, void *arg);

/***** Module APIs *****/

/* Name:        cl5Init
   Description:    initializes changelog module; must be called by a single thread
                before any other changelog function.
   Parameters:  none
   Return:        CL5_SUCCESS if function is successful;
                CL5_SYSTEM_ERROR error if NSPR call fails.
 */
int
cl5Init(void)
{
    if ((clcache_init() != 0)) {
        return CL5_SYSTEM_ERROR;
    }

    return CL5_SUCCESS;
}

/* Name:        cl5Open
   Description:    opens each changelog; must be called after changelog is
                initialized using cl5Init. It is thread safe and the second
                call is ignored.
   Return:        CL5_SUCCESS if successful;
                CL5_BAD_DATA if invalid directory is passed;
                CL5_BAD_STATE if changelog is not initialized;
                CL5_BAD_DBVERSION if dbversion file is missing or has unexpected data
                CL5_SYSTEM_ERROR if NSPR error occurred (during db directory creation);
                CL5_MEMORY_ERROR if memory allocation fails;
                CL5_DB_ERROR if db initialization fails.
 */
int
cl5Open(void)
{
    replica_enumerate_replicas(cldb_SetReplicaDB, NULL);

    /* init the clcache */
    if ((clcache_init() != 0)) {
        cl5Close();
        return CL5_SYSTEM_ERROR;
    }

    clcache_set_config();

    return CL5_SUCCESS;
}

/* Name:        cl5Close
 * Description: closes changelog; waits until all threads are done using changelog;
 *              call is ignored if changelog is already closed.
 * Parameters:  none
 * Return:      CL5_SUCCESS if successful;
 *              CL5_BAD_STATE if db is not in the open or closed state;
 *              CL5_SYSTEM_ERROR if NSPR call fails;
 *              CL5_DB_ERROR if db shutdown fails
 */
int
cl5Close()
{
    int32_t write_ruv = 1;

    replica_enumerate_replicas(cldb_UnSetReplicaDB, (void *)&write_ruv);
    /* There should now be no threads accessing any of the changelog databases -
     * it is safe to remove those databases */
    _cl5DBClose();

    return CL5_SUCCESS;
}

static int
_cldb_DeleteDB(Replica *replica)
{
    int rc = 0;
    cldb_Handle *cldb;
    Slapi_Backend *be;

    cldb = replica_get_cl_info(replica);
    /* make sure that changelog stays open while operation is in progress */

    slapi_counter_increment(cldb->clThreads);

    be = slapi_be_select(replica_get_root(replica));

    slapi_back_ctrl_info(be, BACK_INFO_DBENV_CLDB_REMOVE, (void *)(cldb->db));
    cldb->db = NULL;

    slapi_counter_decrement(cldb->clThreads);
    return rc;
}

int
cldb_RemoveReplicaDB(Replica *replica)
{
    int rc = 0;
    cldb_Handle *cldb = replica_get_cl_info(replica);

    cldb->deleteFile = 1;
    rc = cldb_UnSetReplicaDB(replica, NULL);

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
int
cl5GetUpperBoundRUV(Replica *r, RUV **ruv)
{
    int rc = CL5_SUCCESS;
    cldb_Handle *cldb = replica_get_cl_info(r);

    if (r == NULL || ruv == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5GetUpperBoundRUV - Invalid parameters\n");
        return CL5_BAD_DATA;
    }

    /* check if changelog is initialized */
    pthread_mutex_lock(&(cldb->stLock));
    if (cldb->dbState == CL5_STATE_CLOSED) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5GetUpperBoundRUV - Changelog is not initialized\n");
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_BAD_STATE;
    }

    /* make sure that changelog stays open while operation is in progress */
    slapi_counter_increment(cldb->clThreads);
    PR_ASSERT(cldb && cldb->maxRUV);
    *ruv = ruv_dup(cldb->maxRUV);
    slapi_counter_decrement(cldb->clThreads);

    pthread_mutex_unlock(&(cldb->stLock));

    return rc;
}


/* Name:        cl5ExportLDIF
   Description:    dumps changelog to an LDIF file; changelog can be open or closed.
   Parameters:  clDir - changelog dir
                ldifFile - full path to ldif file to write
                replicas - optional list of replicas whose changes should be exported;
                           if the list is NULL, entire changelog is exported.
   Return:      CL5_SUCCESS if function is successful;
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_BAD_STATE if changelog is not initialized;
                CL5_DB_ERROR if db api fails;
                CL5_SYSTEM_ERROR if NSPR call fails;
                CL5_MEMORY_ERROR if memory allocation fials.
 */
int
cl5ExportLDIF(const char *ldifFile, Replica *replica)
{
    PRFileDesc *prFile = NULL;
    cldb_Handle *cldb = replica_get_cl_info(replica);
    int rc;

    if (ldifFile == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ExportLDIF - null ldif file name\n");
        return CL5_BAD_DATA;
    }

    pthread_mutex_lock(&(cldb->stLock));
    if (cldb->dbState != CL5_STATE_OPEN) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
             "cl5ExportLDIF - Changelog is unavailable (%s)\n",
             cldb->dbState == CL5_STATE_IMPORT ? "import in progress" : "changelog is closed");
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_BAD_STATE;
    }

    /* make sure that changelog is open while operation is in progress */
    slapi_counter_increment(cldb->clThreads);
    pthread_mutex_unlock(&(cldb->stLock));

    prFile = PR_Open(ldifFile, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0600);
    if (prFile == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ExportLDIF - Failed to open (%s) file; NSPR error - %d\n",
                      ldifFile, PR_GetError());
        rc = CL5_SYSTEM_ERROR;
        goto done;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name_cl,
                  "cl5ExportLDIF: starting changelog export to (%s) ...\n", ldifFile);

    rc = _cl5ExportFile(prFile, cldb);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl, "cl5ExportLDIF - "
                      "failed to locate changelog file for replica at (%s)\n",
                      slapi_sdn_get_dn (replica_get_root (replica)));
    }

done:
    if (rc == CL5_SUCCESS)
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name_cl,
                      "cl5ExportLDIF - Changelog export is finished.\n");

    if (prFile)
        PR_Close(prFile);

    slapi_counter_decrement(cldb->clThreads);

    return rc;
}

/* Name:        cl5ImportLDIF
   Description:    imports ldif file into changelog; changelog must be in the closed state
   Parameters:  clDir - changelog dir
                ldifFile - absolute path to the ldif file to import
                replicas - list of replicas whose data should be imported.
   Return:        CL5_SUCCESS if function is successfull;
                CL5_BAD_DATA if invalid parameter is passed;
                CL5_BAD_STATE if changelog is open or not inititalized;
                CL5_DB_ERROR if db api fails;
                CL5_SYSTEM_ERROR if NSPR call fails;
                CL5_MEMORY_ERROR if memory allocation fials.
 */
int
cl5ImportLDIF(const char *clDir, const char *ldifFile, Replica *replica)
{
    LDIFFP *file = NULL;
    int buflen = 0;
    ldif_record_lineno_t lineno = 0;
    int rc = 0;
    char *buff = NULL;
    slapi_operation_parameters op;
    char *replGen = NULL;
    int purgeidx = 0;
    int maxidx = 0;
    int maxpurgesz = 0;
    int maxmaxsz = 0;
    struct berval **purgevals = NULL;
    struct berval **maxvals = NULL;
    int entryCount = 0;
    cldb_Handle *cldb = NULL;
    dbi_db_t *pDB = NULL;
    Slapi_Backend *be = NULL;
    Object *ruv_obj = NULL;

    /* validate params */
    if (ldifFile == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ImportLDIF - null ldif file name\n");
        return CL5_BAD_DATA;
    }

    if (NULL == replica) {
        /* Never happens for now. (see replica_execute_ldif2cl_task) */
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ImportLDIF - empty replica list\n");
        return CL5_BAD_DATA;
    }

    cldb = replica_get_cl_info(replica);
    if (cldb->dbState != CL5_STATE_OPEN) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ImportLDIF - Changelog is not initialized\n");
        return CL5_BAD_STATE;
    }

    /* open LDIF file */
    file = ldif_open(ldifFile, "r");
    if (file == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ImportLDIF - Failed to open (%s) ldif file; system error - %d\n",
                      ldifFile, errno);
        rc = CL5_SYSTEM_ERROR;
        goto done;
    }

    /* Set changelog state to import */
    pthread_mutex_lock(&(cldb->stLock));
    cldb->dbState = CL5_STATE_IMPORT;
    pthread_mutex_unlock(&(cldb->stLock));

    /* Wait for all the threads to stop */
    cldb_StopThreads(replica, NULL);

    /* Remove the old changelog */
    _cldb_DeleteDB(replica);

    /* Create new changelog */
    be = slapi_be_select(replica_get_root(replica));
    ruv_obj = replica_get_ruv(replica);

    pthread_mutex_lock(&(cldb->stLock));
    slapi_back_get_info(be, BACK_INFO_DBENV_CLDB, (void **)&pDB);
    cldb->db = pDB;
    cldb->be = be;
    cldb->dbOpenMode = CL5_OPEN_LDIF2CL;
    slapi_ch_free_string(&cldb->ident);
    cldb->ident = ruv_get_replica_generation((RUV*)object_get_data (ruv_obj));
    if (_cldb_CheckAndSetEnv(be, cldb) != CL5_SUCCESS) {
        object_release(ruv_obj);
        cldb->dbState = CL5_STATE_CLOSED;
        cldb->dbOpenMode = CL5_OPEN_NONE;
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_SYSTEM_ERROR;
    }
    ruv_destroy(&cldb->maxRUV);
    ruv_destroy(&cldb->purgeRUV);
    _cl5ReadRUVs(cldb);
    pthread_mutex_unlock(&(cldb->stLock));

    object_release(ruv_obj);

    /* read entries and write them to changelog */
    while (ldif_read_record(file, &lineno, &buff, &buflen))
    {
        rc = _cl5LDIF2Operation(buff, &op, &replGen);
        if (rc != CL5_SUCCESS) {
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
                if (0 == strcasecmp(type.bv_val, "clpurgeruv")) {
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
                else if (0 == strcasecmp(type.bv_val, "clmaxruv")) {
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
            buflen = 0;
            goto next;
        }
        assert(replGen); /* For coverity */
        slapi_ch_free_string(&buff);
        buflen = 0;
        /* check if the operation should be written to changelog */
        if (0 == strcmp(replGen, cldb->ident)) {
            /*
             * changetype: delete
             * replgen: 4d13a124000000010000
             * csn: 4d23b909000000020000
             * nsuniqueid: 00000000-00000000-00000000-00000000
             * dn: cn=start iteration
             */
            rc = _cl5WriteOperation(cldb, &op);
            if (rc != CL5_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                              "cl5ImportLDIF - "
                              "Failed to write operation to the changelog: "
                              "type: %lu, dn: %s\n",
                              op.operation_type, REPL_GET_DN(&op.target_address));
                slapi_ch_free_string(&replGen);
                operation_parameters_done(&op);
                goto done;
            }
            entryCount++;
            goto next;
        }

    next:
        slapi_ch_free_string(&replGen);
        operation_parameters_done(&op);
    }

    /* Set RUVs and entry count */
    if (purgeidx > 0) {
        ruv_destroy(&cldb->purgeRUV);
        rc = ruv_init_from_bervals(purgevals, &cldb->purgeRUV);
    }
    if (maxidx > 0) {
        ruv_destroy(&cldb->maxRUV);
        rc = ruv_init_from_bervals(maxvals, &cldb->maxRUV);
    }

    cldb->entryCount = entryCount;

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
        ldif_close(file);
    }

    /* All done, reset the clcache, state, and mode */
    pthread_mutex_lock(&(cldb->stLock));

    clcache_destroy();
    clcache_init();
    clcache_set_config();
    cldb->dbState = CL5_STATE_OPEN;
    cldb->dbOpenMode = CL5_OPEN_NORMAL;

    pthread_mutex_unlock(&(cldb->stLock));

    return rc;
}

/* Name:        cl5ConfigTrimming
   Description:    sets changelog trimming parameters; changelog must be open.
   Parameters:  maxEntries - maximum number of entries in the changelog (in all files);
                maxAge - maximum entry age;
                trimInterval - changelog trimming interval.
   Return:        CL5_SUCCESS if successful;
                CL5_BAD_STATE if changelog is not open
 */
int
cl5ConfigTrimming(Replica *replica, int maxEntries, const char *maxAge, int trimInterval)
{
    int isTrimmingEnabledBefore = 0;
    int isTrimmingEnabledAfter = 0;
    cldb_Handle *cldb = replica_get_cl_info(replica);

    if (!cldb) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ConfigTrimming - Changelog info was NULL - is your replication configuration valid?\n");
        return CL5_BAD_STATE;
    }

    if (cldb->dbState == CL5_STATE_CLOSED) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5ConfigTrimming - Changelog is not initialized\n");
        return CL5_BAD_STATE;
    }

    slapi_counter_increment(cldb->clThreads);
    /* make sure changelog is not closed while trimming configuration is updated.*/

    pthread_mutex_lock(&(cldb->clLock));

    isTrimmingEnabledBefore = cldb_IsTrimmingEnabled(cldb);

    if (maxAge) {
        /* don't ignore this argument */
        if (strcmp(maxAge, CL5_STR_IGNORE) != 0) {
            cldb->clConf.maxAge = slapi_parse_duration(maxAge);
        }
    } else {
        /* unlimited */
        cldb->clConf.maxAge = 0;
    }

    if (maxEntries != CL5_NUM_IGNORE) {
        cldb->clConf.maxEntries = maxEntries;
    }

    if (trimInterval != CL5_NUM_IGNORE) {
        cldb->clConf.trimInterval = trimInterval;
    }

    isTrimmingEnabledAfter = cldb_IsTrimmingEnabled(cldb);

    if (isTrimmingEnabledAfter && !isTrimmingEnabledBefore) {
        /* start trimming */
        cldb_StartTrimming(replica);
    } else if (!isTrimmingEnabledAfter && isTrimmingEnabledBefore) {
        /* stop trimming */
        cldb_StopTrimming(replica, NULL);
    } else {
        /* The config was updated, notify the changelog trimming thread */
        pthread_cond_broadcast(&(cldb->clCvar));
    }

    pthread_mutex_unlock(&(cldb->clLock));

    slapi_counter_decrement(cldb->clThreads);

    return CL5_SUCCESS;
}

/* Name:        cl5DestroyIterator
   Description: destroys iterator once iteration through changelog is done
   Parameters:  iterator - iterator to destroy
   Return:        none
 */
void
cl5DestroyIterator(void *iterator)
{
    CL5Iterator *it = (CL5Iterator *)iterator;

    if (it == NULL)
        return;

    /* close cursor */
    dblayer_cursor_op(&it->cursor, DBI_OP_CLOSE, NULL, NULL);

    /* NOTE (LK) locking of CL files  ?*/
    /*
    if (it->file)
        object_release(it->file);
    */

    slapi_ch_free((void **)&it);
}

/* Name:        cl5WriteOperationTxn
   Description:    writes operation to changelog
   Parameters:  replName - name of the replica to which operation applies
                replGen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
                txn - the transaction containing this operation
   Return:        CL5_SUCCESS if function is successfull;
                CL5_BAD_DATA if invalid op is passed;
                CL5_BAD_STATE if db has not been initialized;
                CL5_MEMORY_ERROR if memory allocation failed;
                CL5_DB_ERROR if any other db error occured;
 */
int
cl5WriteOperationTxn(cldb_Handle *cldb, const slapi_operation_parameters *op, void *txn)
{
    int rc;

    if (op == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5WriteOperationTxn - NULL operation passed\n");
        return CL5_BAD_DATA;
    }

    if (!IsValidOperation(op)) {
        return CL5_BAD_DATA;
    }

    if (cldb == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5WriteOperationTxn - changelog is not initialized\n");
        return CL5_BAD_DATA;
    }

    pthread_mutex_lock(&(cldb->stLock));
    if (cldb->dbState != CL5_STATE_OPEN) {
        if (cldb->dbState == CL5_STATE_IMPORT) {
            /* this is expected, no need to flood the logs with the same msg */
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                    "cl5WriteOperationTxn - Changelog is currently being initialized and can not be updated\n");
        } else {
            slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name_cl,
                    "cl5WriteOperationTxn - Changelog is not initialized\n");
        }
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_BAD_STATE;
    }
    /* make sure that changelog is open while operation is in progress */
    slapi_counter_increment(cldb->clThreads);

    pthread_mutex_unlock(&(cldb->stLock));

    rc = _cl5WriteOperationTxn(cldb, op, txn);

    /* update the upper bound ruv vector */
    if (rc == CL5_SUCCESS) {
        rc = _cl5UpdateRUV(cldb, op->csn, PR_FALSE, PR_FALSE);
    }

    slapi_counter_decrement(cldb->clThreads);

    return rc;
}

/* Name:        cl5WriteOperation
   Description:    writes operation to changelog
   Parameters:  replName - name of the replica to which operation applies
                replGen - replica generation for the operation
                !!!Note that we pass name and generation rather than
                   replica object since generation can change while operation
                   is in progress (if the data is reloaded). !!!
                op - operation to write
   Return:        CL5_SUCCESS if function is successfull;
                CL5_BAD_DATA if invalid op is passed;
                CL5_BAD_STATE if db has not been initialized;
                CL5_MEMORY_ERROR if memory allocation failed;
                CL5_DB_ERROR if any other db error occured;
 */
int
cl5WriteOperation(cldb_Handle *cldb, const slapi_operation_parameters *op)
{
    return cl5WriteOperationTxn(cldb, op, NULL);
}

/* Name:        cl5CreateReplayIterator
   Description:    creates an iterator that allows to retrieve changes that should
                to be sent to the consumer identified by ruv. The iteration is performed by
                repeated calls to cl5GetNextOperationToReplay.
   Parameters:  replica - replica whose data we wish to iterate;
                ruv - consumer ruv;
                iterator - iterator to be passed to cl5GetNextOperationToReplay call
   Return:        CL5_SUCCESS, if function is successful;
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
int
cl5CreateReplayIteratorEx(Private_Repl_Protocol *prp, const RUV *consumerRuv, CL5ReplayIterator **iterator, ReplicaId consumerRID)
{
    int rc;
    Replica *replica;
    cldb_Handle *cldb;

    replica = prp->replica;
    if (replica == NULL || consumerRuv == NULL || iterator == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIteratorEx - Invalid parameter\n");
        return CL5_BAD_DATA;
    }

    *iterator = NULL;

    cldb = replica_get_cl_info(replica);
    if (cldb == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIteratorEx - Changelog is not available (NULL) for %s\n",
                      replica_get_name(replica)
        );
        return CL5_BAD_STATE;
    }

    pthread_mutex_lock(&(cldb->stLock));
    if (cldb->dbState != CL5_STATE_OPEN) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIteratorEx - Changelog is not available for %s (dbState: %d)\n",
                      replica_get_name(replica),
                      cldb->dbState);
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_BAD_STATE;
    }

    /* make sure that changelog is open while operation is in progress */
    slapi_counter_increment(cldb->clThreads);

    pthread_mutex_unlock(&(cldb->stLock));

    /* iterate through the ruv in csn order to find first supplier for which
       we can replay changes */		
    rc = _cl5PositionCursorForReplay (consumerRID, consumerRuv, replica, iterator, NULL);

    if (rc != CL5_SUCCESS) {
        /* release the thread. */
        slapi_counter_decrement(cldb->clThreads);
    }

    return rc;
}

/* cl5CreateReplayIterator is now a wrapper for cl5CreateReplayIteratorEx */
int
cl5CreateReplayIterator(Private_Repl_Protocol *prp, const RUV *consumerRuv, CL5ReplayIterator **iterator)
{

    /*    DBDB : I thought it should be possible to refactor this like so, but it seems to not work.
    Possibly the ordering of the calls is significant.
    ReplicaId consumerRID = agmt_get_consumer_rid ( prp->agmt, prp->conn );
    return cl5CreateReplayIteratorEx(prp,consumerRuv,iterator,consumerRID); */

    int rc;
    Replica *replica;
    cldb_Handle *cldb;

    replica = prp->replica;
    if (replica == NULL || consumerRuv == NULL || iterator == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIterator - Invalid parameter\n");
        return CL5_BAD_DATA;
    }

    *iterator = NULL;

    cldb = replica_get_cl_info(replica);
    if (cldb == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIterator - Changelog is not available (NULL) for %s\n",
                      replica_get_name(replica));
        return CL5_BAD_STATE;
    }
    pthread_mutex_lock(&(cldb->stLock));
    if (cldb->dbState != CL5_STATE_OPEN) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5CreateReplayIterator - Changelog is not available for %s (dbState: %d)\n",
                      replica_get_name(replica),
                      cldb->dbState);
        pthread_mutex_unlock(&(cldb->stLock));
        return CL5_BAD_STATE;
    }
    /* make sure that changelog is open while operation is in progress */
    slapi_counter_increment(cldb->clThreads);

    pthread_mutex_unlock(&(cldb->stLock));

    /* iterate through the ruv in csn order to find first supplier for which
       we can replay changes */
    ReplicaId consumerRID = agmt_get_consumer_rid(prp->agmt, prp->conn);
    int continue_on_missing = agmt_get_ignoremissing(prp->agmt);
    int save_cont_miss = continue_on_missing;
    rc = _cl5PositionCursorForReplay(consumerRID, consumerRuv, replica, iterator, &continue_on_missing);
    if (save_cont_miss == 1 && continue_on_missing == 0) {
        /* the option to continue once on a missing csn was used, rest */
        agmt_set_ignoremissing(prp->agmt, 0);
    }

    if (rc != CL5_SUCCESS) {
        /* release the thread */
        slapi_counter_decrement(cldb->clThreads);
    }

    return rc;
}

/* Name:        cl5GetNextOperationToReplay
   Description:    retrieves next operation to be sent to a particular consumer and
                that was created on a particular supplier. Consumer and supplier info
                is encoded in the iterator parameter that must be created by call
                to cl5CreateReplayIterator.
   Parameters:  iterator - iterator that identifies next entry to retrieve;
                op - operation retrieved if function is successful
   Return:        CL5_SUCCESS if function is successfull;
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
cl5GetNextOperationToReplay(CL5ReplayIterator *iterator, CL5Entry *entry)
{
    CSN *csn;
    char *key, *data;
    size_t keylen, datalen;
    char *agmt_name;
    int rc = 0;

    agmt_name = get_thread_private_agmtname();

    if (entry == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5GetNextOperationToReplay - %s - Invalid parameter passed\n", agmt_name);
        return CL5_BAD_DATA;
    }

    rc = clcache_get_next_change(iterator->clcache, (void **)&key, &keylen, (void **)&data, &datalen, &csn, iterator->starting_csn);

    if (rc == DBI_RC_NOTFOUND) {
        /*
         * Abort means we've figured out that we've passed the replica Min CSN,
         * so we should stop looping through the changelog
         */
        return CL5_NOTFOUND;
    }

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl, "cl5GetNextOperationToReplay - %s - "
                                                          "Failed to read next entry; DB error %d\n",
                      agmt_name, rc);
        return CL5_DB_ERROR;
    }

    if (is_cleaned_rid(csn_get_replicaid(csn))) {
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
    if (0 != cl5DBData2Entry(data, datalen, entry, iterator->it_cldb->clcrypt_handle)) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5GetNextOperationToReplay - %s - Failed to format entry rc=%d\n", agmt_name, rc);
        return rc;
    }

    return CL5_SUCCESS;
}

/* Name:        cl5DestroyReplayIterator
   Description:    destorys iterator
   Parameters:  iterator - iterator to destory
   Return:        none
 */
void
cl5DestroyReplayIterator(CL5ReplayIterator **iterator, Replica *replica)
{
    cldb_Handle *cldb;
    if (iterator == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5DestroyReplayIterator - Invalid iterator passed\n");
        return;
    }

    clcache_return_buffer(&(*iterator)->clcache);

    /* TBD (LK) lock/unlock cldb ?
     if ((*iterator)->it_cldb) {
        object_release((*iterator)->it_cldb);
        (*iterator)->it_cldb = NULL;
    }
    */

    /* release supplier's ruv */
    if ((*iterator)->supplierRuvObj) {
        object_release((*iterator)->supplierRuvObj);
        (*iterator)->supplierRuvObj = NULL;
    }

    slapi_ch_free((void **)iterator);

    /* this thread no longer holds a db reference, release it */
    cldb = replica_get_cl_info(replica);
    slapi_counter_decrement(cldb->clThreads);
}

/* Name: cl5GetOperationCount
   Description: returns number of entries in the changelog. The changelog must be
                open for the value to be meaningful.
   Parameters:  replica - optional parameter that specifies the replica whose operations
                we wish to count; if NULL all changelog entries are counted
   Return:        number of entries in the changelog
 */

int
cl5GetOperationCount(Replica *replica)
{
    int count = 0;
    cldb_Handle *cldb = replica_get_cl_info(replica);

    if (replica == NULL) /* compute total entry count */
    {
        /* TBD (LK) get count for all backends
        file_obj = objset_first_obj(s_cl5Desc.dbFiles);
        while (file_obj) {
            file = (CL5DBFile *)object_get_data(file_obj);
            PR_ASSERT(file);
            count += file->entryCount;
            file_obj = objset_next_obj(s_cl5Desc.dbFiles, file_obj);
        }
        */
        count = 0;
    } else if (cldb->dbState == CL5_STATE_CLOSED) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5GetOperationCount - Changelog is not initialized\n");
        return -1;
    } else /* return count for particular db */
    {
        slapi_counter_increment(cldb->clThreads);
        count = cldb->entryCount;
        slapi_counter_decrement(cldb->clThreads);
    }

    return count;
}

/***** Helper Functions *****/


int
cldb_UnSetReplicaDB(Replica *replica, void *arg)
{
    int rc = 0;
    cldb_Handle *cldb = replica_get_cl_info(replica);
    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));

    if (cldb == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cldb_UnSetReplicaDB: cldb is NULL (okay if this is a consumer)\n");
        return -1;
    }

    pthread_mutex_lock(&(cldb->stLock));
    cldb->dbState = CL5_STATE_CLOSED;
    pthread_mutex_unlock(&(cldb->stLock));

    /* cleanup trimming */
    cldb_StopThreads(replica, NULL);

    /* write or cleanup changelog ruvs */
    if (arg) {
        /* If arg is set we are shutting down and need to write the RUVs */
        _cl5WriteReplicaRUV(replica, NULL);
    } else {
        ruv_destroy(&cldb->maxRUV);
        ruv_destroy(&cldb->purgeRUV);
    }

    /* Cleanup the pthread mutexes and friends */
    pthread_mutex_destroy(&(cldb->stLock));
    pthread_mutex_destroy(&(cldb->clLock));
    pthread_condattr_destroy(&(cldb->clCAttr));
    pthread_cond_destroy(&(cldb->clCvar));

    /* Clear the cl encryption data (if configured) */
    (void) clcrypt_destroy(cldb->clcrypt_handle, be);

    if (cldb->deleteFile) {
        _cldb_DeleteDB(replica);
    }

    slapi_counter_destroy(&cldb->clThreads);

    rc = replica_set_cl_info(replica, NULL);

    slapi_ch_free_string(&cldb->ident);
    slapi_ch_free((void **)&cldb);

    return rc;
}

int
cldb_SetReplicaDB(Replica *replica, void *arg)
{
    int rc = -1;
    dbi_db_t *pDB = NULL;
    cldb_Handle *cldb = NULL;
    int openMode = 0;

    if (!replica_is_flag_set(replica, REPLICA_LOG_CHANGES)) {
        /* replica does not have a changelog */
        return 0;
    }

    if (arg) {
        openMode = *(int *)arg;
    }

    cldb = replica_get_cl_info(replica);
    if (cldb) {
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - DB already set to replica\n");
        return 0;
    }

    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));
    Object *ruv_obj = replica_get_ruv(replica);

    rc = slapi_back_get_info(be, BACK_INFO_DBENV_CLDB, (void **)&pDB);
    if (rc == 0) {
        cldb = (cldb_Handle *)slapi_ch_calloc(1, sizeof(cldb_Handle));
        cldb->db = pDB;
        cldb->be = be;
        cldb->ident = ruv_get_replica_generation((RUV*)object_get_data (ruv_obj));
        if (_cldb_CheckAndSetEnv(be, cldb) != CL5_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "cldb_SetReplicaDB - Failed to check be environment\n");
            /* Coverity false positive: cldb is not leaking has it is now linked in the backend context */
            /* coverity[leaked_storage] */
            return CL5_SYSTEM_ERROR;
        }
        _cl5ReadRUVs(cldb);
    }
    object_release(ruv_obj);

    assert(cldb); /* Always true but covscan logs false positive otherwise */
    if (arg) {
        cldb->dbOpenMode = openMode;
    } else {
        cldb->dbOpenMode = CL5_OPEN_NORMAL;
    }
    cldb->clThreads = slapi_counter_new();
    cldb->dbState = CL5_STATE_OPEN;
    cldb->trimmingOnGoing = 0;

    if (pthread_mutex_init(&(cldb->stLock), NULL) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - Failed to create on state lock\n");
        return CL5_SYSTEM_ERROR;
    }
    if (pthread_mutex_init(&(cldb->clLock), NULL) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - Failed to create on close lock\n");
        return CL5_SYSTEM_ERROR;
    }

    /* Set up the condition variable */
	pthread_condattr_init(&(cldb->clCAttr));
	pthread_condattr_setclock(&(cldb->clCAttr), CLOCK_MONOTONIC);
    if (pthread_cond_init(&(cldb->clCvar), &(cldb->clCAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - Failed to create cvar\n");
        return CL5_SYSTEM_ERROR;
    }
    replica_set_cl_info(replica, cldb);

    /* get cl configuration for backend */
    back_info_config_entry config_entry = {0};
    config_entry.dn = "cn=changelog";
    changelog5Config config = {};
    rc = slapi_back_ctrl_info(be, BACK_INFO_CLDB_GET_CONFIG, (void *)&config_entry);
    if (rc !=0 || config_entry.ce == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - failed to read config for changelog\n");
        return CL5_BAD_DATA;
    }

    changelog5_extract_config(config_entry.ce, &config);
    changelog5_register_config_callbacks(slapi_entry_get_dn_const(config_entry.ce), replica);
    slapi_entry_free(config_entry.ce);

    /* set trimming parameters */
    rc = cl5ConfigTrimming(replica, config.maxEntries, config.maxAge, config.trimInterval);
    if (rc != CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cldb_SetReplicaDB - failed to configure changelog trimming\n");
        return CL5_BAD_DATA;
    }

    /* Set the cl encryption algorithm (if configured) */
    if (config.encryptionAlgorithm) {
        cldb->clConf.encryptionAlgorithm = config.encryptionAlgorithm;
        cldb->clcrypt_handle = clcrypt_init(config.encryptionAlgorithm, be);
    }
    changelog5_config_done(&config);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
        "cldb_SetReplicaDB: cldb is set\n");

    return rc;
}

static int
cldb_IsTrimmingEnabled(cldb_Handle *cldb)
{
    if ((cldb->clConf.maxAge != 0 || cldb->clConf.maxEntries != 0) &&  cldb->clConf.trimInterval > 0) {
        return 1;
    } else {
        return 0;
    }
}

int
cldb_StartTrimming(Replica *replica)
{
    return _cl5DispatchTrimThread(replica);
}

int
cldb_StopTrimming(Replica *replica, void *arg)
{
    cldb_Handle *cldb = replica_get_cl_info(replica);

    /* we need to stop the changelog threads - trimming or purging */
    pthread_mutex_lock(&(cldb->clLock));
    pthread_cond_broadcast(&(cldb->clCvar));
    pthread_mutex_unlock(&(cldb->clLock));

    return 0;
}

int
cldb_StopThreads(Replica *replica, void *arg)
{
    cldb_Handle *cldb = replica_get_cl_info(replica);
    PRIntervalTime interval;
    uint64_t threads;

    /* we need to stop the changelog threads - trimming or purging */
    pthread_mutex_lock(&(cldb->clLock));
    pthread_cond_broadcast(&(cldb->clCvar));
    pthread_mutex_unlock(&(cldb->clLock));

    interval = PR_MillisecondsToInterval(100);
    while ((threads = slapi_counter_get_value(cldb->clThreads)) > 0) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cldb_StopThreads - Waiting for threads to exit: %" PRIu64 " thread(s) still active\n",
                      threads);
         DS_Sleep(interval);
    }
    return 0;
}

static int
_cldb_CheckAndSetEnv(Slapi_Backend *be, cldb_Handle *cldb)
{
    int rc = -1; /* initialize to failure */
    dbi_env_t *dbEnv = NULL;

    if (cldb->dbEnv) {
        /* dbEnv already set */
        return CL5_SUCCESS;
    }

    rc = slapi_back_get_info(be, BACK_INFO_DBENV, (void **)&dbEnv);

    if (rc == 0 && dbEnv) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "_cldb_CheckAndSetEnv - Fetched backend dbEnv (%p)\n", dbEnv);
        cldb->dbEnv = dbEnv;
        return CL5_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cldb_CheckAndSetEnv - Failed to fetch backend dbenv\n");
        return CL5_DB_ERROR;
    }
}

/* this function assumes that the entry was validated
   using IsValidOperation

   Data in db format:
   ------------------
   <1 byte version><1 byte change_type><sizeof time_t time><null terminated csn>
   <null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]

   Version 6 looks like this with the addition of "encrypted":
   <1 byte version><1 byte encrypted><1 byte change_type><sizeof time_t time><null terminated csn>
   <null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]


   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/
static int
_cl5Entry2DBData(const CL5Entry *entry, char **data, PRUint32 *len, void *clcrypt_handle)
{
    int size = 1 /* version */ + 1 /* operation type */ + sizeof(time_t);
    char *pos;
    PRUint32 t;
    slapi_operation_parameters *op;
    LDAPMod **add_mods = NULL;
    char *rawDN = NULL;
    char s[CSN_STRSIZE];

    PR_ASSERT(entry && entry->op && data && len);
    op = entry->op;
    PR_ASSERT(op->target_address.uniqueid);

    /* compute size of the buffer needed to hold the data */
    size += CSN_STRSIZE;
    size += strlen(op->target_address.uniqueid) + 1;

    switch (op->operation_type) {
    case SLAPI_OPERATION_ADD:
        if (op->p.p_add.parentuniqueid)
            size += strlen(op->p.p_add.parentuniqueid) + 1;
        else
            size++; /* we just store NULL char */
        slapi_entry2mods(op->p.p_add.target_entry, &rawDN /* dn */, &add_mods);
        size += strlen(rawDN) + 1;
        /* Need larger buffer for the encrypted changelog */
        if (clcrypt_handle) {
            size += (_cl5GetModsSize(add_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
        } else {
            size += _cl5GetModsSize(add_mods);
        }
        break;

    case SLAPI_OPERATION_MODIFY:
        size += REPL_GET_DN_LEN(&op->target_address) + 1;
        /* Need larger buffer for the encrypted changelog */
        if (clcrypt_handle) {
            size += (_cl5GetModsSize(op->p.p_modify.modify_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
        } else {
            size += _cl5GetModsSize(op->p.p_modify.modify_mods);
        }
        break;

    case SLAPI_OPERATION_MODRDN:
        size += REPL_GET_DN_LEN(&op->target_address) + 1;
        /* 1 for deleteoldrdn */
        size += strlen(op->p.p_modrdn.modrdn_newrdn) + 2;
        if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
            size += REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address) + 1;
        else
            size++; /* for NULL char */
        if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)
            size += strlen(op->p.p_modrdn.modrdn_newsuperior_address.uniqueid) + 1;
        else
            size++; /* for NULL char */
        /* Need larger buffer for the encrypted changelog */
        if (clcrypt_handle) {
            size += (_cl5GetModsSize(op->p.p_modrdn.modrdn_mods) * (1 + BACK_CRYPT_OUTBUFF_EXTLEN));
        } else {
            size += _cl5GetModsSize(op->p.p_modrdn.modrdn_mods);
        }
        break;

    case SLAPI_OPERATION_DELETE:
        size += REPL_GET_DN_LEN(&op->target_address) + 1;
        break;
    }

    /* allocate data buffer */
    (*data) = slapi_ch_malloc(size);
    if ((*data) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5Entry2DBData - Failed to allocate data buffer\n");
        return CL5_MEMORY_ERROR;
    }

    /* fill in the data buffer */
    pos = *data;
    /* write a byte of version */
    (*pos) = V_6;
    pos++;
    /* write the encryption flag */
    if (clcrypt_handle) {
        (*pos) = 1;
    } else {
        (*pos) = 0;
    }
    pos++;
    /* write change type */
    (*pos) = (unsigned char)op->operation_type;
    pos++;
    /* write time */
    t = PR_htonl((PRUint32)entry->time);
    memcpy(pos, &t, sizeof(t));
    pos += sizeof(t);
    /* write csn */
    _cl5WriteString(csn_as_string(op->csn, PR_FALSE, s), &pos);
    /* write UniqueID */
    _cl5WriteString(op->target_address.uniqueid, &pos);

    /* figure out what else we need to write depending on the operation type */
    switch (op->operation_type) {
    case SLAPI_OPERATION_ADD:
        _cl5WriteString(op->p.p_add.parentuniqueid, &pos);
        _cl5WriteString(rawDN, &pos);
        _cl5WriteMods(add_mods, &pos, clcrypt_handle);
        slapi_ch_free((void **)&rawDN);
        ldap_mods_free(add_mods, 1);
        break;

    case SLAPI_OPERATION_MODIFY:
        _cl5WriteString(REPL_GET_DN(&op->target_address), &pos);
        _cl5WriteMods(op->p.p_modify.modify_mods, &pos, clcrypt_handle);
        break;

    case SLAPI_OPERATION_MODRDN:
        _cl5WriteString(REPL_GET_DN(&op->target_address), &pos);
        _cl5WriteString(op->p.p_modrdn.modrdn_newrdn, &pos);
        *pos = (PRUint8)op->p.p_modrdn.modrdn_deloldrdn;
        pos++;
        _cl5WriteString(REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address), &pos);
        _cl5WriteString(op->p.p_modrdn.modrdn_newsuperior_address.uniqueid, &pos);
        _cl5WriteMods(op->p.p_modrdn.modrdn_mods, &pos, clcrypt_handle);
        break;

    case SLAPI_OPERATION_DELETE:
        _cl5WriteString(REPL_GET_DN(&op->target_address), &pos);
        break;
    }

    /* (*len) != size in case encrypted */
    (*len) = pos - *data;

    if (*len > size) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5Entry2DBData - real len %d > estimated size %d\n",
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

   Version 6 looks like this with the addition of "encrypted":
   <1 byte version><1 byte encrypted><1 byte change_type><sizeof time_t time><null terminated csn>
   <null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]


   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/


int
cl5DBData2Entry(const char *data, PRUint32 len __attribute__((unused)), CL5Entry *entry, void *clcrypt_handle)
{
    int rc;
    PRUint8 version;
    PRUint8 encrypted = 0;
    char *pos = (char *)data;
    char *strCSN;
    PRUint32 thetime;
    slapi_operation_parameters *op;
    LDAPMod **add_mods = NULL;
    char *rawDN = NULL;
    char s[CSN_STRSIZE];

    PR_ASSERT(data && entry && entry->op);
    op = entry->op;

    /* ONREPL - check that we do not go beyond the end of the buffer */

    /* read byte of version */
    version = (PRUint8)(*pos);
    if (version != V_5 && version != V_6) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5DBData2Entry - Invalid data version: %d\n", version);
        return CL5_BAD_FORMAT;
    }
    pos += sizeof(version);

    if (version == V_6) {
        /* In version 6 we set a flag to note if the changes are encrypted */
        encrypted = (PRUint8)(*pos);
        pos += sizeof(encrypted);
        if (!encrypted) {
            /* This cl entry is not encrypted, so don't try */
            clcrypt_handle = NULL;
        }
    }

    /* read change type */
    op->operation_type = (PRUint8)(*pos);
    pos++;

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&thetime, pos, sizeof(thetime));
    entry->time = (time_t)PR_ntohl(thetime);
    pos += sizeof(thetime);

    /* read csn */
    _cl5ReadString(&strCSN, &pos);
    assert(strCSN);
    if (op->csn == NULL || strcmp(strCSN, csn_as_string(op->csn, PR_FALSE, s)) != 0) {
        op->csn = csn_new_by_string(strCSN);
    }
    slapi_ch_free((void **)&strCSN);

    /* read UniqueID */
    _cl5ReadString(&op->target_address.uniqueid, &pos);

    /* figure out what else we need to read depending on the operation type */
    switch (op->operation_type) {
    case SLAPI_OPERATION_ADD:
        _cl5ReadString(&op->p.p_add.parentuniqueid, &pos);
        /* richm: need to free parentuniqueid */
        _cl5ReadString(&rawDN, &pos);
        op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
        /* convert mods to entry */
        rc = _cl5ReadMods(&add_mods, &pos, clcrypt_handle);
        slapi_mods2entry(&(op->p.p_add.target_entry), rawDN, add_mods);
        ldap_mods_free(add_mods, 1);
        break;

    case SLAPI_OPERATION_MODIFY:
        _cl5ReadString(&rawDN, &pos);
        op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
        rc = _cl5ReadMods(&op->p.p_modify.modify_mods, &pos, clcrypt_handle);
        break;

    case SLAPI_OPERATION_MODRDN:
        _cl5ReadString(&rawDN, &pos);
        op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
        _cl5ReadString(&op->p.p_modrdn.modrdn_newrdn, &pos);
        op->p.p_modrdn.modrdn_deloldrdn = *pos;
        pos++;
        _cl5ReadString(&rawDN, &pos);
        op->p.p_modrdn.modrdn_newsuperior_address.sdn = slapi_sdn_new_dn_passin(rawDN);
        _cl5ReadString(&op->p.p_modrdn.modrdn_newsuperior_address.uniqueid, &pos);
        rc = _cl5ReadMods(&op->p.p_modrdn.modrdn_mods, &pos, clcrypt_handle);
        break;

    case SLAPI_OPERATION_DELETE:
        _cl5ReadString(&rawDN, &pos);
        op->target_address.sdn = slapi_sdn_new_dn_passin(rawDN);
        rc = CL5_SUCCESS;
        break;

    default:
        rc = CL5_BAD_FORMAT;
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5DBData2Entry - Failed to format entry\n");
        break;
    }

    return rc;
}

/* Get entry time from changelog record data without decoding the whole operation */
int
cl5DBData2EntryTime(const char *data, time_t *entrytime)
{
    PRUint8 version;
    char *pos = (char *)data;
    PRUint32 thetime;

    PR_ASSERT(data && entrytime);

    /* ONREPL - check that we do not go beyond the end of the buffer */

    /* read byte of version */
    version = (PRUint8)(*pos);
    if (version != V_5 && version != V_6) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "cl5DBData2EntryTime - Invalid data version: %d\n", version);
        return CL5_BAD_FORMAT;
    }
    pos += sizeof(version);

    if (version == V_6) {
        /* In version 6 we set a flag to note if the changes are encrypted */
        pos += sizeof(PRUint8);
    }

    /* read change type */
    pos++;

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&thetime, pos, sizeof(thetime));
    *entrytime = (time_t)PR_ntohl(thetime);
    return CL5_SUCCESS;
}

/* Map the dbi layer error code to the CL5_ ones */
static int
_cl5Dberror(cldb_Handle *cldb, int rc, const char *errmsg)
{
    int clrc = CL5_SUCCESS;
    int loglvl = SLAPI_LOG_ERR;
    switch (rc) {
        case DBI_RC_RETRY:
            clrc = CL5_DB_RETRY;
            loglvl = SLAPI_LOG_REPL;
            break;
        case DBI_RC_NOTFOUND:
            clrc = CL5_NOTFOUND;
            break;
         case DBI_RC_SUCCESS:
            clrc = CL5_SUCCESS;
            errmsg = NULL;
            break;
        default:
            if (rc>0 && rc<CL5_LAST_ERROR_CODE) {
                /* Lets assume that:
                 * it is a CL5_ error already logged by the callback
                 */
                errmsg = NULL;
                clrc = rc;
            } else {
                clrc = CL5_DB_ERROR;
            }
            break;
    }
    if (errmsg) {
        slapi_log_err(loglvl, repl_plugin_name_cl,
                      "%s on changelog %s; db error - %d %s\n",
                      errmsg, cldb->ident, rc, dblayer_strerror(rc));
    }
    return clrc;
}

/* Iterate over a changelog cursor */
int
_cl5Iterate(cldb_Handle *cldb, dbi_iterate_cb_t *action_cb, DBLCI_CTX *dblcictx, PRBool readonly)
{
    dbi_txn_t *txnid = NULL;
    int rc = CL5_DB_RETRY;
    int nbtries = 0;
    CSN *startcsn = NULL;

    dblcictx->finished = PR_FALSE;
    dblcictx->cldb = cldb;
    while ( !slapi_is_shutting_down() &&
            ((rc == CL5_SUCCESS && dblcictx->finished == PR_FALSE) ||
             (rc == CL5_DB_RETRY && nbtries < MAX_RETRIES))) {
        nbtries++;
        dblcictx->changed.nb = 0;
        dblcictx->seen.nb = 0;
        if (rc == CL5_SUCCESS) {
            /* action_cb have decided to abort/commit the txn
             * then go on with the iteration
             */
            startcsn = &dblcictx->csn;
        }
        if (startcsn) {
            /* Save startcsn to be able to retry at the same place */
            dblcictx->startcsn = *startcsn;
            startcsn = &dblcictx->startcsn;
        }
        if (!readonly || dblayer_is_lmdb(cldb->be)) {
            /*
             * if write operation are performed or if lmdb is used, a transaction is needed.
             * In bdb case DB txn lock accessed pages until the end of the transaction.
             * in lmdb case the read or write lock is global and also held until the end
             * of the transaction.
             */
            rc = TXN_BEGIN(cldb, NULL, &txnid, readonly);
            if (rc != 0) {
                rc = _cl5Dberror(cldb, rc, "_cl5Iterate - Failed to begin transaction");
                continue;
            }
        } else {
            /* read-only opertion on bdb are transactionless, so no reason to abort txn 
             * after having seen some number of records 
             */
            dblcictx->seen.nbmax = 0;
        }
        /* create cursor */
        rc = dblayer_new_cursor(cldb->be, cldb->db, txnid, &dblcictx->cursor);
        if (rc != 0) {
            rc = _cl5Dberror(cldb, rc, "_cl5Iterate - Failed to create cursor");
            TXN_ABORT(cldb, txnid);
            continue;
        }
        if (startcsn) {
            char csnstr[CSN_STRSIZE] = "";
            dbi_val_t startingkey = {0};
            csn_as_string(startcsn, 0, csnstr);
            dblayer_value_set(cldb->be, &startingkey, csnstr, CSN_STRSIZE);
            rc = dblayer_cursor_iterate(&dblcictx->cursor, action_cb, &startingkey, dblcictx);
        } else {
            rc = dblayer_cursor_iterate(&dblcictx->cursor, action_cb, NULL, dblcictx);
        }
        if (dblcictx->seen.nbmax > 10 && rc == CL5_DB_LOCK_ERROR) {
            /*
             * Ran out of locks, need to restart the transaction.
             * Reduce the the batch count and reset the key to
             * the starting point.
             * PR: Not sure weither this code is still working because dbimpl API layer
             *  may remap bdb error 12 to something else.
             */
            dblcictx->seen.nbmax -= 10;
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "_cl5Iterate - Ran out of db locks while iterating on entries; "
                          "Reducing the batch value to %d and retry.\n", dblcictx->seen.nbmax);
            nbtries = 0;
            continue;
        }
        rc = _cl5Dberror(cldb, rc, NULL);
        if (rc != CL5_NOTFOUND && rc != CL5_SUCCESS) {
            dblcictx->changed.nb = 0; /* Tells to abort the txn */
            dblcictx->seen.nb = 0;
        }
        dblayer_cursor_op(&dblcictx->cursor, DBI_OP_CLOSE, NULL, NULL);
        dblcictx->changed.tot += dblcictx->changed.nb;
        dblcictx->seen.tot += dblcictx->seen.nb;

        if (txnid) {
            if (dblcictx->changed.nb) {
                int rc2 = TXN_COMMIT(cldb, txnid);
                if (rc2 != DBI_RC_SUCCESS) {
                    rc = _cl5Dberror(cldb, rc2, "_cl5Iterate - Failed to commit transaction");
                }
            } else {
                int rc2 = TXN_ABORT(cldb, txnid);
                if (rc2 != DBI_RC_SUCCESS) {
                    if (rc == CL5_SUCCESS || rc == CL5_NOTFOUND) {
                        rc = _cl5Dberror(cldb, rc2, "_cl5Iterate - Failed to abort transaction");
                    }
                }
            }
        }
    }
    if (rc == CL5_DB_RETRY && nbtries > MAX_RETRIES) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5Iterate - Too many db retries errors on changelog %s\n",
                      cldb->ident);
        rc = CL5_DB_ERROR;
    }
    return rc;
}

/* _cl5ConstructRUVs helper: Find or allocate sorted RID_INFO slot in DBLCI_CTX */
RID_INFO *
_cl5GetRidInfo(DBLCI_CTX *dblcictx, ReplicaId rid, PRBool addifmissing)
{
    int idx = 0;
    int idx_min = 0;
    int idx_max = dblcictx->nb_rids-1;
    while (idx_min <= idx_max) {
        idx = (idx_min + idx_max) / 2;
        if (dblcictx->rids[idx].rid == rid) {
            return &dblcictx->rids[idx];
        }
        if (dblcictx->rids[idx].rid > rid) {
            idx_max = idx-1;
        } else {
            idx_min = idx+1;
        }
    }
    /* Not found: lets allocates a new RID_INFO */
    if (addifmissing == PR_FALSE) {
        return NULL;
    }
    dblcictx->nb_rids++;
    if (dblcictx->nb_rids >= dblcictx->max_rids) {
        dblcictx->max_rids += 200;
        dblcictx->rids = (RID_INFO *)slapi_ch_realloc((void*)dblcictx->rids, dblcictx->max_rids * sizeof (RID_INFO));
    }
    for (int i = dblcictx->nb_rids-2; i >= idx_min; i--) {
        dblcictx->rids[i+1] = dblcictx->rids[i];
    }
    dblcictx->rids[idx_min].new = 1;
    dblcictx->rids[idx_min].rid = rid;
    return &dblcictx->rids[idx_min];
}

/* thread management functions */
static int
_cl5DispatchTrimThread(Replica *replica)
{
    PRThread *pth = NULL;

    pth = PR_CreateThread(PR_USER_THREAD, (VFP)(void *)_cl5TrimMain,
                          (void *)replica, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                          PR_UNJOINABLE_THREAD, DEFAULT_THREAD_STACKSIZE);
    if (NULL == pth) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "_cl5DispatchTrimThread - Failed to create trimming thread for %s"
                      "; NSPR error - %d\n", replica_get_name(replica),
                      PR_GetError());
        return CL5_SYSTEM_ERROR;
    }

    return CL5_SUCCESS;
}

/* data conversion functions */
static void
_cl5WriteString(const char *str, char **buff)
{
    if (str) {
        strcpy(*buff, str);
        (*buff) += strlen(str) + 1;
    } else /* just write NULL char */
    {
        (**buff) = '\0';
        (*buff)++;
    }
}

static void
_cl5ReadString(char **str, char **buff)
{
    if (str) {
        int len = strlen(*buff);

        if (len) {
            *str = slapi_ch_strdup(*buff);
            (*buff) += len + 1;
        } else /* just null char - skip it */
        {
            *str = NULL;
            (*buff)++;
        }
    } else /* just skip this string */
    {
        (*buff) += strlen(*buff) + 1;
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
static void
_cl5WriteMods(LDAPMod **mods, char **buff, void *clcrypt_handle)
{
    PRInt32 i;
    char *mod_start;
    PRInt32 count = 0;

    if (mods == NULL)
        return;

    /* skip mods count */
    mod_start = (*buff) + sizeof(count);

    /* write mods*/
    for (i = 0; mods[i]; i++) {
        if (0 <= _cl5WriteMod(mods[i], &mod_start, clcrypt_handle)) {
            count++;
        }
    }

    count = PR_htonl(count);
    memcpy(*buff, &count, sizeof(count));

    (*buff) = mod_start;
}

/*
 * return values:
 *     positive: no need to encrypt && succeeded to write a mod
 *            0: succeeded to encrypt && write a mod
 *     negative: failed to encrypt && no write to the changelog
 */
static int
_cl5WriteMod(LDAPMod *mod, char **buff, void *clcrypt_handle)
{
    char *orig_pos;
    char *pos;
    PRInt32 count;
    struct berval *bv;
    struct berval *encbv;
    struct berval *bv_to_use;
    Slapi_Mod smod;
    int rc = -1;

    if (NULL == mod) {
        return rc;
    }
    if (SLAPD_UNHASHED_PW_NOLOG == slapi_config_get_unhashed_pw_switch()) {
        if (0 == strcasecmp(mod->mod_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD)) {
            /* If nsslapd-unhashed-pw-switch == nolog, skip writing it to cl. */
            return rc;
        }
    }

    slapi_mod_init_byref(&smod, mod);

    orig_pos = pos = *buff;
    /* write mod op */
    *pos = (PRUint8)slapi_mod_get_operation(&smod);
    pos++;
    /* write attribute name    */
    _cl5WriteString(slapi_mod_get_type(&smod), &pos);

    /* write value count */
    count = PR_htonl(slapi_mod_get_num_values(&smod));
    memcpy(pos, &count, sizeof(count));
    pos += sizeof(PRInt32);

    /* if the mod has no values, eg delete attr or replace attr without values
     * do not reset buffer
     */
    rc = 0;

    bv = slapi_mod_get_first_value(&smod);
    while (bv) {
        encbv = NULL;
        rc = clcrypt_encrypt_value(clcrypt_handle,
                                   bv, &encbv);
        if (rc > 0) {
            /* no encryption needed. use the original bv */
            bv_to_use = bv;
        } else if ((0 == rc) && encbv) {
            /* successfully encrypted. use the encrypted bv */
            bv_to_use = encbv;
        } else { /* failed */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5WriteMod - Encrypting \"%s: %s\" failed\n",
                          slapi_mod_get_type(&smod), bv->bv_val);
            bv_to_use = NULL;
            rc = -1;
            break;
        }
        if (bv_to_use) {
            _cl5WriteBerval(bv_to_use, &pos);
        }
        slapi_ch_bvfree(&encbv);
        bv = slapi_mod_get_next_value(&smod);
    }

    if (rc < 0) {
        (*buff) = orig_pos;
    } else {
        (*buff) = pos;
    }

    slapi_mod_done(&smod);
    return rc;
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

static int
_cl5ReadMods(LDAPMod ***mods, char **buff, void *clcrypt_handle)
{
    char *pos = *buff;
    int i;
    int rc;
    PRInt32 mod_count;
    Slapi_Mods smods;
    Slapi_Mod smod;

    /* need to copy first, to skirt around alignment problems on certain
       architectures */
    memcpy((char *)&mod_count, *buff, sizeof(mod_count));
    mod_count = PR_ntohl(mod_count);
    pos += sizeof(mod_count);

    slapi_mods_init(&smods, mod_count);

    for (i = 0; i < mod_count; i++) {
        rc = _cl5ReadMod(&smod, &pos, clcrypt_handle);
        if (rc != CL5_SUCCESS) {
            slapi_mods_done(&smods);
            return rc;
        }

        slapi_mods_add_smod(&smods, &smod);
    }

    *buff = pos;

    *mods = slapi_mods_get_ldapmods_passout(&smods);
    slapi_mods_done(&smods);

    return CL5_SUCCESS;
}

static int
_cl5ReadMod(Slapi_Mod *smod, char **buff, void *clcrypt_handle)
{
    char *pos = *buff;
    PRInt32 val_count;
    char *type;
    int op;
    struct berval bv;
    struct berval *decbv;
    struct berval *bv_to_use;
    int rc = 0;

    op = (*pos) & 0x000000FF;
    pos++;
    _cl5ReadString(&type, &pos);

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&val_count, pos, sizeof(val_count));
    val_count = PR_ntohl(val_count);
    pos += sizeof(PRInt32);

    slapi_mod_init(smod, val_count);
    slapi_mod_set_operation(smod, op | LDAP_MOD_BVALUES);
    slapi_mod_set_type(smod, type);
    slapi_ch_free((void **)&type);

    for (size_t i = 0; i < val_count; i++) {
        _cl5ReadBerval(&bv, &pos);
        decbv = NULL;
        rc = 0;
        rc = clcrypt_decrypt_value(clcrypt_handle,
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
            int ii;
            for (ii = 0, ptr = encstr; (ii < bv.bv_len) && (ptr < encend - 6);
                 ii++, ptr += 3) {
                sprintf(ptr, "%x", 0xff & bv.bv_val[ii]);
            }
            if (ptr >= encend - 6) {
                sprintf(ptr, "...");
                ptr += 3;
            }
            *ptr = '\0';
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5ReadMod - Decrypting \"%s: %s\" failed\n",
                          slapi_mod_get_type(smod), encstr);
            bv_to_use = NULL;
        }
        if (bv_to_use) {
            slapi_mod_add_value(smod, bv_to_use);
        }
        slapi_ch_bvfree(&decbv);
        slapi_ch_free((void **)&bv.bv_val);
    }

    (*buff) = pos;

    return CL5_SUCCESS;
}

static int
_cl5GetModsSize(LDAPMod **mods)
{
    int size;
    int i;

    if (mods == NULL)
        return 0;

    size = sizeof(PRInt32);
    for (i = 0; mods[i]; i++) {
        size += _cl5GetModSize(mods[i]);
    }

    return size;
}

static int
_cl5GetModSize(LDAPMod *mod)
{
    int size;
    int i;

    size = 1 + strlen(mod->mod_type) + 1 + sizeof(mod->mod_op);
    i = 0;
    if (mod->mod_op & LDAP_MOD_BVALUES) /* values are in binary form */
    {
        while (mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL) {
            size += (PRInt32)mod->mod_bvalues[i]->bv_len + sizeof(PRInt32);
            i++;
        }
    } else /* string data */
    {
        PR_ASSERT(0); /* ggood string values should never be used in the server */
    }

    return size;
}

static void
_cl5ReadBerval(struct berval *bv, char **buff)
{
    PRUint32 length = 0;
    PRUint32 net_length = 0;

    PR_ASSERT(bv && buff);

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
        bv->bv_val = slapi_ch_malloc(bv->bv_len);
        memcpy(bv->bv_val, *buff, bv->bv_len);
        *buff += bv->bv_len;
    } else {
        bv->bv_val = NULL;
    }
}

static void
_cl5WriteBerval(struct berval *bv, char **buff)
{
    PRUint32 length = 0;
    PRUint32 net_length = 0;

    length = (PRUint32)bv->bv_len;
    net_length = PR_htonl(length);

    memcpy(*buff, &net_length, sizeof(net_length));
    *buff += sizeof(net_length);
    memcpy(*buff, bv->bv_val, length);
    *buff += length;
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
static int
_cl5ReadBervals(struct berval ***bv, char **buff, unsigned int size __attribute__((unused)))
{
    PRInt32 count;
    int i;
    char *pos;

    PR_ASSERT(bv && buff);

    /* ONREPL - need to check that we don't go beyond the end of the buffer */

    pos = *buff;
    memcpy((char *)&count, pos, sizeof(count));
    count = PR_htonl(count);
    pos += sizeof(count);

    /* allocate bervals */
    *bv = (struct berval **)slapi_ch_malloc((count + 1) * sizeof(struct berval *));
    if (*bv == NULL) {
        return CL5_MEMORY_ERROR;
    }

    for (i = 0; i < count; i++) {
        (*bv)[i] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
        if ((*bv)[i] == NULL) {
            ber_bvecfree(*bv);
            return CL5_MEMORY_ERROR;
        }

        _cl5ReadBerval((*bv)[i], &pos);
    }

    (*bv)[count] = NULL;
    *buff = pos;

    return CL5_SUCCESS;
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
static int
_cl5WriteBervals(struct berval **bv, char **buff, size_t *size)
{
    PRInt32 count, net_count;
    char *pos;
    int i;

    PR_ASSERT(bv && buff && size);

    /* compute number of values and size of the buffer to hold them */
    *size = sizeof(count);
    for (count = 0; bv[count]; count++) {
        *size += (size_t)(sizeof(PRInt32) + (PRInt32)bv[count]->bv_len);
    }

    /* allocate buffer */
    *buff = (char *)slapi_ch_malloc(*size);
    if (*buff == NULL) {
        *size = 0;
        return CL5_MEMORY_ERROR;
    }

    /* fill the buffer */
    pos = *buff;
    net_count = PR_htonl(count);
    memcpy(pos, &net_count, sizeof(net_count));
    pos += sizeof(net_count);
    for (i = 0; i < count; i++) {
        _cl5WriteBerval(bv[i], &pos);
    }

    return CL5_SUCCESS;
}

static int32_t
_cl5CheckCSNinCL(const ruv_enum_data *element, void *arg)
{
    cldb_Handle *cldb = (cldb_Handle *)arg;
    int rc = 0;

    dbi_val_t key = {0}, data = {0};
    char csnStr[CSN_STRSIZE];

    /* construct the key */
    csn_as_string(element->csn, PR_FALSE, csnStr);
    dblayer_value_set_buffer(cldb->be, &key, csnStr, CSN_STRSIZE);

    rc = dblayer_db_op(cldb->be, cldb->db, NULL, DBI_OP_GET, &key, &data);
    dblayer_value_free(cldb->be, &data);
    return rc;
}

static int32_t
_cl5CheckMaxRUV(cldb_Handle *cldb, RUV *maxruv)
{
    int rc = 0;

    rc = ruv_enumerate_elements(maxruv, _cl5CheckCSNinCL, (void *)cldb);

    return rc;
}

static void
_cl5DBClose(void)
{
    replica_enumerate_replicas(_cl5WriteReplicaRUV, NULL);
}

static int
_cl5TrimMain(void *param)
{
    struct timespec current_time = {0};
    struct timespec prev_time = {0};
    Replica *replica = (Replica *)param;
    cldb_Handle *cldb = replica_get_cl_info(replica);
    int32_t trimInterval;

    if (cldb == NULL) {
        /* This can happened in race condition
         * when the cldb_SetReplicaDB is called but the
         * dispatching of_cl5TrimMain thread is slow.
         * So trimming could be have been stopped (ruv reload) that
         * clears the cldb
         */
        return 0;
    }
    trimInterval = cldb->clConf.trimInterval;

    /* Get the initial current time for checking the trim interval */
    clock_gettime(CLOCK_MONOTONIC, &prev_time);

    /* Lock the CL state, and bump the thread count */
    pthread_mutex_lock(&(cldb->stLock));

    /* First check that no other trimming thread is running */
    if (cldb->trimmingOnGoing) {
        pthread_mutex_unlock(&(cldb->stLock));
        return 0;
    }

    /* Now trimming thread can start */
    cldb->trimmingOnGoing = 1;
    slapi_counter_increment(cldb->clThreads);

    while (cldb->dbState == CL5_STATE_OPEN)
    {
        pthread_mutex_unlock(&(cldb->stLock));

        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_sec - prev_time.tv_sec >= trimInterval) {
            /* time to trim */
            prev_time = current_time;
            _cl5TrimReplica(replica);
        }

        pthread_mutex_lock(&(cldb->clLock));
        /* While we have the CL lock get a fresh copy of the trim interval */
        trimInterval = cldb->clConf.trimInterval;
        current_time.tv_sec += trimInterval;
        pthread_cond_timedwait(&(cldb->clCvar), &(cldb->clLock), &current_time);
        pthread_mutex_unlock(&(cldb->clLock));

        pthread_mutex_lock(&(cldb->stLock));
    }
    slapi_counter_decrement(cldb->clThreads);
    cldb->trimmingOnGoing = 0;

    pthread_mutex_unlock(&(cldb->stLock));

    return 0;
}

/*
 * We remove an entry if it has been replayed to all consumers and the number
 * of entries in the changelog is larger than maxEntries or age of the entry
 * is larger than maxAge.  Also we can't purge entries which correspond to max
 * csns in the supplier's ruv. Here is a example where we can get into trouble:
 *
 *   The server is setup with time based trimming and no consumer's
 *   At some point all the entries are trimmed from the changelog.
 *   At a later point a consumer is added and initialized online.
 *   Then a change is made on the supplier.
 *   To update the consumer, the supplier would attempt to locate the last
 *   change sent to the consumer in the changelog and will fail because the
 *   change was removed.
 */
/*
 * We are purging a changelog after a cleanAllRUV task.  Find the specific
 * changelog for the backend that is being cleaned, and purge all the records
 * with the cleaned rid.
 */
static void
_cl5DoPurging(cleanruv_purge_data *purge_data)
{
    ReplicaId rid = purge_data->cleaned_rid;
    const Slapi_DN *suffix_sdn = purge_data->suffix_sdn;
    cldb_Handle *cldb = replica_get_cl_info(purge_data->replica);

    pthread_mutex_lock(&(cldb->clLock));
    _cl5PurgeRID (cldb, rid);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                  "_cl5DoPurging - Purged rid (%d) from suffix (%s)\n",
                  rid, slapi_sdn_get_dn(suffix_sdn));
    pthread_mutex_unlock(&(cldb->clLock));
    return;
}

static inline int
_cl5CIEventCheckTxnEnd(DBLCI_EVENT_COUNT *ev)
{
    return (ev->nbmax && ev->nb >= ev->nbmax);
}

/*
 * _cl5Iterate callbacks helper
 * Changelog cursor iterator callback common code initializer.
 *  Shoukd start all the _cl5CursorIterate callbacks.
 */
static int
_cl5CICbInit(dbi_val_t *key, dbi_val_t *data, DBLCI_CTX *dblcictx)
{
    /* Verify that we have a valid csn */
    if (key->size != CSN_STRSIZE) {
        return DBI_RC_SUCCESS;
    }
    /* Update last csn */
    csn_init_by_string(&dblcictx->csn, data->data);
    if (_cl5CIEventCheckTxnEnd(&dblcictx->seen) ||
        _cl5CIEventCheckTxnEnd(&dblcictx->changed)) {
        /*
         * returns DBI_RC_NOTFOUND so dblayer_cursor_iterate
         * stops with DBI_RC_SUCCESS return code, then
         * _cl5Iterate commits the txn and restart a new txn
         * and iterate again starting from last seen record
         * (i.e same key and data that the one we are
         * currently processing )
         */
        return DBI_RC_NOTFOUND;
    }
    dblcictx->seen.nb++;
    return DBI_RC_SUCCESS;
}

/*
 * _cl5Iterate callbacks helper
 * Remove current entry from changelog.
 */
int
_cl5CICbRemoveEntry(DBLCI_CTX *dblcictx, const char *funcname)
{
    int rc = dblayer_cursor_op(&dblcictx->cursor,DBI_OP_DEL, NULL, NULL);
    if (rc == DBI_RC_SUCCESS) {
        /* decrement entry count */
        PR_AtomicDecrement(&dblcictx->cldb->entryCount);
        dblcictx->changed.nb++;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "%s - Failed to remove entry, err=%d %s\n",
                      funcname, rc, dblayer_strerror(rc));
    }
    return rc;
}

/*
 * _cl5PurgeRid helper: dblayer_cursor_iterate callback.
 * Returns:
 *     DBI_RC_SUCCESS to iterate on next entry
 *     DBI_RC_NOTFOUND to stop iteration with DBI_RC_SUCCESS code
 *     other DBI_RC_ code to stop iteration with that error code.
 */
int
_cl5PurgeRidOnEntry(dbi_val_t *key, dbi_val_t *data, void *ctx)
{
    DBLCI_CTX *dblcictx = ctx;
    ReplicaId rid = 0;
    int rc = 0;

    rc = _cl5CICbInit(key, data, dblcictx);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }

    rid = csn_get_replicaid(&dblcictx->csn);
    if (rid == dblcictx->rid2purge) {
        rc = _cl5CICbRemoveEntry(dblcictx, __FUNCTION__);
    }
    return rc;
}

/*
 *  _cl5PurgeRID(Object *obj,  ReplicaId cleaned_rid)
 *
 *  Clean the entire changelog of updates from the "cleaned rid" via CLEANALLRUV
 *  Delete entries in batches so we don't consume too many db locks, and we don't
 *  lockup the changelog during the entire purging process using one transaction.
 *  We save the key from the last iteration so we don't have to start from the
 *  beginning for each new iteration.
 */
void
_cl5PurgeRID(cldb_Handle *cldb, ReplicaId cleaned_rid)
{
    DBLCI_CTX dblcictx = {0};

    dblcictx.seen.nbmax = CL5_PURGE_MAX_LOOKUP_PER_TRANSACTION;
    dblcictx.changed.nbmax = CL5_PURGE_MAX_PER_TRANSACTION;
    dblcictx.rid2purge = cleaned_rid;
    _cl5Iterate(cldb, _cl5PurgeRidOnEntry, &dblcictx, PR_FALSE);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                  "_cl5PurgeRID - Removed (%ld entries) that originated from rid (%d)\n",
                  dblcictx.changed.tot, cleaned_rid);
}

/*
 * _cl5TrimReplica helper: dblayer_cursor_iterate callback.
 * Returns:
 *     DBI_RC_SUCCESS to iterate on next entry
 *     DBI_RC_NOTFOUND to stop iteration with DBI_RC_SUCCESS code
 *     other DBI_RC_ code to stop iteration with that error code.
 */
int
_cl5TrimEntry(dbi_val_t *key, dbi_val_t *data, void *ctx)
{
    DBLCI_CTX *dblcictx = ctx;
    Replica *r = dblcictx->r;
    time_t entrytime = 0;
    ReplicaId rid = 0;
    int rc = 0;

    rc = _cl5CICbInit(key, data, dblcictx);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }

    if (cl5HelperEntry(NULL, &dblcictx->csn) == PR_TRUE) {
        return DBI_RC_SUCCESS;
    }
    /* Get the operation time without decoding the whole operation */
    rc = cl5DBData2EntryTime(data->data, &entrytime);
    if (rc != CL5_SUCCESS) {
        return DBI_RC_OTHER;
    }
    if (dblcictx->numToTrim <= 0 &&
        _cl5CanTrim(entrytime, &dblcictx->numToTrim, r, &dblcictx->cldb->clConf) == PR_FALSE) {
        /* trimming is complete */
        dblcictx->finished = PR_TRUE;
        return DBI_RC_NOTFOUND;
    }
    if (ruv_covers_csn_strict(dblcictx->ruv, &dblcictx->csn)) {
        /* Lets remove the entry */
        rc = _cl5CICbRemoveEntry(dblcictx, __FUNCTION__);
        if (rc != DBI_RC_SUCCESS) {
            return rc;
        }
        if (dblcictx->numToTrim > 0)
            (dblcictx->numToTrim)--;
        /* We should not update the changelog purge ruv with a deleted csn
         * so lets us mark the rid to update the purge ruv on a second phase
         */
        rid = csn_get_replicaid(&dblcictx->csn);
        _cl5GetRidInfo(dblcictx, rid, PR_TRUE);
    } else { /* ruv_covers_csn_strict */
        /* The changelog DB is time ordered. If we can not trim
         * a CSN, we will not be allowed to trim the rest of the
         * CSNs generally. However, the maxcsn of each replica ID
         * is always kept in the changelog as an anchor for
         * replaying future changes. We have to skip those anchor
         * CSNs, otherwise a non-active replica ID could block
         * the trim forever.
         */
        CSN *maxcsn = NULL;
        ruv_get_largest_csn_for_replica(dblcictx->ruv, rid, &maxcsn);
        rc = csn_compare(&dblcictx->csn, maxcsn);
        if (maxcsn)
            csn_free(&maxcsn);
        if (rc) {
            /* csn is not anchor CSN */
            dblcictx->finished = PR_TRUE;
            return DBI_RC_NOTFOUND;
        } else {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "_cl5TrimReplica - Changelog purge skipped anchor csn %s\n",
                          (char*)key->data);
            return DBI_RC_SUCCESS;
        }
    }
    return DBI_RC_SUCCESS;
}

/*
 * _cl5TrimReplica helper: dblayer_cursor_iterate callback.
 * Update the purge ruv
 * Returns:
 *     DBI_RC_SUCCESS to iterate on next entry
 *     DBI_RC_NOTFOUND to stop iteration with DBI_RC_SUCCESS code
 *     other DBI_RC_ code to stop iteration with that error code.
 */
int
_cl5TrimUpdateRuv(dbi_val_t *key, dbi_val_t *data, void *ctx)
{
    DBLCI_CTX *dblcictx = ctx;
    RID_INFO *ridinfo = NULL;
    ReplicaId rid = 0;
    int rc = 0;

    rc = _cl5CICbInit(key, data, dblcictx);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }
    rid = csn_get_replicaid(&dblcictx->csn);
    ridinfo = _cl5GetRidInfo(dblcictx, rid, PR_FALSE);
    if (ridinfo) {
        rc = _cl5UpdateRUV(dblcictx->cldb, &dblcictx->csn, PR_FALSE, PR_TRUE);
        if (rc != CL5_SUCCESS) {
            return rc;
        }

        /* Lets remove the rid from the list */
        dblcictx->nb_rids--;
        if (dblcictx->nb_rids == 0) {
            dblcictx->finished = PR_TRUE;
            return DBI_RC_NOTFOUND;
        }
        for (size_t i = ridinfo-dblcictx->rids; i <= dblcictx->nb_rids; i++) {
            dblcictx->rids[i] = dblcictx->rids[i+1];
        }
     }
     return DBI_RC_SUCCESS;
}

void
_cl5TrimReplica(Replica *r)
{
    DBLCI_CTX dblcictx = {0};
    int rc = CL5_SUCCESS;

    cldb_Handle *cldb = replica_get_cl_info(r);
    if (!_cl5CanTrim ((time_t)0, &dblcictx.numToTrim, r, &cldb->clConf) ) {
        return;
    }

    /* construct the ruv up to which we can trim */
    rc = _cl5GetRUV2Purge2(r, &dblcictx.ruv);
    if (rc != CL5_SUCCESS || dblcictx.ruv == NULL) {
        return;
    }
    dblcictx.r = r;
    dblcictx.seen.nbmax = CL5_TRIM_MAX_LOOKUP_PER_TRANSACTION;
    dblcictx.changed.nbmax = CL5_TRIM_MAX_PER_TRANSACTION;
    rc = _cl5Iterate(cldb, _cl5TrimEntry, &dblcictx, PR_FALSE);
    ruv_destroy(&dblcictx.ruv);
    rc = _cl5Iterate(cldb, _cl5TrimUpdateRuv, &dblcictx, PR_TRUE);
    slapi_ch_free((void**)&dblcictx.rids);

    if (dblcictx.changed.tot) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5TrimReplica - Trimmed %ld changes from the changelog\n",
                      dblcictx.changed.tot);
    }
}

static PRBool
_cl5CanTrim(time_t time, long *numToTrim, Replica *replica, CL5Config *dbTrim)
{
    *numToTrim = 0;

    if (dbTrim->maxAge == 0 && dbTrim->maxEntries == 0) {
        return PR_FALSE;
    }
    if (dbTrim->maxAge == 0) {
        *numToTrim = cl5GetOperationCount(replica) - dbTrim->maxEntries;
        return (*numToTrim > 0);
    }

    if (dbTrim->maxEntries > 0 &&
        (*numToTrim = cl5GetOperationCount(replica) - dbTrim->maxEntries) > 0) {
        return PR_TRUE;
    }

    if (time) {
        return (slapi_current_utc_time() - time > dbTrim->maxAge);
    } else {
        return PR_TRUE;
    }
}

static int
_cl5ReadRUV (cldb_Handle *cldb, PRBool purge)
{
    int rc;
    char csnStr[CSN_STRSIZE];
    dbi_val_t key = {0}, data = {0};
    struct berval **vals = NULL;
    char *pos;
    char *agmt_name;

    agmt_name = get_thread_private_agmtname();

    if (purge) { /* read purge vector entry */
        _cl5GetHelperEntryKey(PURGE_RUV_TIME, csnStr);
    } else { /* read upper bound vector */
        _cl5GetHelperEntryKey(MAX_RUV_TIME, csnStr);
    }

    dblayer_value_set_buffer(cldb->be, &key, csnStr, CSN_STRSIZE);
    dblayer_value_init(cldb->be, &data);

    rc = dblayer_db_op(cldb->be, cldb->db, NULL /*txn*/, DBI_OP_GET, &key, &data);
    switch (rc) {
    case 0:
        pos = data.data;
        rc = _cl5ReadBervals(&vals, &pos, data.size);
        dblayer_value_free(cldb->be, &data);
        if (rc != CL5_SUCCESS)
            goto done;

        if (purge) {
            rc = ruv_init_from_bervals(vals, &cldb->purgeRUV);
        } else {
            rc = ruv_init_from_bervals(vals, &cldb->maxRUV);
        }
        if (rc != RUV_SUCCESS) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "_cl5ReadRUV - %s - Failed to initialize %s ruv; "
                          "RUV error %d\n",
                          agmt_name, purge ? "purge" : "upper bound", rc);

            rc = CL5_RUV_ERROR;
            goto done;
        }

        /* delete the entry; it is re-added when file
                               is successfully closed */
        dblayer_db_op(cldb->be, cldb->db, NULL, DBI_OP_DEL, &key, NULL);

        rc = CL5_SUCCESS;
        goto done;

    case DBI_RC_NOTFOUND: /* RUV is lost - need to construct */
        rc = CL5_NOTFOUND;
        goto done;

    default:
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5ReadRUV - %s - Failed to get purge RUV; "
                      "db error - %d %s\n",
                      agmt_name, rc, dblayer_strerror(rc));
        rc = CL5_DB_ERROR;
        goto done;
    }

done:
    ber_bvecfree(vals);
    return rc;
}

/* Read changelog RUUV and purge RUV. Rebuild them if needed */
int
_cl5ReadRUVs (cldb_Handle *cldb)
{
    int rc = _cl5ReadRUV(cldb, PR_TRUE);
    if (rc == CL5_SUCCESS) {
        rc = _cl5ReadRUV(cldb, PR_FALSE);
    }
    if (rc == CL5_NOTFOUND) {
        rc = _cl5ConstructRUVs(cldb);
    }
    if (rc == CL5_SUCCESS) {
        rc = _cl5GetEntryCount(cldb);
    }
    return rc;
}


static int
_cl5WriteRUV (cldb_Handle *cldb, PRBool purge)
{
    int rc;
    dbi_val_t key = {0}, data = {0};
    char csnStr[CSN_STRSIZE];
    struct berval **vals;
    dbi_txn_t *txnid = NULL;
    char *buff;
    size_t size;

    if ((purge && cldb->purgeRUV == NULL) || (!purge && cldb->maxRUV == NULL))
        return CL5_SUCCESS;

    if (purge) {
        /* Set the minimum CSN of each vector to a dummy CSN that contains
         * just a replica ID, e.g. 00000000000000010000.
         * The minimum CSN in a purge RUV is not used so the value doesn't
         * matter, but it needs to be set to something so that it can be
         * flushed to changelog at shutdown and parsed at startup with the
         * regular string-to-RUV parsing routines. */
        ruv_insert_dummy_min_csn(cldb->purgeRUV);
        key.data = _cl5GetHelperEntryKey(PURGE_RUV_TIME, csnStr);
        ruv_to_bervals(cldb->purgeRUV, &vals);
    } else {
        key.data = _cl5GetHelperEntryKey(MAX_RUV_TIME, csnStr);
        rc = ruv_to_bervals(cldb->maxRUV, &vals);
    }

    if (!purge && _cl5CheckMaxRUV(cldb, cldb->maxRUV)) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5WriteRUV - changelog maxRUV not found in changelog for file %s\n",
                      cldb->ident);
        ber_bvecfree(vals);
        return CL5_DB_ERROR;
    }

    key.size = CSN_STRSIZE;

    rc = _cl5WriteBervals(vals, &buff, &size);
    dblayer_value_set(cldb->be, &data, buff, size);
    ber_bvecfree(vals);
    if (rc != CL5_SUCCESS) {
        return rc;
    }

    rc = dblayer_db_op(cldb->be, cldb->db, txnid, DBI_OP_PUT, &key, &data);

    dblayer_value_free(cldb->be, &data);
    if (rc == 0) {
        return CL5_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5WriteRUV - Failed to write %s RUV for file %s; db error - %d (%s)\n",
                      purge ? "purge" : "upper bound", cldb->ident, rc, dblayer_strerror(rc));

        return CL5_DB_ERROR;
    }
}

/* _cl5ConstructRUVs helper: Action callback to build DBLCI_CTX */
int
_cl5GenRUVInfo(dbi_val_t *key, dbi_val_t *data, void *ctx)
{
    DBLCI_CTX *dblcictx = ctx;
    ReplicaId rid = 0;
    RID_INFO *ridinfo = NULL;
    CSN csn = {0};
    int rc = _cl5CICbInit(key, data, dblcictx);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }
    rid = csn_get_replicaid(&dblcictx->csn);
    if (cl5HelperEntry(NULL, &dblcictx->csn) == PR_TRUE) {
        return DBI_RC_SUCCESS;
    }
    if (is_cleaned_rid(rid)) {
        /* skip this entry as the rid is invalid */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GenRUVInfo - Skipping entry because its csn contains a cleaned rid(%d)\n",
                      rid);
        return DBI_RC_SUCCESS;
    }
    /* Lets update min and max csn for the rid.
     * As the db ensure that the csn are sorted, the first csn after ridinfo get created is the min
     * and csn is the current max
     */
    ridinfo = _cl5GetRidInfo(dblcictx, rid, PR_TRUE);
    if (ridinfo->new == 1) {
        ridinfo->new = 0;
        ridinfo->mincsn = csn;
    }
    ridinfo->maxcsn = csn;
    return DBI_RC_SUCCESS;
}

/* Walk every changelog records to find min and max csns for each rids then update changelog RUVs */
int
_cl5ConstructRUVs (cldb_Handle *cldb)
{
    /* construct the RUV */
    DBLCI_CTX dblcictx = {0};
    char mincsnstr[CSN_STRSIZE] = "";
    char maxcsnstr[CSN_STRSIZE] = "";
    int rc = ruv_init_new(cldb->ident, 0, NULL, &cldb->purgeRUV);

    if (rc != RUV_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                           "Failed to initialize purges RUV for file %s; ruv error - %d\n",
                      cldb->ident, rc);
        return CL5_RUV_ERROR;
    }
    rc = ruv_init_new(cldb->ident, 0, NULL, &cldb->maxRUV);
    if (rc != RUV_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                           "Failed to initialize upper bound RUV for file %s; ruv error - %d\n",
                      cldb->ident, rc);
        return CL5_RUV_ERROR;
    }

    slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                  "_cl5ConstructRUVs - Rebuilding the replication changelog RUV, "
                  "this may take several minutes...\n");

    /*
     * No specific dblcictx initialization has the changelog is starting, we may afford to
     * have a single txn
     */
    rc = _cl5Iterate(cldb, _cl5GenRUVInfo, &dblcictx, PR_TRUE);
    if (rc == CL5_NOTFOUND) {
        /* Now that we have the min and max csn for each rids, it is time to update the RUVs */
        rc = CL5_SUCCESS;
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                           "Found %d replicas in %s changelog file.\n",
                      dblcictx.nb_rids, cldb->ident);
        for (size_t i=0; i<dblcictx.nb_rids; i++) {
            rc = ruv_set_csns(cldb->maxRUV, &dblcictx.rids[i].maxcsn, NULL);
            if (rc != RUV_SUCCESS) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                                   "Failed to update upper bound RUV for file %s; ruv error - %d\n",
                              cldb->ident, rc);
                rc = CL5_DB_ERROR;
                break;
            }
            rc = ruv_set_csns(cldb->purgeRUV, &dblcictx.rids[i].mincsn, NULL);
            if (rc != RUV_SUCCESS) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                                   "Failed to update purge RUV for file %s; ruv error - %d\n",
                              cldb->ident, rc);
                rc = CL5_DB_ERROR;
                break;
            }
            csn_as_string(&dblcictx.rids[i].maxcsn, PR_FALSE, maxcsnstr);
            csn_as_string(&dblcictx.rids[i].mincsn, PR_FALSE, mincsnstr);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5ConstructRUVs - "
                                                               "Replica id: %d mincsn: %s maxcsn: %s\n",
                          dblcictx.rids[i].rid, mincsnstr, maxcsnstr);
        }
    }
    slapi_ch_free((void**)&dblcictx.rids);
    if (rc != RUV_SUCCESS) {
        ruv_destroy(&cldb->purgeRUV);
        ruv_destroy(&cldb->maxRUV);
        rc = CL5_DB_ERROR;
    }

    slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                  "_cl5ConstructRUVs - Rebuilding replication changelog RUV complete.  Result %d (%s)\n",
                  rc, rc ? "Failed to rebuild changelog RUV" : "Success");

    return rc;
}

static int
_cl5UpdateRUV (cldb_Handle *cldb, CSN *csn, PRBool newReplica, PRBool purge)
{
    ReplicaId rid;
    int rc = RUV_SUCCESS; /* initialize rc to avoid erroneous logs */

    PR_ASSERT(csn);

    /*
     *  if purge is TRUE, cldb->purgeRUV must be set;
     *  if purge is FALSE, maxRUV must be set
     */
    PR_ASSERT(cldb && ((purge && cldb->purgeRUV) || (!purge && cldb->maxRUV)));
    rid = csn_get_replicaid(csn);

    /* update vector only if this replica is not yet part of RUV */
    if (purge && newReplica) {
        if (ruv_contains_replica(cldb->purgeRUV, rid)) {
            return CL5_SUCCESS;
        } else {
            /* if the replica is not part of the purgeRUV yet, add it unless it's from a cleaned rid */
            ruv_add_replica(cldb->purgeRUV, rid, multisupplier_get_local_purl());
        }
    } else {
        if (purge) {
            rc = ruv_set_csns(cldb->purgeRUV, csn, NULL);
        } else {
            rc = ruv_set_csns(cldb->maxRUV, csn, NULL);
        }
    }

    if (rc != RUV_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5UpdatePurgeRUV - "
                                                           "Failed to update %s RUV for file %s; ruv error - %d\n",
                      purge ? "purge" : "upper bound", cldb->ident, rc);
        return CL5_RUV_ERROR;
    }

    return CL5_SUCCESS;
}

static int
_cl5EnumConsumerRUV(const ruv_enum_data *element, void *arg)
{
    int rc;
    RUV *ruv;
    CSN *csn = NULL;

    PR_ASSERT(element && element->csn && arg);

    ruv = (RUV *)arg;

    rc = ruv_get_largest_csn_for_replica(ruv, csn_get_replicaid(element->csn), &csn);
    if (rc != RUV_SUCCESS || csn == NULL || csn_compare(element->csn, csn) < 0) {
        ruv_set_max_csn(ruv, element->csn, NULL);
    }

    if (csn)
        csn_free(&csn);

    return 0;
}

static int
_cl5GetRUV2Purge2(Replica *replica, RUV **ruv)
{
    int rc = CL5_SUCCESS;
    Object *agmtObj = NULL;
    Repl_Agmt *agmt;
    Object *consRUVObj, *supRUVObj;
    RUV *consRUV, *supRUV;
    CSN *csn;

    if (!ruv) {
        rc = CL5_UNKNOWN_ERROR;
        goto done;
    }

    /* We start with this replica's RUV. */
    supRUVObj = replica_get_ruv(replica);
    PR_ASSERT(supRUVObj);

    supRUV = (RUV *)object_get_data(supRUVObj);
    PR_ASSERT(supRUV);

    *ruv = ruv_dup(supRUV);

    object_release(supRUVObj);

    agmtObj = agmtlist_get_first_agreement_for_replica(replica);
    while (agmtObj) {
        agmt = (Repl_Agmt *)object_get_data(agmtObj);
        PR_ASSERT(agmt);
        /* we need to handle all agreements, also if they are not enabled
         * if they will be later enabled and changes are trimmed
         * replication can fail
         */
        consRUVObj = agmt_get_consumer_ruv(agmt);
        if (consRUVObj) {
            consRUV = (RUV *)object_get_data(consRUVObj);
            rc = ruv_enumerate_elements(consRUV, _cl5EnumConsumerRUV, *ruv);
            if (rc != RUV_SUCCESS) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5GetRUV2Purge2 - "
                                                                   "Failed to construct ruv; ruv error - %d\n",
                              rc);
                rc = CL5_RUV_ERROR;
                object_release(consRUVObj);
                object_release(agmtObj);
                break;
            }

            object_release(consRUVObj);
        }

        agmtObj = agmtlist_get_next_agreement_for_replica(replica, agmtObj);
    }

    /* check if there is any data in the constructed ruv - otherwise get rid of it */
    if (ruv_get_max_csn(*ruv, &csn) != RUV_SUCCESS || csn == NULL) {
        ruv_destroy(ruv);
    } else {
        csn_free(&csn);
    }
done:
    if (rc != CL5_SUCCESS && ruv)
        ruv_destroy(ruv);

    return rc;
}

int
cl5NotifyRUVChange(Replica *replica)
{
    int rc = 0;
    cldb_Handle *cldb = replica_get_cl_info(replica);
    Object *ruv_obj = replica_get_ruv(replica);

    /* In error condition, cldb may be NULL (typically after if on line
     * initialization failed. (In which case the backend mapping tree is not
     * mounted
     */
    if (!cldb) {
        return -1;
    }

    pthread_mutex_lock(&(cldb->clLock));

    slapi_ch_free_string(&cldb->ident);
    ruv_destroy(&cldb->maxRUV);
    ruv_destroy(&cldb->purgeRUV);

    cldb->ident = ruv_get_replica_generation ((RUV*)object_get_data (ruv_obj));
    rc = _cl5ReadRUVs(cldb);

    pthread_mutex_unlock(&(cldb->clLock));
    object_release(ruv_obj);
    return rc;
}

static int
_cl5GetEntryCount(cldb_Handle *cldb)
{
    int rc;
    char csnStr[CSN_STRSIZE];
    dbi_val_t key = {0}, data = {0};

    /* read entry count. if the entry is there - the file was successfully closed
       last time it was used */
    _cl5GetHelperEntryKey(ENTRY_COUNT_TIME, csnStr);
    dblayer_value_set_buffer(cldb->be, &key, csnStr, CSN_STRSIZE);
    dblayer_value_init(cldb->be, &data);

    rc = dblayer_db_op(cldb->be, cldb->db, NULL, DBI_OP_GET, &key, &data);
    switch (rc) {
    case 0:
        cldb->entryCount = *(int *)data.data;
        dblayer_value_free(cldb->be, &data);

        /* delete the entry. the entry is re-added when file
                               is successfully closed */
        dblayer_db_op(cldb->be, cldb->db, NULL, DBI_OP_DEL, &key, NULL);
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "_cl5GetEntryCount - %d changes for replica %s\n",
                      cldb->entryCount, cldb->ident);
        return CL5_SUCCESS;

    case DBI_RC_NOTFOUND:
        cldb->entryCount = 0;

        rc = dblayer_get_entries_count(cldb->be, cldb->db, NULL, &cldb->entryCount);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5GetEntryCount - Failed to get changelog statistics");
            return CL5_DB_ERROR;
        }

        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "_cl5GetEntryCount - %d changes for replica %s\n",
                      cldb->entryCount, cldb->ident);

        return CL5_SUCCESS;

    default:
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5GetEntryCount - Failed to get count entry; "
                      "db error - %d %s\n",
                      rc, dblayer_strerror(rc));
        return CL5_DB_ERROR;
    }
}

static int
_cl5WriteEntryCount(cldb_Handle *cldb)
{
    int rc;
    dbi_val_t key = {0}, data = {0};
    char csnStr[CSN_STRSIZE];
    dbi_txn_t *txnid = NULL;

    key.data = _cl5GetHelperEntryKey(ENTRY_COUNT_TIME, csnStr);
    key.size = CSN_STRSIZE;
    data.data = (void *)&cldb->entryCount;
    data.size = sizeof(cldb->entryCount);

    rc = dblayer_db_op(cldb->be, cldb->db, txnid, DBI_OP_PUT, &key, &data);
    if (rc == 0) {
        return CL5_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5WriteEntryCount - "
                      "Failed to write count entry for file %s; db error - %d %s\n",
                      cldb->ident, rc, dblayer_strerror(rc));
        return CL5_DB_ERROR;
    }
}

static const char *
_cl5OperationType2Str(int type)
{
    switch (type) {
    case SLAPI_OPERATION_ADD:
        return T_ADDCTSTR;
    case SLAPI_OPERATION_MODIFY:
        return T_MODIFYCTSTR;
    case SLAPI_OPERATION_MODRDN:
        return T_MODRDNCTSTR;
    case SLAPI_OPERATION_DELETE:
        return T_DELETECTSTR;
    default:
        return NULL;
    }
}

static int
_cl5Str2OperationType(const char *str)
{
    if (strcasecmp(str, T_ADDCTSTR) == 0)
        return SLAPI_OPERATION_ADD;

    if (strcasecmp(str, T_MODIFYCTSTR) == 0)
        return SLAPI_OPERATION_MODIFY;

    if (strcasecmp(str, T_MODRDNCTSTR) == 0)
        return SLAPI_OPERATION_MODRDN;

    if (strcasecmp(str, T_DELETECTSTR) == 0)
        return SLAPI_OPERATION_DELETE;

    return -1;
}

static int
_cl5Operation2LDIF(const slapi_operation_parameters *op, const char *replGen, char **ldifEntry, PRInt32 *lenLDIF)
{
    int len = 2;
    lenstr *l = NULL;
    const char *strType;
    const char *strDeleteOldRDN = "false";
    char *buff, *start;
    LDAPMod **add_mods;
    char *rawDN = NULL;
    char strCSN[CSN_STRSIZE];

    PR_ASSERT(op && replGen && ldifEntry && IsValidOperation(op));

    strType = _cl5OperationType2Str(op->operation_type);
    csn_as_string(op->csn, PR_FALSE, strCSN);

    /* find length of the buffer */
    len += LDIF_SIZE_NEEDED(strlen(T_CHANGETYPESTR), strlen(strType));
    len += LDIF_SIZE_NEEDED(strlen(T_REPLGEN), strlen(replGen));
    len += LDIF_SIZE_NEEDED(strlen(T_CSNSTR), strlen(strCSN));
    len += LDIF_SIZE_NEEDED(strlen(T_UNIQUEIDSTR), strlen(op->target_address.uniqueid));

    switch (op->operation_type) {
    case SLAPI_OPERATION_ADD:
        if (NULL == op->p.p_add.target_entry) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5Operation2LDIF - ADD - entry is NULL\n");
            return CL5_BAD_FORMAT;
        }
        if (op->p.p_add.parentuniqueid)
            len += LDIF_SIZE_NEEDED(strlen(T_PARENTIDSTR), strlen(op->p.p_add.parentuniqueid));
        slapi_entry2mods(op->p.p_add.target_entry, &rawDN, &add_mods);
        len += LDIF_SIZE_NEEDED(strlen(T_DNSTR), strlen(rawDN));
        l = make_changes_string(add_mods, NULL);
        len += LDIF_SIZE_NEEDED(strlen(T_CHANGESTR), l->ls_len);
        ldap_mods_free(add_mods, 1);
        break;

    case SLAPI_OPERATION_MODIFY:
        if (NULL == op->p.p_modify.modify_mods) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5Operation2LDIF - MODIFY - mods are NULL\n");
            return CL5_BAD_FORMAT;
        }
        len += LDIF_SIZE_NEEDED(strlen(T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
        l = make_changes_string(op->p.p_modify.modify_mods, NULL);
        len += LDIF_SIZE_NEEDED(strlen(T_CHANGESTR), l->ls_len);
        break;

    case SLAPI_OPERATION_MODRDN:
        if (NULL == op->p.p_modrdn.modrdn_mods) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5Operation2LDIF - MODRDN - mods are NULL\n");
            return CL5_BAD_FORMAT;
        }
        len += LDIF_SIZE_NEEDED(strlen(T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
        len += LDIF_SIZE_NEEDED(strlen(T_NEWRDNSTR), strlen(op->p.p_modrdn.modrdn_newrdn));
        strDeleteOldRDN = (op->p.p_modrdn.modrdn_deloldrdn ? "true" : "false");
        len += LDIF_SIZE_NEEDED(strlen(T_DRDNFLAGSTR),
                                strlen(strDeleteOldRDN));
        if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
            len += LDIF_SIZE_NEEDED(strlen(T_NEWSUPERIORDNSTR),
                                    REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address));
        if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)
            len += LDIF_SIZE_NEEDED(strlen(T_NEWSUPERIORIDSTR),
                                    strlen(op->p.p_modrdn.modrdn_newsuperior_address.uniqueid));
        l = make_changes_string(op->p.p_modrdn.modrdn_mods, NULL);
        len += LDIF_SIZE_NEEDED(strlen(T_CHANGESTR), l->ls_len);
        break;

    case SLAPI_OPERATION_DELETE:
        if (NULL == REPL_GET_DN(&op->target_address)) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5Operation2LDIF - DELETE - target dn is NULL\n");
            return CL5_BAD_FORMAT;
        }
        len += LDIF_SIZE_NEEDED(strlen(T_DNSTR), REPL_GET_DN_LEN(&op->target_address));
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5Operation2LDIF - Invalid operation type - %lu\n", op->operation_type);
        return CL5_BAD_FORMAT;
    }

    /* allocate buffer */
    buff = slapi_ch_malloc(len);
    start = buff;
    if (buff == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5Operation2LDIF: memory allocation failed\n");
        return CL5_MEMORY_ERROR;
    }

    /* fill buffer */
    slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGETYPESTR, (char *)strType, strlen(strType), 0);
    slapi_ldif_put_type_and_value_with_options(&buff, T_REPLGEN, (char *)replGen, strlen(replGen), 0);
    slapi_ldif_put_type_and_value_with_options(&buff, T_CSNSTR, (char *)strCSN, strlen(strCSN), 0);
    slapi_ldif_put_type_and_value_with_options(&buff, T_UNIQUEIDSTR, op->target_address.uniqueid,
                                               strlen(op->target_address.uniqueid), 0);

    switch (op->operation_type) {
    case SLAPI_OPERATION_ADD:
        if (op->p.p_add.parentuniqueid)
            slapi_ldif_put_type_and_value_with_options(&buff, T_PARENTIDSTR,
                                                       op->p.p_add.parentuniqueid, strlen(op->p.p_add.parentuniqueid), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, rawDN, strlen(rawDN), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
        slapi_ch_free((void **)&rawDN);
        break;

    case SLAPI_OPERATION_MODIFY:
        slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address),
                                                   REPL_GET_DN_LEN(&op->target_address), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
        break;

    case SLAPI_OPERATION_MODRDN:
        slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address),
                                                   REPL_GET_DN_LEN(&op->target_address), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_NEWRDNSTR, op->p.p_modrdn.modrdn_newrdn,
                                                   strlen(op->p.p_modrdn.modrdn_newrdn), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_DRDNFLAGSTR, strDeleteOldRDN,
                                                   strlen(strDeleteOldRDN), 0);
        if (REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address))
            slapi_ldif_put_type_and_value_with_options(&buff, T_NEWSUPERIORDNSTR,
                                                       REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address),
                                                       REPL_GET_DN_LEN(&op->p.p_modrdn.modrdn_newsuperior_address), 0);
        if (op->p.p_modrdn.modrdn_newsuperior_address.uniqueid)
            slapi_ldif_put_type_and_value_with_options(&buff, T_NEWSUPERIORIDSTR,
                                                       op->p.p_modrdn.modrdn_newsuperior_address.uniqueid,
                                                       strlen(op->p.p_modrdn.modrdn_newsuperior_address.uniqueid), 0);
        slapi_ldif_put_type_and_value_with_options(&buff, T_CHANGESTR, l->ls_buf, l->ls_len, 0);
        break;

    case SLAPI_OPERATION_DELETE:
        slapi_ldif_put_type_and_value_with_options(&buff, T_DNSTR, REPL_GET_DN(&op->target_address),
                                                   REPL_GET_DN_LEN(&op->target_address), 0);
        break;
    }

    *buff = '\n';
    buff++;
    *buff = '\0';

    *ldifEntry = start;
    *lenLDIF = buff - start;

    if (l)
        lenstr_free(&l);

    return CL5_SUCCESS;
}

static int
_cl5LDIF2Operation(char *ldifEntry, slapi_operation_parameters *op, char **replGen)
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

    PR_ASSERT(op && ldifEntry && replGen);

    memset(op, 0, sizeof(*op));

    next = ldifEntryWork;
    while ((line = ldif_getline(&next)) != NULL) {
        if (*line == '\n' || *line == '\0') {
            break;
        }

        /* this call modifies ldifEntry */
        type = bv_null;
        value = bv_null;
        rc = slapi_ldif_parse_line(line, &type, &value, &freeval);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "_cl5LDIF2Operation - Failed to parse ldif line, moving on...\n");
            continue;
        }
        if (strncasecmp(type.bv_val, T_CHANGETYPESTR,
                        strlen(T_CHANGETYPESTR) > type.bv_len ? strlen(T_CHANGETYPESTR) : type.bv_len) == 0) {
            op->operation_type = _cl5Str2OperationType(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_REPLGEN, type.bv_len) == 0) {
            *replGen = slapi_ch_strdup(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_CSNSTR, type.bv_len) == 0) {
            op->csn = csn_new_by_string(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_UNIQUEIDSTR, type.bv_len) == 0) {
            op->target_address.uniqueid = slapi_ch_strdup(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_DNSTR, type.bv_len) == 0) {
            PR_ASSERT(op->operation_type);

            if (op->operation_type == SLAPI_OPERATION_ADD) {
                rawDN = slapi_ch_strdup(value.bv_val);
                op->target_address.sdn = slapi_sdn_new_dn_byval(rawDN);
            } else
                op->target_address.sdn = slapi_sdn_new_dn_byval(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_PARENTIDSTR, type.bv_len) == 0) {
            op->p.p_add.parentuniqueid = slapi_ch_strdup(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_NEWRDNSTR, type.bv_len) == 0) {
            op->p.p_modrdn.modrdn_newrdn = slapi_ch_strdup(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_DRDNFLAGSTR, type.bv_len) == 0) {
            op->p.p_modrdn.modrdn_deloldrdn = (strncasecmp(value.bv_val, "true", value.bv_len) ? PR_FALSE : PR_TRUE);
        } else if (strncasecmp(type.bv_val, T_NEWSUPERIORDNSTR, type.bv_len) == 0) {
            op->p.p_modrdn.modrdn_newsuperior_address.sdn = slapi_sdn_new_dn_byval(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_NEWSUPERIORIDSTR, type.bv_len) == 0) {
            op->p.p_modrdn.modrdn_newsuperior_address.uniqueid = slapi_ch_strdup(value.bv_val);
        } else if (strncasecmp(type.bv_val, T_CHANGESTR,
                               strlen(T_CHANGESTR) > type.bv_len ? strlen(T_CHANGESTR) : type.bv_len) == 0) {
            PR_ASSERT(op->operation_type);

            switch (op->operation_type) {
            case SLAPI_OPERATION_ADD:
                /*
                 * When it comes here, case T_DNSTR is already
                 * passed and rawDN is supposed to set.
                 * But it's a good idea to make sure it is
                 * not NULL.
                 */
                if (NULL == rawDN) {
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                                  "_cl5LDIF2Operation - corrupted format "
                                  "for operation type - %lu\n",
                                  op->operation_type);
                    slapi_ch_free_string(&ldifEntryWork);
                    return CL5_BAD_FORMAT;
                }
                mods = parse_changes_string(value.bv_val);
                PR_ASSERT(mods);
                slapi_mods2entry(&(op->p.p_add.target_entry), rawDN,
                                 slapi_mods_get_ldapmods_byref(mods));
                slapi_ch_free((void **)&rawDN);
                slapi_mods_free(&mods);
                break;

            case SLAPI_OPERATION_MODIFY:
                mods = parse_changes_string(value.bv_val);
                PR_ASSERT(mods);
                op->p.p_modify.modify_mods = slapi_mods_get_ldapmods_passout(mods);
                slapi_mods_free(&mods);
                break;

            case SLAPI_OPERATION_MODRDN:
                mods = parse_changes_string(value.bv_val);
                PR_ASSERT(mods);
                op->p.p_modrdn.modrdn_mods = slapi_mods_get_ldapmods_passout(mods);
                slapi_mods_free(&mods);
                break;

            default:
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                              "_cl5LDIF2Operation - Invalid operation type - %lu\n",
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
        if (IsValidOperation(op)) {
            rval = CL5_SUCCESS;
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5LDIF2Operation - Invalid data format\n");
        }
    }
    slapi_ch_free_string(&ldifEntryWork);
    return rval;
}

static int
_cl5WriteOperationTxn(cldb_Handle *cldb, const slapi_operation_parameters *op, void *txn)
{
    int rc;
    int cnt;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    char csnStr[CSN_STRSIZE];
    PRIntervalTime interval;
    CL5Entry entry;
    dbi_txn_t *txnid = NULL;
    dbi_txn_t *parent_txnid = (dbi_txn_t *)txn;
    char *edata = NULL;
    PRUint32 esize = 0;

    /* assign entry time - used for trimming */
    entry.time = slapi_current_utc_time();
    entry.op = (slapi_operation_parameters *)op;

    /* construct the key */
    csn_as_string(op->csn, PR_FALSE, csnStr);
    dblayer_value_set_buffer(cldb->be, &key, csnStr, CSN_STRSIZE);

    /* construct the data */
    rc = _cl5Entry2DBData(&entry, &edata, &esize, cldb->clcrypt_handle);
    if (rc != CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "_cl5WriteOperationTxn - Failed to convert entry with csn (%s) "
                      "to db format\n",
                      csnStr);
        goto done;
    }
    dblayer_value_set(cldb->be, &data, edata, esize);

    /*
     * if this is part of ldif2cl - just write the entry without transaction,
     * and skip to the end.
     */
    if (cldb->dbOpenMode == CL5_OPEN_LDIF2CL) {
        rc = dblayer_db_op(cldb->be, cldb->db, NULL, DBI_OP_PUT, &key, &data);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5WriteOperationTxn - Failed to write entry; db error - %d %s\n",
                          rc, dblayer_strerror(rc));
            rc = CL5_DB_ERROR;
        }
        goto done;
    }

    /* write the entry */
    rc = EAGAIN;
    cnt = 0;

    while ((rc == EAGAIN || rc == DBI_RC_RETRY) && cnt < MAX_TRIALS) {
        if (cnt != 0) {
            /* abort previous transaction */
            rc = TXN_ABORT(cldb, txnid);
            if (rc != 0) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                              "_cl5WriteOperationTxn - Failed to abort transaction; db error - %d %s\n",
                              rc, dblayer_strerror(rc));
                rc = CL5_DB_ERROR;
                goto done;
            }
            /* back off */
            interval = PR_MillisecondsToInterval(slapi_rand() % 100);
            DS_Sleep(interval);
        }
        /* begin transaction */
        rc = TXN_BEGIN(cldb, parent_txnid, &txnid, 0);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5WriteOperationTxn - Failed to start transaction; db error - %d %s\n",
                          rc, dblayer_strerror(rc));
            rc = CL5_DB_ERROR;
            goto done;
        }

        rc = dblayer_db_op(cldb->be, cldb->db, txnid, DBI_OP_PUT, &key, &data);
        if (CL5_OS_ERR_IS_DISKFULL(rc)) {
            slapi_log_err(SLAPI_LOG_CRIT, repl_plugin_name_cl,
                          "_cl5WriteOperationTxn - Changelog DISK FULL; db error - %d %s\n",
                          rc, dblayer_strerror(rc));
            rc = CL5_DB_ERROR;
            goto done;
        }
        if (cnt != 0) {
            if (rc == 0) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl, "_cl5WriteOperationTxn - "
                                                                  "retry (%d) the transaction (csn=%s) succeeded\n",
                              cnt, (char *)key.data);
            } else if ((cnt + 1) >= MAX_TRIALS) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl, "_cl5WriteOperationTxn - "
                                                                  "retry (%d) the transaction (csn=%s) failed (rc=%d (%s))\n",
                              cnt, (char *)key.data, rc, dblayer_strerror(rc));
            }
        }
        cnt++;
    }

    if (rc == 0) /* we successfully added entry */
    {
        rc = TXN_COMMIT(cldb, txnid);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5WriteOperationTxn - Failed to commit transaction; db error - %d %s\n",
                          rc, dblayer_strerror(rc));
            rc = CL5_DB_ERROR;
            goto done;
        }
    } else {
        char s[CSN_STRSIZE];
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5WriteOperationTxn - Failed to write entry with csn (%s); "
                      "db error - %d %s\n",
                      csn_as_string(op->csn, PR_FALSE, s),
                      rc, dblayer_strerror(rc));
        rc = TXN_ABORT(cldb, txnid);
        if (rc != 0) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5WriteOperationTxn - Failed to abort transaction; db error - %d %s\n",
                          rc, dblayer_strerror(rc));
        }
        rc = CL5_DB_ERROR;
        goto done;
    }

    /* update entry count - we assume that all entries are new */
    PR_AtomicIncrement(&cldb->entryCount);

    /* update purge vector if we have not seen any changes from this replica before */
    _cl5UpdateRUV(cldb, op->csn, PR_TRUE, PR_TRUE);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                  "cl5WriteOperationTxn - Successfully written entry with csn (%s)\n", csnStr);
    rc = CL5_SUCCESS;
done:
    dblayer_value_free(cldb->be, &data);

    return rc;
}

static int
_cl5WriteOperation(cldb_Handle *cldb, const slapi_operation_parameters *op)
{
    return _cl5WriteOperationTxn(cldb, op, NULL);
}

PRBool
cl5HelperEntry(const char *csnstr, CSN *csnp)
{
    CSN *csn;
    time_t csnTime;
    PRBool retval = PR_FALSE;

    if (csnp) {
        csn = csnp;
    } else {
        csn = csn_new_by_string(csnstr);
    }
    if (csn == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                      "cl5HelperEntry - Failed to get csn time; csn error\n");
        return PR_FALSE;
    }
    csnTime = csn_get_time(csn);

    if (csnTime == ENTRY_COUNT_TIME || csnTime == PURGE_RUV_TIME) {
        retval = PR_TRUE;
    }

    if (NULL == csnp)
        csn_free(&csn);
    return retval;
}

#ifdef FOR_DEBUGGING
/* Replay iteration helper functions */
static PRBool
_cl5ValidReplayIterator(const CL5ReplayIterator *iterator)
{
    if (iterator == NULL ||
        iterator->consumerRuv == NULL || iterator->supplierRuvObj == NULL ||
        iterator->it_cldb == NULL)
        return PR_FALSE;

    return PR_TRUE;
}
#endif

/* Algorithm: ONREPL!!!
 */
struct replica_hash_entry
{
    ReplicaId rid;      /* replica id */
    PRBool sendChanges; /* indicates whether changes should be sent for this replica */
};


static int
_cl5PositionCursorForReplay(ReplicaId consumerRID, const RUV *consumerRuv, Replica *replica, CL5ReplayIterator **iterator, int *continue_on_missing)
{
    CLC_Buffer *clcache = NULL;
    CSN *startCSN = NULL;
    char csnStr[CSN_STRSIZE];
    int rc = CL5_SUCCESS;
    Object *supplierRuvObj = NULL;
    RUV *supplierRuv = NULL;
    PRBool haveChanges = PR_FALSE;
    char *agmt_name;

    cldb_Handle *cldb = replica_get_cl_info(replica);
    PR_ASSERT (consumerRuv && replica && iterator);

    csnStr[0] = '\0';

    /* get supplier's RUV */
    supplierRuvObj = replica_get_ruv(replica);
    PR_ASSERT(supplierRuvObj);

    if (!supplierRuvObj) {
        rc = CL5_UNKNOWN_ERROR;
        goto done;
    }

    supplierRuv = (RUV *)object_get_data(supplierRuvObj);
    PR_ASSERT(supplierRuv);

    agmt_name = get_thread_private_agmtname();

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5PositionCursorForReplay - (%s): Consumer RUV:\n", agmt_name);
        ruv_dump(consumerRuv, agmt_name, NULL);
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5PositionCursorForReplay - (%s): Supplier RUV:\n", agmt_name);
        ruv_dump(supplierRuv, agmt_name, NULL);
    }


    /* initialize the changelog buffer and do the initial load */
    rc = clcache_get_buffer(replica, &clcache, cldb->db, consumerRID, consumerRuv, supplierRuv);
    if (rc != 0)
        goto done;

    rc = clcache_load_buffer(clcache, &startCSN, continue_on_missing, NULL);

    if (rc == 0) {
        haveChanges = PR_TRUE;
        rc = CL5_SUCCESS;
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            csn_as_string(startCSN, PR_FALSE, csnStr);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "%s: CSN %s found, position set for replay\n", agmt_name, csnStr);
        }
    } else if (rc == DBI_RC_NOTFOUND) {
        /* buffer not loaded.
         * either because no changes have to be sent ==> startCSN is NULL
         * or the calculated startCSN cannot be found in the changelog
         */
        if (startCSN == NULL) {
            rc = CL5_NOTFOUND;
            goto done;
        }
        /* check whether this csn should be present */
        rc = _cl5CheckMissingCSN(startCSN, supplierRuv, cldb);
        if (rc == CL5_MISSING_DATA) /* we should have had the change but we don't */
        {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                csn_as_string(startCSN, PR_FALSE, csnStr);
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                              "repl_plugin_name_cl - %s: CSN %s not found, seems to be missing\n", agmt_name, csnStr);
            }
        } else /* we are not as up to date or we purged */
        {
            csn_as_string(startCSN, PR_FALSE, csnStr);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "repl_plugin_name_cl - %s: CSN %s not found, we aren't as up to date, or we purged\n",
                          agmt_name, csnStr);
        }
    } else {
        csn_as_string(startCSN, PR_FALSE, csnStr);
        /* db error */
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "repl_plugin_name_cl - %s: Failed to retrieve change with CSN %s; db error - %d %s\n",
                      agmt_name, csnStr, rc, dblayer_strerror(rc));

        rc = CL5_DB_ERROR;
    }


    /* setup the iterator */
    if (haveChanges) {
        *iterator = (CL5ReplayIterator *)slapi_ch_calloc(1, sizeof(CL5ReplayIterator));

        if (*iterator == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5PositionCursorForReplay - %s - Failed to allocate iterator\n", agmt_name);
            rc = CL5_MEMORY_ERROR;
            goto done;
        }
        /* ONREPL - should we make a copy of both RUVs here ?*/
        (*iterator)->it_cldb = cldb;
        (*iterator)->clcache = clcache;
        clcache = NULL;
        (*iterator)->consumerRID = consumerRID;
        (*iterator)->consumerRuv = consumerRuv;
        (*iterator)->supplierRuvObj = supplierRuvObj;
        csn_as_string(startCSN, PR_FALSE, (*iterator)->starting_csn);
    } else if (rc == CL5_SUCCESS) {
        /* we have no changes to send */
        rc = CL5_NOTFOUND;
    }

done:
    if (clcache)
        clcache_return_buffer(&clcache);

    if (rc != CL5_SUCCESS) {
        if (supplierRuvObj)
            object_release(supplierRuvObj);
    }

    return rc;
}

struct ruv_it
{
    CSN **csns; /* csn list */
    int alloc;  /* allocated size */
    int pos;    /* position in the list */
};

static int
ruv_consumer_iterator(const ruv_enum_data *enum_data, void *arg)
{
    struct ruv_it *data = (struct ruv_it *)arg;

    PR_ASSERT(data);

    /* check if we have space for one more element */
    if (data->pos >= data->alloc - 2) {
        data->alloc += 4;
        data->csns = (CSN **)slapi_ch_realloc((void *)data->csns, data->alloc * sizeof(CSN *));
    }

    data->csns[data->pos] = csn_dup(enum_data->csn);
    data->pos++;

    return 0;
}


static int
ruv_supplier_iterator(const ruv_enum_data *enum_data, void *arg)
{
    int i;
    PRBool found = PR_FALSE;
    ReplicaId rid;
    struct ruv_it *data = (struct ruv_it *)arg;

    PR_ASSERT(data);

    rid = csn_get_replicaid(enum_data->min_csn);
    /* check if the replica that generated the csn is already in the list */
    for (i = 0; i < data->pos; i++) {
        if (rid == csn_get_replicaid(data->csns[i])) {
            found = PR_TRUE;

            /* remove datacsn[i] if it is greater or equal to the supplier's maxcsn */
            if (csn_compare(data->csns[i], enum_data->csn) >= 0) {
                int j;

                csn_free(&data->csns[i]);
                for (j = i + 1; j < data->pos; j++) {
                    data->csns[j - 1] = data->csns[j];
                }
                data->pos--;
            }
            break;
        }
    }

    if (!found) {
        /* check if we have space for one more element */
        if (data->pos >= data->alloc - 2) {
            data->alloc += 4;
            data->csns = (CSN **)slapi_ch_realloc((void *)data->csns,
                                                  data->alloc * sizeof(CSN *));
        }

        data->csns[data->pos] = csn_dup(enum_data->min_csn);
        data->pos++;
    }
    return 0;
}


static int
my_csn_compare(const void *arg1, const void *arg2)
{
    return (csn_compare(*((CSN **)arg1), *((CSN **)arg2)));
}


/* builds CSN ordered list of all csns in the RUV */
CSN **
cl5BuildCSNList(const RUV *consRuv, const RUV *supRuv)
{
    struct ruv_it data;
    int count, rc;
    CSN **csns;

    PR_ASSERT(consRuv);

    count = ruv_replica_count(consRuv);
    csns = (CSN **)slapi_ch_calloc(count + 1, sizeof(CSN *));

    data.csns = csns;
    data.alloc = count + 1;
    data.pos = 0;

    /* add consumer elements to the list */
    rc = ruv_enumerate_elements(consRuv, ruv_consumer_iterator, &data);
    if (rc == 0 && supRuv) {
        /* add supplier elements to the list */
        rc = ruv_enumerate_elements(supRuv, ruv_supplier_iterator, &data);
    }

    /* we have no csns */
    if (data.csns[0] == NULL) {
        /* csns might have been realloced in ruv_supplier_iterator() */
        slapi_ch_free((void **)&data.csns);
        csns = NULL;
    } else {
        csns = data.csns;
        data.csns[data.pos] = NULL;
        if (rc == 0) {
            qsort(csns, data.pos, sizeof(CSN *), my_csn_compare);
        } else {
            cl5DestroyCSNList(&csns);
        }
    }

    return csns;
}

void
cl5DestroyCSNList(CSN ***csns)
{
    if (csns && *csns) {
        int i;

        for (i = 0; (*csns)[i]; i++) {
            csn_free(&(*csns)[i]);
        }

        slapi_ch_free((void **)csns);
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
static int
_cl5CheckMissingCSN(const CSN *csn, const RUV *supplierRuv, cldb_Handle *cldb)
{
    ReplicaId rid;
    CSN *supplierCsn = NULL;
    CSN *purgeCsn = NULL;
    int rc = CL5_SUCCESS;
    char csnStr[CSN_STRSIZE];

    PR_ASSERT(csn && supplierRuv && cldb);

    rid = csn_get_replicaid(csn);
    ruv_get_largest_csn_for_replica(supplierRuv, rid, &supplierCsn);
    if (supplierCsn == NULL) {
        /* we have not seen any changes from this replica so it is
           ok not to have this csn */
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN - "
                                                               "can't locate %s csn: we have not seen any changes for replica %d\n",
                          csn_as_string(csn, PR_FALSE, csnStr), rid);
        }
        return CL5_SUCCESS;
    }

    ruv_get_largest_csn_for_replica(cldb->purgeRUV, rid, &purgeCsn);
    if (purgeCsn == NULL) {
        /* changelog never contained any changes for this replica */
        if (csn_compare(csn, supplierCsn) <= 0) {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN - "
                                                                   "the change with %s csn was never logged because it was imported "
                                                                   "during replica initialization\n",
                              csn_as_string(csn, PR_FALSE, csnStr));
            }
            rc = CL5_PURGED_DATA; /* XXXggood is that the correct return value? */
        } else {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN - "
                                                                   "change with %s csn has not yet been seen by this server; "
                                                                   " last csn seen from that replica is %s\n",
                              csn_as_string(csn, PR_FALSE, csnStr),
                              csn_as_string(supplierCsn, PR_FALSE, csnStr));
            }
            rc = CL5_SUCCESS;
        }
    } else /* we have both purge and supplier csn */
    {
        if (csn_compare(csn, purgeCsn) < 0) /* the csn is below the purge point */
        {
            rc = CL5_PURGED_DATA;
        } else {
            if (csn_compare(csn, supplierCsn) <= 0) /* we should have the data but we don't */
            {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN - "
                                                                       "change with %s csn has been purged by this server; "
                                                                       "the current purge point for that replica is %s\n",
                                  csn_as_string(csn, PR_FALSE, csnStr),
                                  csn_as_string(purgeCsn, PR_FALSE, csnStr));
                }
                rc = CL5_MISSING_DATA;
            } else {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl, "_cl5CheckMissingCSN - "
                                                                       "change with %s csn has not yet been seen by this server; "
                                                                       " last csn seen from that replica is %s\n",
                                  csn_as_string(csn, PR_FALSE, csnStr),
                                  csn_as_string(supplierCsn, PR_FALSE, csnStr));
                }
                rc = CL5_SUCCESS;
            }
        }
    }

    if (supplierCsn)
        csn_free(&supplierCsn);

    if (purgeCsn)
        csn_free(&purgeCsn);

    return rc;
}

/* Helper functions that work with individual changelog files */

/*
 * _cl5PurgeRid helper: dblayer_cursor_iterate callback.
 * Returns:
 *     DBI_RC_SUCCESS to iterate on next entry
 *     DBI_RC_NOTFOUND to stop iteration with DBI_RC_SUCCESS code
 *     other DBI_RC_ code to stop iteration with that error code.
 */
int
_cl5ExportEntry2File(dbi_val_t *key, dbi_val_t *data, void *ctx)
{
    DBLCI_CTX *dblcictx = ctx;
    slapi_operation_parameters op = {0};
    cldb_Handle *cldb = dblcictx->cldb;
    CL5Entry entry = {0};
    PRInt32 len, wlen;
    char *buff;
    int rc = _cl5CICbInit(key, data, dblcictx);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }
    entry.op = &op;
    rc = cl5DBData2Entry(data->data, data->size, &entry, cldb->clcrypt_handle);
    if (rc != DBI_RC_SUCCESS) {
        return rc;
    }
    rc = _cl5Operation2LDIF(&op, cldb->ident, &buff, &len);
    if (rc != CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "_cl5ExportEntry2File - Failed to convert operation to ldif\n");
    } else {
        wlen = slapi_write_buffer(dblcictx->exportFile, buff, len);
        slapi_ch_free_string(&buff);
        if (wlen < len) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "_cl5ExportEntry2File - Failed to write to ldif file\n");
            rc = CL5_SYSTEM_ERROR;
        }
    }
    operation_parameters_done(&op);
    return rc;
}

static int
_cl5ExportFile(PRFileDesc *prFile, cldb_Handle *cldb)
{
    DBLCI_CTX dblcictx = {0};
    int rc = CL5_SUCCESS;
    PR_ASSERT(prFile && cldb);

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        ruv_dump(cldb->purgeRUV, "clpurgeruv", prFile);
        ruv_dump(cldb->maxRUV, "clmaxruv", prFile);
    }
    slapi_write_buffer(prFile, "\n", strlen("\n"));

    dblcictx.seen.nbmax = CL5_PURGE_MAX_LOOKUP_PER_TRANSACTION;
    dblcictx.exportFile = prFile;
    rc = _cl5Iterate(cldb, _cl5ExportEntry2File, &dblcictx, PR_TRUE);

    return (rc == CL5_NOTFOUND) ? CL5_SUCCESS : rc;
}

static char *
_cl5GetHelperEntryKey(int type, char *csnStr)
{
    CSN *csn = csn_new();
    char *rt;

    csn_set_time(csn, (time_t)type);
    csn_set_replicaid(csn, 0);

    rt = csn_as_string(csn, PR_FALSE, csnStr);
    csn_free(&csn);

    return rt;
}

/*
 * Write RUVs into the changelog;
 * implemented for backup to make sure the backed up changelog contains RUVs
 * Return values: 0 -- success
 *                1 -- failure
 */
static int
_cl5WriteReplicaRUV(Replica *r, void *arg)
{
    int rc = 0;
    cldb_Handle *cldb = replica_get_cl_info(r);

    if (NULL == cldb) {
        /* TBD should this really happen, do we need an error msg */
        return rc;
    }

    _cl5WriteEntryCount(cldb);
    rc = _cl5WriteRUV(cldb, PR_TRUE);
    rc |= _cl5WriteRUV(cldb, PR_FALSE);
    ruv_destroy(&cldb->maxRUV);
    ruv_destroy(&cldb->purgeRUV);

    return rc;
}

static char *
_cl5LdifFileName(char *instance_ldif)
{
    char *cl_ldif = NULL;
    char *p = strstr(instance_ldif, ".ldif");

    if (p) {
        *p = '\0';
        cl_ldif = slapi_ch_smprintf("%s_cl.ldif", instance_ldif);
    } else {
        cl_ldif = slapi_ch_smprintf("%s_cl", instance_ldif);
    }

    return cl_ldif;
}

int
cl5Import(Slapi_PBlock *pb)
{
    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "cl5Export - Importing changelog\n");

    /* TBD
     * as in cl5Export
     * get ldif dir from pblock
     * generate cl ldif name
     * call clImportLDIF
     */
    return 0;
}

int
cl5Export(Slapi_PBlock *pb)
{
    char *instance_name;
    char *instance_ldif;
    char *instance_cl_ldif;
    Slapi_Backend *be;
    Replica *replica = NULL;
    int rc;

    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_FILE, &instance_ldif);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    replica = replica_get_replica_from_dn(slapi_be_getsuffix(be, 0));
    if (replica == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                              "cl5Export - No replica defined for instance %s\n", instance_name);
        return 0;
    }

    instance_cl_ldif = _cl5LdifFileName(instance_ldif);
    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                  "cl5Export - Exporting changelog for instance %s to file %s\n",
                  instance_name, instance_cl_ldif);
    rc = cl5ExportLDIF(instance_cl_ldif, replica);

    return rc;
}

/*
 *  Clean the in memory RUV, at shutdown we will write the update to the db
 */
void
cl5CleanRUV(ReplicaId rid, Replica *replica)
{
    cldb_Handle *cldb = replica_get_cl_info(replica);

    if (cldb == NULL) {
        return;
    }
    ruv_delete_replica(cldb->purgeRUV, rid);
    ruv_delete_replica(cldb->maxRUV, rid);
}

static void
free_purge_data(cleanruv_purge_data *purge_data)
{
    slapi_ch_free((void **)&purge_data);
}

/*
 * Create a thread to purge a changelog of cleaned RIDs
 */
void
trigger_cl_purging(cleanruv_purge_data *purge_data)
{
    PRThread *trim_tid = NULL;

    trim_tid = PR_CreateThread(PR_USER_THREAD, (VFP)(void *)trigger_cl_purging_thread,
                               (void *)purge_data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                               PR_UNJOINABLE_THREAD, DEFAULT_THREAD_STACKSIZE);
    if (NULL == trim_tid) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "trigger_cl_purging - Failed to create cl purging "
                      "thread; NSPR error - %d\n",
                      PR_GetError());
        free_purge_data(purge_data);
    } else {
        /* need a little time for the thread to get started */
        DS_Sleep(PR_SecondsToInterval(1));
    }
}

/*
 * Purge a changelog of entries that originated from a particular replica(rid)
 */
void
trigger_cl_purging_thread(void *arg)
{
    cleanruv_purge_data *purge_data = (cleanruv_purge_data *)arg;
    Replica *replica = purge_data->replica;
    cldb_Handle *cldb = replica_get_cl_info(replica);

    pthread_mutex_lock(&(cldb->stLock));
    /* Make sure we have a change log, and we aren't closing it */
    if (cldb->dbState != CL5_STATE_OPEN) {
        goto free_and_return;
    }

    slapi_counter_increment(cldb->clThreads);

    /* Purge the changelog */
    _cl5DoPurging(purge_data);

    slapi_counter_decrement(cldb->clThreads);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                  "trigger_cl_purging_thread - purged changelog for (%s) rid (%d)\n",
                  slapi_sdn_get_dn(purge_data->suffix_sdn), purge_data->cleaned_rid);

free_and_return:
    pthread_mutex_unlock(&(cldb->stLock));
    free_purge_data(purge_data);
}

char *
cl5GetLdifDir(Slapi_Backend *be)
{
    char *dir = NULL;
    char *dbdir = NULL;

    if (NULL == be) {
        dir = slapi_ch_strdup("/tmp");
    } else {
        slapi_back_get_info(be, BACK_INFO_DIRECTORY, (void **)&dbdir);
        dir = slapi_ch_smprintf("%s/../ldif",dbdir);
    }
    return dir;
}

int32_t
cldb_is_open(Replica *replica)
{
    cldb_Handle *cldb = replica_get_cl_info(replica);
    int32_t open = 0; /* not open */

    if (cldb) {
        pthread_mutex_lock(&(cldb->stLock));
        open = (cldb->dbState == CL5_STATE_OPEN);
        pthread_mutex_unlock(&(cldb->stLock));
    }

    return open;
}
