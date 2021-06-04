/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "mdb_layer.h"
#include <prthread.h>
#include <prclist.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <glob.h>


Slapi_ComponentId *dbmdb_componentid;

#define BULKOP_MAX_RECORDS  100 /* Max records handled by a single bulk operations */

/* bulkdata->v.data contents */
typedef struct {
    int use_multiple;        /* If if we use GET_MULTIPLE/NEXT_MULTIPLE or use single operation method */
    uint dbi_flags;          /* dbi flags */
    MDB_cursor *cursor;      /* cursor position */
    int op;                  /* MDB operation to get next value */
    int maxrecords;          /* Number maximum of operation before moving to next block */
    MDB_val data;            /* data for single or multiple operation */
    MDB_val key;             /* key */
    size_t data_size;        /* Size of a single item in data */
} dbmdb_bulkdata_t;


/* Use these macros to incr/decrement the thread count for the
   database housekeeping threads.  This ensures that the
   value is changed in a thread safe manner, and safely notifies
   the main thread during cleanup. INCR_THREAD_COUNT should be
   the first real statement in the thread function, before any
   actual work is done, other than perhaps variable assignments.
   DECR_THREAD_COUNT should be called as the next to last thing
   in the thread function, just before the trace log message and
   return.
*/
#define INCR_THREAD_COUNT(pEnv)       \
    pthread_mutex_lock(&pEnv->dbmdb_thread_count_lock); \
    ++pEnv->dbmdb_thread_count;     \
    pthread_mutex_unlock(&pEnv->dbmdb_thread_count_lock)

#define DECR_THREAD_COUNT(pEnv)                  \
    pthread_mutex_lock(&pEnv->dbmdb_thread_count_lock);            \
    if (--pEnv->dbmdb_thread_count == 0) {     \
        pthread_cond_broadcast(&pEnv->dbmdb_thread_count_cv); \
    }                                            \
    pthread_mutex_unlock(&pEnv->dbmdb_thread_count_lock)

#define NEWDIR_MODE 0755
#define DB_REGION_PREFIX "__db."

static int dbmdb_force_checkpoint(struct ldbminfo *li);
static int dbmdb_restore_file_check(struct ldbminfo *li);

#define MEGABYTE (1024 * 1024)
#define GIGABYTE (1024 * MEGABYTE)

/* env. vars. you can set to stress txn handling */
#define TXN_TESTING "TXN_TESTING"               /* enables the txn test thread */
#define TXN_TEST_HOLD_MSEC "TXN_TEST_HOLD_MSEC" /* time to hold open the db cursors */
#define TXN_TEST_LOOP_MSEC "TXN_TEST_LOOP_MSEC" /* time to wait before looping again */
#define TXN_TEST_USE_TXN "TXN_TEST_USE_TXN"     /* use transactions or not */
#define TXN_TEST_USE_RMW "TXN_TEST_USE_RMW"     /* use DB_RMW for c_get flags or not */
#define TXN_TEST_INDEXES "TXN_TEST_INDEXES"     /* list of indexes to use - comma delimited - id2entry,entryrdn,etc. */
#define TXN_TEST_VERBOSE "TXN_TEST_VERBOSE"     /* be wordy */


/* this flag is used if user remotely turned batching off */
#define FLUSH_REMOTEOFF 0


/*
 * return nsslapd-db-home-directory (dbmdb_dbhome_directory), if exists.
 * Otherwise, return nsslapd-directory (dbmdb_home_directory).
 *
 * if dbmdb_dbhome_directory exists, set 1 to dbhome.
 */
char *
dbmdb_get_home_dir(struct ldbminfo *li, int *dbhome)
{
    dbmdb_ctx_t *conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    if (conf->home[0]) {
        *dbhome = 1;
        return conf->home;
    }
    slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_get_home_dir", "Db home directory is not set. "
                      "Possibly %s (optionally %s) is missing in the config file.\n",
                      CONFIG_DIRECTORY, CONFIG_DB_HOME_DIRECTORY);
    return NULL;
}

/*
 * return the top db directory
 */
char *
dbmdb_get_db_dir(struct ldbminfo *li)
{
    return li->li_directory;
}

/* Function which calls limdb to override some system calls which
 * the library makes. We call this before calling any other function
 * in limdb.
 * Several OS use this, either partially or completely.
 * This will eventually change---we will simply pass to limdb
 * the addresses of a bunch of NSPR functions, and everything
 * will magically work on all platforms (Ha!)
 */

#ifdef DB_USE_64LFS
/* What is going on here ?
 * Well, some platforms now support an extended API for dealing with
 * files larger than 2G.  (This apparently comes from the LFS -- "Large
 * File Summit"... Summit, indeed.)  Anyway, we try to detect at runtime
 * whether this machine has the extended API, and use it if it's present.
 *
 */


/* helper function for open64 */
static int
dbmdb_open_large(const char *path, int oflag, mode_t mode)
{
    int err;

    err = open64(path, oflag, mode);
    /* weird but necessary: */
    if (err >= 0)
        errno = 0;
    return err;
}

/* this is REALLY dumb.  but nspr 19980529(x) doesn't support 64-bit files
 * because of some weirdness we're doing at initialization (?), so we need
 * to export some function that can open huge files, so that exporting
 * can work right.  when we fix the nspr problem (or get a more recent
 * version of nspr that might magically work?), this should be blown away.
 * (call mode_t an int because NT can't handle that in prototypes.)
 * -robey, 28oct98
 */
int
dbmdb_open_huge_file(const char *path, int oflag, int mode)
{
    return dbmdb_open_large(path, oflag, (mode_t)mode);
}

/* Helper function for large seeks, db4.3 */
static int
dbmdb_seek43_large(int fd, off64_t offset, int whence)
{
    off64_t ret = 0;

    ret = lseek64(fd, offset, whence);

    return (ret < 0) ? errno : 0;
}

#else /* DB_USE_64LFS */

int
dbmdb_open_huge_file(const char *path, int oflag, int mode)
{
    return open(path, oflag, mode);
}

#endif /* DB_USE_64LFS */

/*
 * This function is called after all the config options have been read in,
 * so we can do real initialization work here.
 */

int
dbmdb_start(struct ldbminfo *li, int dbmode)
{
    int readonly = dbmode & (DBLAYER_ARCHIVE_MODE | DBLAYER_EXPORT_MODE | DBLAYER_TEST_MODE);
    dblayer_init_pvt_txn();    /* Initialize thread local storage for handling dblayer txn */
    return dbmdb_make_env(MDB_CONFIG(li), readonly, li->li_mode);
}

/* mode is one of
 * DBLAYER_NORMAL_MODE,
 * DBLAYER_INDEX_MODE,
 * DBLAYER_IMPORT_MODE,
 * DBLAYER_EXPORT_MODE
 */
int
dbmdb_instance_start(backend *be, int mode)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    int return_value = -1;
    dbmdb_dbi_t id2entry_dbi = {0};

    if (!ctx->env) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_instance_start", "Backend %s: dbenv is not available.\n",
                      inst ? inst->inst_name : "unknown");
        return return_value;
    }

    /* Need to duplicate because ldbm_instance_destructor frees both values */
    inst->inst_dir_name = slapi_ch_strdup(inst->inst_name);

    if (NULL != inst->inst_id2entry) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "dbmdb_instance_start", "Backend \"%s\" already started.\n",
                      inst->inst_name);
        return 0;
    }

    if (attrcrypt_init(inst)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_instance_start", "Unable to initialize attrcrypt system for %s\n",
                      inst->inst_name);
        return return_value;
    }


    /* Now attempt to open id2entry */
    return_value = dbmdb_open_dbi_from_filename(&id2entry_dbi, be, ID2ENTRY LDBM_FILENAME_SUFFIX, NULL, MDB_CREATE);
    if (return_value == 0) {
        dbmdb_mdbdbi2dbi_db(&id2entry_dbi, &inst->inst_id2entry);
        if ((mode & DBLAYER_NORMAL_MODE) && id2entry_dbi.state.dataversion != DBMDB_CURRENT_DATAVERSION) {
            return_value = dbmdb_ldbm_upgrade(inst, id2entry_dbi.state.dataversion);
        }
    }

    if (0 == return_value) {
        /* get nextid from disk now */
        get_ids_from_disk(be);
    }

    if (mode & DBLAYER_NORMAL_MODE) {
        /* richm - not sure if need to acquire the be lock first? */
        /* need to set state back to started - set to stopped in
           dblayer_instance_close */
        be->be_state = BE_STATE_STARTED;
    }

    /*
     * check if nextid is valid: it only matters if the database is either
     * being imported or is in normal mode
     */
    if (inst->inst_nextid > MAXID && !(mode & DBLAYER_EXPORT_MODE)) {
        slapi_log_err(SLAPI_LOG_CRIT, "dbmdb_instance_start", "Backend '%s' "
                                                                "has no IDs left. DATABASE MUST BE REBUILT.\n",
                      be->be_name);
        return 1;
    }

    if (return_value != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_instance_start", "Failure %s (%d)\n",
                      dblayer_strerror(return_value), return_value);
    }
    return return_value;
}

void
dbmdb_pre_close(struct ldbminfo *li)
{
    /* There is no database threads ==> nothing to do */
}

int
dbmdb_post_close(struct ldbminfo *li, int dbmode)
{
    dbmdb_ctx_t *conf = 0;
    int return_value = 0;
    PR_ASSERT(NULL != li);
    dblayer_private *priv = li->li_dblayer_private;

    conf = (dbmdb_ctx_t *)li->li_dblayer_config;

    /* We close all the files we ever opened, and call pEnv->close. */
    if (NULL == conf->env) /* db env is already closed. do nothing. */
        return return_value;
    /* Shutdown the performance counter stuff */
    if (DBLAYER_NORMAL_MODE & dbmode) {
        if (conf->perf_private) {
            dbmdb_perfctrs_terminate(&conf->perf_private, conf->env);
        }
    }

    /* Now release the db environment */
    dbmdb_ctx_close(conf);
    priv->dblayer_env = NULL;

    return return_value;
}

/*
 * This function is called when the server is shutting down, or when the
 * backend is being disabled (e.g. backup/restore).
 * This is not safe to call while other threads are calling into the open
 * databases !!!   So: DON'T !
 */
int
dbmdb_close(struct ldbminfo *li, int dbmode)
{
    backend *be = NULL;
    ldbm_instance *inst;
    Object *inst_obj;
    int return_value = 0;
    int shutdown = g_get_shutdown();

    dbmdb_pre_close(li);

    /*
     * dblayer_close_indexes and pDB->close used to be located above loop:
     *   while(priv->dblayer_thread_count > 0) in pre_close.
     * This order fixes a bug: shutdown under the stress makes txn_checkpoint
     * (checkpoint_thread) fail b/c the mpool might have been already closed.
     */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (shutdown) {
            vlv_close(inst);
        }
        be = inst->inst_be;
        if (NULL != be->be_instance_info) {
            return_value |= dblayer_instance_close(be);
        }
    }

    return_value |= dbmdb_post_close(li, dbmode);

    return return_value;
}

/* API to remove the environment */
int
dbmdb_remove_env(struct ldbminfo *li)
{
#ifdef TODO
    MDB_env *env = NULL;
    char *home_dir = NULL;
    int rc = db_env_create(&env, 0);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_remove_env", "Failed to create MDB_env (returned: %d)\n", rc);
        return rc;
    }
    if (NULL == li) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_remove_env", "No ldbm info is given\n");
        return -1;
    }

    home_dir = dbmdb_get_home_dir(li, NULL);
    if (home_dir) {
        rc = env->remove(env, home_dir, 0);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_remove_env", "Failed to remove MDB_dbienvironment files. "
                                                "Please remove %s/__db.00# (# is 1 through 6)\n",
                          home_dir);
        }
    }
    return rc;
#endif /* TODO */
}

/* Determine mdb open flags according to dbi type */
int dbmdb_get_open_flags(const char *dbname)
{
    const char *pt = strrchr(dbname, '/');
    if (!pt) {
        pt = dbname;
    }

    if (!strcasecmp(pt, LDBM_ENTRYRDN_STR LDBM_FILENAME_SUFFIX)) {
        return MDB_DUPSORT;
    }
    if (!strcasecmp(pt, ID2ENTRY LDBM_FILENAME_SUFFIX)) {
        return 0;
    }
    if (strstr(pt,  "changelog")) {
        return 0;
    }
    /* Otherwise assume it is an index */
    return MDB_DUPSORT + MDB_INTEGERDUP + MDB_DUPFIXED;
}


/* Routines for opening and closing a db instance (MDB_dbi) in the MDB_env.
   Used by ldif2db merging code currently.

   Return value:
       Success: 0
    Failure: -1
 */
int
dbmdb_get_db(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int open_flags = 0;
    dbmdb_cursor_t cur = {0};
    dblayer_private *priv = NULL;
    int return_value = 0;

    PR_ASSERT(NULL != li);
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    *ppDB = NULL;

    if (NULL == inst->inst_name) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_get_db", "Backend instance name is not configured.\n");
        return -1;
    }

    open_flags = 0;
    if (open_flag & MDB_OPEN_DIRTY_DBI)
        open_flags |= MDB_OPEN_DIRTY_DBI;
    if (open_flag & DBOPEN_CREATE)
        open_flags |= MDB_CREATE;
    if (open_flag & DBOPEN_TRUNCATE)
        open_flags |= MDB_TRUNCATE_DBI;
    if (open_flag & DBOPEN_ALLOW_DIRTY)
        open_flags |= MDB_OPEN_DIRTY_DBI;
    /* If import mode try to open in dirty mode */
    if (ai && (ai->ai_indexmask & INDEX_OFFLINE))
        open_flags |= MDB_OPEN_DIRTY_DBI;
    if (dbmdb_public_in_import(inst))
        open_flags |= MDB_OPEN_DIRTY_DBI;

    return_value = dbmdb_open_dbi_from_filename(&cur.dbi, be, indexname, NULL, open_flags);
    if (0 != return_value)
        goto out;

    dbmdb_mdbdbi2dbi_db(&cur.dbi, ppDB);

out:
    return return_value;
}

int
dbmdb_close_file(dbmdb_dbi_t **db)
{
    /* There is no need to close the mdb db instance handles because there is no associated file descriptor
     * and otherresources get freed when closing the global MDB_env .
     * But we still must free the dbmdb_dbi_t struct.
     */
    slapi_ch_free((void**)db);
    return 1;
}


/*
  dbmdb_db_remove assumptions:

  No environment has the given database open.

*/


#define DBLAYER_CACHE_DELAY PR_MillisecondsToInterval(5)
int
dbmdb_rm_db_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_checkpoint)
{
    struct ldbminfo *li = NULL;
    ldbm_instance *inst = NULL;
    dblayer_handle *handle = NULL;
    char *dbname = NULL;
    int rc = 0;
    dbi_db_t *db = 0;
    dbmdb_ctx_t *conf = NULL;

    if ((NULL == be) || (NULL == be->be_database)) {
        return rc;
    }
    inst = (ldbm_instance *)be->be_instance_info;
    if (NULL == inst) {
        return rc;
    }
    li = (struct ldbminfo *)be->be_database->plg_private;
    if (NULL == li) {
        return rc;
    }
    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    if (NULL == conf || NULL == conf->env) { /* db does not exist or not started */
        return rc;
    }

    if (0 == dblayer_get_index_file(be, a, (dbi_db_t**)&db, MDB_OPEN_DIRTY_DBI /* Don't create an index file
                                                   if it does not exist but open dirty files. */)) {
        /* first, remove the file handle for this index, if we have it open */
        PR_Lock(inst->inst_handle_list_mutex);
        if (a->ai_dblayer) {
            /* there is a handle */
            handle = (dblayer_handle *)a->ai_dblayer;

            /* when we successfully called dblayer_get_index_file we bumped up
         the reference count of how many threads are using the index. So we
         must manually back off the count by one here.... rwagner */

            dblayer_release_index_file(be, a, db);

            while (slapi_atomic_load_64(&(a->ai_dblayer_count), __ATOMIC_ACQUIRE) > 0) {
                /* someone is using this index file */
                /* ASSUMPTION: you have already set the INDEX_OFFLINE flag, because
                 * you intend to mess with this index.  therefore no new requests
                 * for this indexfile should happen, so the dblayer_count should
                 * NEVER increase.
                 */
                PR_ASSERT(a->ai_indexmask & INDEX_OFFLINE);
                PR_Unlock(inst->inst_handle_list_mutex);
                DS_Sleep(DBLAYER_CACHE_DELAY);
                PR_Lock(inst->inst_handle_list_mutex);
            }

            rc = dbmdb_dbi_remove(conf, &db);
            slapi_ch_free_string(&dbname);
            a->ai_dblayer = NULL;
            if (rc == 0) {
                /* remove handle from handle-list */
                if (inst->inst_handle_head == handle) {
                    inst->inst_handle_head = handle->dblayer_handle_next;
                    if (inst->inst_handle_tail == handle) {
                        inst->inst_handle_tail = NULL;
                    }
                } else {
                    dblayer_handle *hp;

                    for (hp = inst->inst_handle_head; hp; hp = hp->dblayer_handle_next) {
                        if (hp->dblayer_handle_next == handle) {
                            hp->dblayer_handle_next = handle->dblayer_handle_next;
                            if (inst->inst_handle_tail == handle) {
                                inst->inst_handle_tail = hp;
                            }
                            break;
                        }
                    }
                }
                slapi_ch_free((void **)&handle);
            } else {
                rc = -1; /* Failed to remove the db instance */
            }
        } else {
            /* no handle to close */
        }
        PR_Unlock(inst->inst_handle_list_mutex);
    }

    return rc;
}


/*
 * Transaction stuff. The idea is that the caller doesn't need to
 * know the transaction mechanism underneath (because the caller is
 * typically a few calls up the stack from any MDB_dbistuff).
 * Sadly, in slapd there was no handy structure associated with
 * an LDAP operation, and passed around everywhere, so we had
 * to invent the back_txn structure.
 * The lower levels of the back-end look into this structure, and
 * take out the MDB_txn they need.
 */

int
dbmdb_txn_begin(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dbmdb_ctx_t *conf = NULL;
    dblayer_private *priv = NULL;
    back_txn new_txn = {NULL};
    PR_ASSERT(NULL != li);
    /*
     * When server is shutting down, some components need to
     * flush some data (e.g. replication to write ruv).
     * So don't check shutdown signal unless we can't write.
     */
    if (g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL) {
        return return_value;
    }

    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    if (txn) {
        txn->back_txn_txn = NULL;
    }

    if (1) {
        dbi_txn_t *new_txn_back_txn_txn = NULL;

        if (use_lock)
            slapi_rwlock_rdlock(&conf->dbmdb_env_lock);
        if (!parent_txn) {
            /* see if we have a stored parent txn */
            back_txn *par_txn_txn = dblayer_get_pvt_txn();
            if (par_txn_txn) {
                parent_txn = par_txn_txn->back_txn_txn;
            }
        }
        return_value = START_TXN(&new_txn_back_txn_txn, parent_txn, 0);
        return_value = dbmdb_map_error(__FUNCTION__, return_value);
        if (0 != return_value) {
            if (use_lock)
                slapi_rwlock_unlock(&conf->dbmdb_env_lock);
        } else {
            new_txn.back_txn_txn = new_txn_back_txn_txn;
            /* this txn is now our current transaction for current operations
               and new parent for any nested transactions created */
            dblayer_push_pvt_txn(&new_txn);
            if (txn) {
                txn->back_txn_txn = new_txn.back_txn_txn;
            }
        }
    } else {
        return_value = 0;
    }
    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "dblayer_txn_begin_ext", "Serious Error---Failed in dblayer_txn_begin, err=%d (%s)\n",
                      return_value, dblayer_strerror(return_value));
    }
    return return_value;
}

int
dbmdb_txn_commit(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dbmdb_ctx_t *conf = NULL;
    dblayer_private *priv = NULL;
    dbi_txn_t *db_txn = NULL;
    back_txn *cur_txn = NULL;

    PR_ASSERT(NULL != li);

    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* use the transaction we are given - if none, see if there
       is a transaction in progress */
    if (txn) {
        db_txn = txn->back_txn_txn;
    }
    cur_txn = dblayer_get_pvt_txn();
    if (!db_txn) {
        if (cur_txn) {
            db_txn = cur_txn->back_txn_txn;
        }
    }
    if (NULL != db_txn && conf->env) {
        /* if we were given a transaction, and it is the same as the
           current transaction in progress, pop it off the stack
           or, if no transaction was given, we must be using the
           current one - must pop it */
        if (!txn || (cur_txn && (cur_txn->back_txn_txn == db_txn))) {
            dblayer_pop_pvt_txn();
        }
        return_value = END_TXN(&db_txn, 0);
        return_value = dbmdb_map_error(__FUNCTION__, return_value);
        if (txn) {
            /* this handle is no longer value - set it to NULL */
            txn->back_txn_txn = NULL;
        }
        if (use_lock)
            slapi_rwlock_unlock(&conf->dbmdb_env_lock);
    } else {
        return_value = 0;
    }

    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "dblayer_txn_commit_ext", "Serious Error---Failed in dblayer_txn_commit, err=%d (%s)\n",
                      return_value, dblayer_strerror(return_value));
        if (LDBM_OS_ERR_IS_DISKFULL(return_value)) {
            operation_out_of_disk_space();
        }
    }
    return return_value;
}

int
dbmdb_txn_abort(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dblayer_private *priv = NULL;
    dbi_txn_t *db_txn = NULL;
    back_txn *cur_txn = NULL;
    dbmdb_ctx_t *conf = NULL;

    PR_ASSERT(NULL != li);

    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* use the transaction we are given - if none, see if there
       is a transaction in progress */
    if (txn) {
        db_txn = txn->back_txn_txn;
    }
    cur_txn = dblayer_get_pvt_txn();
    if (!db_txn) {
        if (cur_txn) {
            db_txn = cur_txn->back_txn_txn;
        }
    }
    if (NULL != db_txn && conf->env) {
        /* if we were given a transaction, and it is the same as the
           current transaction in progress, pop it off the stack
           or, if no transaction was given, we must be using the
           current one - must pop it */
        if (!txn || (cur_txn && (cur_txn->back_txn_txn == db_txn))) {
            dblayer_pop_pvt_txn();
        }
        END_TXN(&db_txn, 1);
        return_value = 0;
        if (txn) {
            /* this handle is no longer value - set it to NULL */
            txn->back_txn_txn = NULL;
        }
        if (use_lock)
            slapi_rwlock_unlock(&conf->dbmdb_env_lock);
    } else {
        return_value = 0;
    }

    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "dblayer_txn_abort_ext", "Serious Error---Failed in dblayer_txn_abort, err=%d (%s)\n",
                      return_value, dblayer_strerror(return_value));
        if (LDBM_OS_ERR_IS_DISKFULL(return_value)) {
            operation_out_of_disk_space();
        }
    }
    return return_value;
}

/* TEL:  Added startdb flag.  If set (1), the MDB_dbienvironment will be started so
 * that dbmdb_db_remove_ex will be used to remove the database files instead
 * of simply deleting them.  That is important when doing a selective restoration
 * of a single backend (FRI).  If not set (0), the traditional remove is used.
 */
static int
_dbmdb_delete_instance_dir(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    dbmdb_ctx_t *conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    int rc = dbmdb_dbi_rmdir(conf, inst->inst_dir_name);
    return rc;
}

/* delete the db3 files in a specific backend instance --
 * this is probably only used for import.
 * assumption: dblayer is open, but the instance has been closed.
 */
int
dbmdb_delete_instance_dir(backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int ret = dbmdb_force_checkpoint(li);

    if (ret != 0) {
        return ret;
    } else {
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        return _dbmdb_delete_instance_dir(inst);
    }
}


/* delete an entire db/ directory, including all instances under it!
 * this is used mostly for restores.
 * dblayer is assumed to be closed.
 */
int
dbmdb_delete_db(struct ldbminfo *li)
{
    char path[MAXPATHLEN];

    PR_snprintf(path, MAXPATHLEN, "%s/data.mdb", MDB_CONFIG(li)->home);
    unlink(path);
    PR_snprintf(path, MAXPATHLEN, "%s/lock.mdb", MDB_CONFIG(li)->home);
    unlink(path);
    PR_snprintf(path, MAXPATHLEN, "%s/INFO.mdb", MDB_CONFIG(li)->home);
    unlink(path);

    return 0;
}


/*
 * Return the current size of the database (in bytes).
 */
uint64_t
dbmdb_database_size(struct ldbminfo *li)
{
    dbmdb_ctx_t *priv = NULL;
    PRFileInfo64 info = {0};
    char path[MAXPATHLEN];

    PR_ASSERT(NULL != li);
    priv = (dbmdb_ctx_t *)li->li_dblayer_config;
    PR_ASSERT(NULL != priv);
    PR_ASSERT(NULL != priv->home);
    PR_snprintf(path, MAXPATHLEN, "%s/data.mdb", priv->home);
    PR_GetFileInfo64(path, &info);    /* Ignores errors */
    return info.size;
}


/* And finally... Tubular Bells.
 * Well, no, actually backup and restore...
 */

/* Backup works like this:
 * the slapd executable is run like for ldif2ldbm and so on.
 * this means that the front-end gets the back-end loaded, and then calls
 * into the back-end backup entry point. This then gets us down to here.
 *
 * So, we need to copy the data files to the backup point.
 * While we are doing that, we need to make sure that the logfile
 * truncator in slapd doesn't delete our files. To do this we need
 * some way to signal to it that it should cease its work, or we need
 * to do something like start a long-lived transaction so that the
 * log files look like they're needed.
 *
 * When we've copied the data files, we can then copy the log files
 * too.
 *
 * Finally, we tell the log file truncator to go back about its business in peace
 *
 */

int
dbmdb_copyfile(char *source, char *destination, int overwrite __attribute__((unused)), int mode)
{
#ifdef DB_USE_64LFS
#define OPEN_FUNCTION dbmdb_open_large
#else
#define OPEN_FUNCTION open
#endif
    int source_fd = -1;
    int dest_fd = -1;
    char *buffer = NULL;
    int return_value = -1;
    int bytes_to_write = 0;

    /* malloc the buffer */
    buffer = slapi_ch_malloc(64 * 1024);
    if (NULL == buffer) {
        goto error;
    }
    /* Open source file */
    source_fd = OPEN_FUNCTION(source, O_RDONLY, 0);
    if (-1 == source_fd) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_copyfile", "Failed to open source file %s by \"%s\"\n",
                      source, strerror(errno));
        goto error;
    }
    /* Open destination file */
    dest_fd = OPEN_FUNCTION(destination, O_CREAT | O_WRONLY, mode);
    if (-1 == dest_fd) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_copyfile", "Failed to open dest file %s by \"%s\"\n",
                      destination, strerror(errno));
        goto error;
    }
    slapi_log_err(SLAPI_LOG_INFO,
                  "dbmdb_copyfile", "Copying %s to %s\n", source, destination);
    /* Loop round reading data and writing it */
    while (1) {
        int i;
        char *ptr = NULL;
        return_value = read(source_fd, buffer, 64 * 1024);
        if (return_value <= 0) {
            /* means error or EOF */
            if (return_value < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_copyfile", "Failed to read by \"%s\": rval = %d\n",
                              strerror(errno), return_value);
            }
            break;
        }
        bytes_to_write = return_value;
        ptr = buffer;
#define CPRETRY 4
        for (i = 0; i < CPRETRY; i++) { /* retry twice */
            return_value = write(dest_fd, ptr, bytes_to_write);
            if (return_value == bytes_to_write) {
                break;
            } else {
                /* means error */
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_copyfile", "Failed to write by \"%s\"; real: %d bytes, exp: %d bytes\n",
                              strerror(errno), return_value, bytes_to_write);
                if (return_value > 0) {
                    bytes_to_write -= return_value;
                    ptr += return_value;
                    slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_copyfile", "Retrying to write %d bytes\n", bytes_to_write);
                } else {
                    break;
                }
            }
        }
        if ((CPRETRY == i) || (return_value < 0)) {
            return_value = -1;
            break;
        }
    }
error:
    if (source_fd != -1) {
        close(source_fd);
    }
    if (dest_fd != -1) {
        close(dest_fd);
    }
    slapi_ch_free((void **)&buffer);
    return return_value;
}

/* Destination Directory is an absolute pathname */
int
dbmdb_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task)
{
#ifdef TODO
    dblayer_private *priv = NULL;
    dbmdb_ctx_t *conf = NULL;
    char **listA = NULL, **listB = NULL, **listi, **listj, *prefix;
    char *home_dir = NULL;
    char *db_dir = NULL;
    int return_value = -1;
    char *pathname1;
    char *pathname2;
    back_txn txn;
    int cnt = 1, ok = 0;
    Object *inst_obj;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    char *changelogdir = NULL;

    PR_ASSERT(NULL != li);
    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    db_dir = dbmdb_get_db_dir(li);

    home_dir = dbmdb_get_home_dir(li, NULL);
    if (NULL == home_dir || '\0' == *home_dir) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_backup", "Missing db home directory info\n");
        return return_value;
    }

    /*
     * What are we doing here ?
     * We want to copy into the backup directory:
     * All the backend instance dir / database files;
     * All the logfiles
     * The version file
     */

    /* changed in may 1999 for political correctness.
     * 1. take checkpoint
     * 2. open transaction
     * 3. get list of logfiles (A)
     * 4. copy the db# files
     * 5. get list of logfiles (B)
     * 6. if !(A in B), goto 3
     *    (logfiles were flushed during our backup)
     * 7. copy logfiles from list B
     * 8. abort transaction
     * 9. backup index config info
     */

    /* Order of checkpointing and txn creation reversed to work
     * around MDB_dbiproblem. If we don't do it this way around DB
     * thinks all old transaction logs are required for recovery
     * when the MDB_dbienvironment has been newly created (such as
     * after an import).
     */

    /* do a quick checkpoint */
    dbmdb_force_checkpoint(li);
    dblayer_txn_init(li, &txn);
    return_value = dblayer_txn_begin_all(li, NULL, &txn);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_backup", "Transaction error\n");
        return return_value;
    }

    if (g_get_shutdown() || c_get_shutdown()) {
        slapi_log_err(SLAPI_LOG_WARNING, "dblayer_backup", "Server shutting down, backup aborted\n");
        return_value = -1;
        goto bail;
    }

    /* repeat this until the logfile sets match... */
    do {
        /* get the list of logfiles currently existing */
        if (conf->dbmdb_enable_transactions) {
            return_value = LOG_ARCHIVE(((dbmdb_db_env *)priv->dblayer_env)->dbmdb_MDB_env,
                                       &listA, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listA == NULL)) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dblayer_backup", "Log archive error\n");
                if (task) {
                    slapi_task_log_notice(task, "Backup: log archive error\n");
                }
                return_value = -1;
                goto bail;
            }
        } else {
            ok = 1;
        }
        if (g_get_shutdown() || c_get_shutdown()) {
            slapi_log_err(SLAPI_LOG_ERR, "dblayer_backup", "Server shutting down, backup aborted\n");
            return_value = -1;
            goto bail;
        }

        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);
            inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                  inst_dir, MAXPATHLEN);
            if ((NULL == inst_dirp) || ('\0' == *inst_dirp)) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dblayer_backup", "Instance dir is empty\n");
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backup: Instance dir is empty\n");
                }
                if (inst_dirp != inst_dir) {
                    slapi_ch_free_string(&inst_dirp);
                }
                return_value = -1;
                goto bail;
            }
            return_value = dbmdb_copy_directory(li, task, inst_dirp,
                                                  dest_dir, 0 /* backup */,
                                                  &cnt, 0, 0);
            if (return_value) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dblayer_backup", "Error in copying directory "
                                                "(%s -> %s): err=%d\n",
                              inst_dirp, dest_dir, return_value);
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backup: error in copying directory "
                                          "(%s -> %s): err=%d\n",
                                          inst_dirp, dest_dir, return_value);
                }
                if (inst_dirp != inst_dir) {
                    slapi_ch_free_string(&inst_dirp);
                }
                goto bail;
            }
            if (inst_dirp != inst_dir)
                slapi_ch_free_string(&inst_dirp);
        }
        if (conf->dbmdb_enable_transactions) {
            /* now, get the list of logfiles that still exist */
            return_value = LOG_ARCHIVE(((dbmdb_db_env *)priv->dblayer_env)->dbmdb_MDB_env,
                                       &listB, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listB == NULL)) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dblayer_backup", "Can't get list of logs\n");
                goto bail;
            }

            /* compare: make sure everything in list A is still in list B */
            ok = 1;
            for (listi = listA; listi && *listi && ok; listi++) {
                int found = 0;
                for (listj = listB; listj && *listj && !found; listj++) {
                    if (strcmp(*listi, *listj) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    ok = 0; /* missing log: start over */
                    slapi_log_err(SLAPI_LOG_WARNING,
                                  "dblayer_backup", "Log %s has been swiped "
                                                    "out from under me! (retrying)\n",
                                  *listi);
                    if (task) {
                        slapi_task_log_notice(task,
                                              "WARNING: Log %s has been swiped out from under me! "
                                              "(retrying)",
                                              *listi);
                    }
                }
            }

            if (g_get_shutdown() || c_get_shutdown()) {
                slapi_log_err(SLAPI_LOG_ERR, "dblayer_backup", "Server shutting down, backup aborted\n");
                return_value = -1;
                goto bail;
            }

            if (ok) {
                size_t p1len, p2len;
                char **listptr;

                prefix = NULL;
                if ((NULL != conf->dbmdb_log_directory) &&
                    (0 != strlen(conf->dbmdb_log_directory))) {
                    prefix = conf->dbmdb_log_directory;
                } else {
                    prefix = db_dir;
                }
                /* log files have the same filename len(100 is a safety net:) */
                p1len = strlen(prefix) + strlen(*listB) + 100;
                pathname1 = (char *)slapi_ch_malloc(p1len);
                p2len = strlen(dest_dir) + strlen(*listB) + 100;
                pathname2 = (char *)slapi_ch_malloc(p2len);
                /* We copy those over */
                for (listptr = listB; listptr && *listptr && ok; ++listptr) {
                    PR_snprintf(pathname1, p1len, "%s/%s", prefix, *listptr);
                    PR_snprintf(pathname2, p2len, "%s/%s", dest_dir, *listptr);
                    slapi_log_err(SLAPI_LOG_INFO, "dblayer_backup", "Backing up file %d (%s)\n",
                                  cnt, pathname2);
                    if (task) {
                        slapi_task_log_notice(task,
                                              "Backing up file %d (%s)", cnt, pathname2);
                        slapi_task_log_status(task,
                                              "Backing up file %d (%s)", cnt, pathname2);
                    }
                    return_value = dbmdb_copyfile(pathname1, pathname2,
                                                    0, priv->dblayer_file_mode);
                    if (0 > return_value) {
                        slapi_log_err(SLAPI_LOG_ERR, "dblayer_backup", "Error in copying file '%s' (err=%d)\n",
                                      pathname1, return_value);
                        if (task) {
                            slapi_task_log_notice(task, "Error copying file '%s' (err=%d)",
                                                  pathname1, return_value);
                        }
                        slapi_ch_free((void **)&pathname1);
                        slapi_ch_free((void **)&pathname2);
                        goto bail;
                    }
                    if (g_get_shutdown() || c_get_shutdown()) {
                        slapi_log_err(SLAPI_LOG_ERR, "dblayer_backup", "Server shutting down, backup aborted\n");
                        return_value = -1;
                        slapi_ch_free((void **)&pathname1);
                        slapi_ch_free((void **)&pathname2);
                        goto bail;
                    }
                    cnt++;
                }
                slapi_ch_free((void **)&pathname1);
                slapi_ch_free((void **)&pathname2);
            }

            slapi_ch_free((void **)&listA);
            slapi_ch_free((void **)&listB);
        }
    } while (!ok);

    /* now copy the version file */
    pathname1 = slapi_ch_smprintf("%s/%s", home_dir, DBVERSION_FILENAME);
    pathname2 = slapi_ch_smprintf("%s/%s", dest_dir, DBVERSION_FILENAME);
    slapi_log_err(SLAPI_LOG_INFO, "dblayer_backup", "Backing up file %d (%s)\n", cnt, pathname2);
    if (task) {
        slapi_task_log_notice(task, "Backing up file %d (%s)", cnt, pathname2);
        slapi_task_log_status(task, "Backing up file %d (%s)", cnt, pathname2);
    }
    return_value = dbmdb_copyfile(pathname1, pathname2, 0, priv->dblayer_file_mode);
    if (0 > return_value) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_backup", "Error in copying version file "
                                        "(%s -> %s): err=%d\n",
                      pathname1, pathname2, return_value);
        if (task) {
            slapi_task_log_notice(task,
                                  "Backup: error in copying version file "
                                  "(%s -> %s): err=%d\n",
                                  pathname1, pathname2, return_value);
        }
    }
    slapi_ch_free((void **)&pathname1);
    slapi_ch_free((void **)&pathname2);

    /* Lastly we tell log file truncation to start again */

    if (0 == return_value) /* if everything went well, backup the index conf */
        return_value = dbmdb_dse_conf_backup(li, dest_dir);
bail:
    slapi_ch_free((void **)&listA);
    slapi_ch_free((void **)&listB);
    dblayer_txn_abort_all(li, &txn);
    slapi_ch_free_string(&changelogdir);
    return return_value;
#endif /* TODO */
}


/*
 * Restore is pretty easy.
 * We delete the current database.
 * We then copy all the files over from the backup point.
 * We then leave them there for the slapd process to pick up and do the recovery
 * (which it will do as it sees no guard file).
 */

/* Helper function first */

int
dbmdb_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task)
{
#ifdef TODO
    dbmdb_ctx_t *conf = NULL;
    dblayer_private *priv = NULL;
    int return_value = 0;
    int tmp_rval;
    char filename1[MAXPATHLEN];
    char filename2[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRFileInfo64 info;
    ldbm_instance *inst = NULL;
    int seen_logfiles = 0; /* Tells us if we restored any logfiles */
    int is_a_logfile = 0;
    int dbmode;
    int action = 0;
    char *home_dir = NULL;
    char *real_src_dir = NULL;
    struct stat sbuf;
    char *changelogdir = NULL;
    char *restore_dir = NULL;
    char *prefix = NULL;
    int cnt = 1;

    PR_ASSERT(NULL != li);
    conf = (dbmdb_ctx_t *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* DBMDB_dbithis is a hack, take out later */
    PR_Lock(li->li_config_mutex);
    /* dbmdb_home_directory is freed in dbmdb_post_close.
     * li_directory needs to live beyond dblayer. */
    slapi_ch_free_string(&conf->dbmdb_home_directory);
    conf->dbmdb_home_directory = slapi_ch_strdup(li->li_directory);
    conf->dbmdb_cachesize = li->li_dbcachesize;
    conf->dbmdb_lock_config = li->li_dblock;
    conf->dbmdb_ncache = li->li_dbncache;
    priv->dblayer_file_mode = li->li_mode;
    PR_Unlock(li->li_config_mutex);

    home_dir = dbmdb_get_home_dir(li, NULL);

    if (NULL == home_dir || '\0' == *home_dir) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore",
                      "Missing db home directory info\n");
        return -1;
    }

    /* We find out if slapd is startcfg */
    /* If it is, we fail */
    /* We check on the source staging area, no point in going further if it
     * isn't there */
    if (stat(src_dir, &sbuf) < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore", "Backup directory %s does not "
                                                        "exist.\n",
                      src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s does not exist.",
                                  src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    } else if (!S_ISDIR(sbuf.st_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore", "Backup directory %s is not "
                                                        "a directory.\n",
                      src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s is not a directory.",
                                  src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (!dbmdb_version_exists(li, src_dir)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore", "Backup directory %s does not "
                                                        "contain a complete backup\n",
                      src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s does not "
                                        "contain a complete backup",
                                  src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /*
     * Check if the target is a superset of the backup.
     * If not don't restore any db at all, otherwise
     * the target will be crippled.
     */
    dirhandle = PR_OpenDir(src_dir);
    if (NULL != dirhandle) {
        while ((direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)) && direntry->name) {
            PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                        src_dir, direntry->name);
            {
                tmp_rval = PR_GetFileInfo64(filename1, &info);
                if (tmp_rval == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) {
                    inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
                    if (inst == NULL) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_restore", "Target server has no backend (%s) configured\n",
                                      direntry->name);
                        if (task) {
                            slapi_task_log_notice(task,
                                                  "dbmdb_restore - Target server has no backend (%s) configured",
                                                  direntry->name);
                            slapi_task_cancel(task, LDAP_UNWILLING_TO_PERFORM);
                        }
                        PR_CloseDir(dirhandle);
                        return_value = LDAP_UNWILLING_TO_PERFORM;
                        goto error_out;
                    }

                    if (slapd_comp_path(src_dir, inst->inst_parent_dir_name) == 0) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_restore", "Backup dir %s and target dir %s are identical\n",
                                      src_dir, inst->inst_parent_dir_name);
                        if (task) {
                            slapi_task_log_notice(task,
                                                  "Restore: backup dir %s and target dir %s are identical",
                                                  src_dir, inst->inst_parent_dir_name);
                        }
                        PR_CloseDir(dirhandle);
                        return_value = LDAP_UNWILLING_TO_PERFORM;
                        goto error_out;
                    }
                }
            }
        }
        PR_CloseDir(dirhandle);
    }

    /* We delete the existing database */
    /* changelogdir is taken care only when it's not NULL. */
    return_value = dbmdb_delete_database_ex(li, changelogdir);
    if (return_value) {
        goto error_out;
    }

    {
        /* Otherwise use the src_dir from the caller */
        real_src_dir = src_dir;
    }

    /* We copy the files over from the staging area */
    /* We want to treat the logfiles specially: if there's
     * a log file directory configured, copy the logfiles there
     * rather than to the db dirctory */
    dirhandle = PR_OpenDir(real_src_dir);
    if (NULL == dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_restore", "Failed to open the directory \"%s\"\n", real_src_dir);
        if (task) {
            slapi_task_log_notice(task,
                                  "Restore: failed to open the directory \"%s\"", real_src_dir);
        }
        return_value = -1;
        goto error_out;
    }

    while (NULL !=
           (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name) {
            /* NSPR doesn't behave like the docs say it should */
            break;
        }

        /* Is this entry a directory? */
        PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                    real_src_dir, direntry->name);
        tmp_rval = PR_GetFileInfo64(filename1, &info);
        if (tmp_rval == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) {
            /* This is an instance directory. It contains the *.db#
             * files for the backend instance.
             * restore directory is supposed to be where the backend
             * directory is located.
             */
            if (0 == strcmp(CHANGELOG_BACKUPDIR, direntry->name)) {
                if (changelogdir) {
                    char *cldirname = PL_strrchr(changelogdir, '/');
                    char *p = filename1 + strlen(filename1);
                    if (NULL == cldirname) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_restore", "Broken changelog dir path %s\n",
                                      changelogdir);
                        if (task) {
                            slapi_task_log_notice(task,
                                                  "Restore: broken changelog dir path %s",
                                                  changelogdir);
                        }
                        goto error_out;
                    }
                    PR_snprintf(p, sizeof(filename1) - (p - filename1),
                                "/%s", cldirname + 1);
                    /* Get the parent dir of changelogdir */
                    *cldirname = '\0';
                    return_value = dbmdb_copy_directory(li, task, filename1,
                                                          changelogdir, 1 /* restore */,
                                                          &cnt, 0, 1);
                    *cldirname = '/';
                    if (return_value) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_restore", "Failed to copy directory %s\n",
                                      filename1);
                        if (task) {
                            slapi_task_log_notice(task,
                                                  "Restore: failed to copy directory %s",
                                                  filename1);
                        }
                        goto error_out;
                    }
                    /* Copy DBVERSION */
                    p = filename1 + strlen(filename1);
                    PR_snprintf(p, sizeof(filename1) - (p - filename1),
                                "/%s", DBVERSION_FILENAME);
                    PR_snprintf(filename2, sizeof(filename2), "%s/%s",
                                changelogdir, DBVERSION_FILENAME);
                    return_value = dbmdb_copyfile(filename1, filename2,
                                                    0, priv->dblayer_file_mode);
                    if (0 > return_value) {
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore", "Failed to copy file %s\n", filename1);
                        goto error_out;
                    }
                }
                continue;
            }

            inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
            if (inst == NULL)
                continue;

            restore_dir = inst->inst_parent_dir_name;
            /* If we're doing a partial restore, we need to reset the LSNs on the data files */
            if (dbmdb_copy_directory(li, task, filename1,
                                       restore_dir, 1 /* restore */, &cnt, 0, 0) == 0)
                continue;
            else {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_restore", "Failed to copy directory %s\n",
                              filename1);
                if (task) {
                    slapi_task_log_notice(task,
                                          "dbmdb_restore - Failed to copy directory %s", filename1);
                }
                goto error_out;
            }
        }

        if (dbmdb_doskip(direntry->name))
            continue;

        /* Is this a log file ? */
        /* Log files have names of the form "log.xxxxx" */
        /* We detect these by looking for the prefix "log." and
         * the lack of the ".db#" suffix */
        is_a_logfile = dbmdb_is_logfilename(direntry->name);
        if (is_a_logfile) {
            seen_logfiles = 1;
        }
        if (is_a_logfile && (NULL != BDB_CONFIG(li)->dbmdb_log_directory) &&
            (0 != strlen(BDB_CONFIG(li)->dbmdb_log_directory))) {
            prefix = BDB_CONFIG(li)->dbmdb_log_directory;
        } else {
            prefix = home_dir;
        }
        mkdir_p(prefix, 0700);
        PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                    real_src_dir, direntry->name);
        PR_snprintf(filename2, sizeof(filename2), "%s/%s",
                    prefix, direntry->name);
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_restore", "Restoring file %d (%s)\n",
                      cnt, filename2);
        if (task) {
            slapi_task_log_notice(task, "Restoring file %d (%s)",
                                  cnt, filename2);
            slapi_task_log_status(task, "Restoring file %d (%s)",
                                  cnt, filename2);
        }
        return_value = dbmdb_copyfile(filename1, filename2, 0,
                                        priv->dblayer_file_mode);
        if (0 > return_value) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_restore", "Failed to copy file %s\n", filename1);
            goto error_out;
        }
        cnt++;
    }
    PR_CloseDir(dirhandle);

    /* We're done ! */

    /* [605024] check the DBVERSION and reset idl-switch if needed */
    if (dbmdb_version_exists(li, home_dir)) {
        char *ldbmversion = NULL;
        char *dataversion = NULL;

        if (dbmdb_version_read(li, home_dir, &ldbmversion, &dataversion) != 0) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_restore", "Unable to read dbversion file in %s\n",
                          home_dir);
        } else {
            dbmdb_adjust_idl_switch(ldbmversion, li);
            slapi_ch_free_string(&ldbmversion);
            slapi_ch_free_string(&dataversion);
        }
    }

    return_value = dbmdb_check_db_version(li, &action);
    if (action &
        (DBVERSION_UPGRADE_3_4 | DBVERSION_UPGRADE_4_4 | DBVERSION_UPGRADE_4_5)) {
        dbmode = DBLAYER_CLEAN_RECOVER_MODE; /* upgrade: remove logs & recover */
    } else if (seen_logfiles) {
        dbmode = DBLAYER_RESTORE_MODE;
    } else if (action & DBVERSION_NEED_DN2RDN) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_restore", "%s is on, while the instance %s is in the DN format. "
                                         "Please run dn2rdn to convert the database format.\n",
                      CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    } else if (action & DBVERSION_NEED_RDN2DN) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_restore", "%s is off, while the instance %s is in the RDN format. "
                                         "Please change the value to on in dse.ldif.\n",
                      CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    } else {
        dbmode = DBLAYER_RESTORE_NO_RECOVERY_MODE;
    }

    /* now start the database code up, to prevent recovery next time the
     * server starts;
     * dbmdb_dse_conf_verify may need to have db started, as well. */
    /* If no logfiles were stored, then fatal recovery isn't required */

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* command line mode; no need to run db threads */
        dbmode |= DBLAYER_NO_MDB_valHREADS_MODE;
    } else /* on-line mode */
    {
        allinstance_set_not_busy(li);
    }

    tmp_rval = dbmdb_start(li, dbmode);
    if (0 != tmp_rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_restore", "Failed to init database\n");
        if (task) {
            slapi_task_log_notice(task, "dbmdb_restore - Failed to init database");
        }
        return_value = tmp_rval;
        goto error_out;
    }

    if (0 == return_value) { /* only when the copyfile succeeded */
        /* check the DSE_* files, if any */
        tmp_rval = dbmdb_dse_conf_verify(li, real_src_dir);
        if (0 != tmp_rval)
            slapi_log_err(SLAPI_LOG_WARNING,
                          "dbmdb_restore", "Unable to verify the index configuration\n");
    }

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* command line: close the database down again */
        tmp_rval = dblayer_close(li, dbmode);
        if (0 != tmp_rval) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_restore", "Failed to close database\n");
        }
    } else {
        allinstance_set_busy(li); /* on-line mode */
    }

    return_value = tmp_rval ? tmp_rval : return_value;

error_out:
    /* Free the restore src dir, but only if we allocated it above */
    if (real_src_dir && (real_src_dir != src_dir)) {
        /* If this was an FRI restore and the staging area exists, go ahead and remove it */
        slapi_ch_free_string(&real_src_dir);
    }
    slapi_ch_free_string(&changelogdir);
    return return_value;
#endif /* TODO */
}

static char *
dbmdb__import_file_name(ldbm_instance *inst)
{
    char *fname = slapi_ch_smprintf("%s/.import_%s",
                                    inst->inst_parent_dir_name,
                                    inst->inst_dir_name);
    return fname;
}

static char *
dbmdb_restore_file_name(struct ldbminfo *li)
{
    char *fname = slapi_ch_smprintf("%s/../.restore", li->li_directory);

    return fname;
}

static int
dbmdb_file_open(char *fname, int flags, int mode, PRFileDesc **prfd)
{
    int rc = 0;
    *prfd = PR_Open(fname, flags, mode);

    if (NULL == *prfd)
        rc = PR_GetError();
    if (rc && rc != PR_FILE_NOT_FOUND_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_file_open", "Failed to open file: %s, error: (%d) %s\n",
                      fname, rc, slapd_pr_strerror(rc));
    }
    return rc;
}

int
dbmdb_import_file_init(ldbm_instance *inst)
{
    int rc = -1;
    PRFileDesc *prfd = NULL;
    char *fname = dbmdb__import_file_name(inst);
    rc = dbmdb_file_open(fname, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, inst->inst_li->li_mode, &prfd);
    if (prfd) {
        PR_Close(prfd);
        rc = 0;
    }
    slapi_ch_free_string(&fname);
    return rc;
}

int
dbmdb_restore_file_init(struct ldbminfo *li)
{
    int rc = -1;
    PRFileDesc *prfd;
    char *fname = dbmdb_restore_file_name(li);
    rc = dbmdb_file_open(fname, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, li->li_mode, &prfd);
    if (prfd) {
        PR_Close(prfd);
        rc = 0;
    }
    slapi_ch_free_string(&fname);
    return rc;
}

void
dbmdb_import_file_update(ldbm_instance *inst)
{
    PRFileDesc *prfd;
    char *fname = dbmdb__import_file_name(inst);
    dbmdb_file_open(fname, PR_RDWR, inst->inst_li->li_mode, &prfd);

    if (prfd) {
        char *line = slapi_ch_smprintf("import of %s succeeded", inst->inst_dir_name);
        slapi_write_buffer(prfd, line, strlen(line));
        slapi_ch_free_string(&line);
        PR_Close(prfd);
    }
    slapi_ch_free_string(&fname);
}

int
dbmdb_file_check(char *fname, int mode)
{
    int rc = 0;
    int err;
    PRFileDesc *prfd;
    err = dbmdb_file_open(fname, PR_RDWR, mode, &prfd);

    if (prfd) {
        /* file exists, additional check on size */
        PRFileInfo64 prfinfo;
        rc = 1;
        /* read it */
        err = PR_GetOpenFileInfo64(prfd, &prfinfo);
        if (err == PR_SUCCESS && 0 == prfinfo.size) {
            /* it is empty restore or import has failed */
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_file_check", "Previous import or restore failed, file: %s is empty\n", fname);
        }
        PR_Close(prfd);
        PR_Delete(fname);
    } else {
        if (PR_FILE_NOT_FOUND_ERROR == err) {
            rc = 0;
        } else {
            /* file exists, but we cannot open it */
            rc = 1;
            /* error is already looged try to delete it*/
            PR_Delete(fname);
        }
    }

    return rc;
}

int
dbmdb_import_file_check(ldbm_instance *inst)
{
    int rc;
    char *fname = dbmdb__import_file_name(inst);
    rc = dbmdb_file_check(fname, inst->inst_li->li_mode);
    slapi_ch_free_string(&fname);
    return rc;
}

static int
dbmdb_restore_file_check(struct ldbminfo *li)
{
    int rc;
    char *fname = dbmdb_restore_file_name(li);
    rc = dbmdb_file_check(fname, li->li_mode);
    slapi_ch_free_string(&fname);
    return rc;
}

void

dbmdb_restore_file_update(struct ldbminfo *li, const char *directory)
{
    PRFileDesc *prfd;
    char *fname = dbmdb_restore_file_name(li);
    dbmdb_file_open(fname, PR_RDWR, li->li_mode, &prfd);
    slapi_ch_free_string(&fname);
    if (prfd) {
        char *line = slapi_ch_smprintf("restore of %s succeeded", directory);
        slapi_write_buffer(prfd, line, strlen(line));
        slapi_ch_free_string(&line);
        PR_Close(prfd);
    }
}

/*
 * delete the index files belonging to the instance
 */
int
dbmdb_delete_indices(ldbm_instance *inst)
{
    int rval = -1;
    struct attrinfo *a = NULL;
    int i;

    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_delete_indices", "NULL instance is passed\n");
        return rval;
    }
    rval = 0;
    for (a = (struct attrinfo *)avl_getfirst(inst->inst_attrs), i = 0;
         NULL != a;
         a = (struct attrinfo *)avl_getnext(), i++) {
        rval += dbmdb_rm_db_file(inst->inst_be, a, PR_TRUE, i /* chkpt; 1st time only */);
    }
    return rval;
}

void
dbmdb_set_recovery_required(struct ldbminfo *li)
{
    /* No recovery with lmdb */
}

int
dbmdb_get_info(Slapi_Backend *be, int cmd, void **info)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dbmdb_ctx_t *ctx;
    int rc = -1;

    if ( !info || !li) {
        return rc;
    }
    ctx = MDB_CONFIG(li);

    switch (cmd) {
    case BACK_INFO_DBENV:
        *(MDB_env **)info = ctx->env;
        rc = 0;
        break;
    case BACK_INFO_DBENV_OPENFLAGS:
        *(int *)info = ctx->readonly ? MDB_RDONLY : 0;
        rc = 0;
        break;
    case BACK_INFO_DB_PAGESIZE:
    case BACK_INFO_INDEXPAGESIZE:
        *(uint32_t *)info = ctx->info.pagesize;
        rc = 0;
        break;
    case BACK_INFO_DIRECTORY:
        if (li) {
            *(char **)info = li->li_directory;
            rc = 0;
        }
        break;
    case BACK_INFO_DBHOME_DIRECTORY:
    case BACK_INFO_DB_DIRECTORY:
        *(char **)info = ctx->home;
        rc = 0;
        break;
    case BACK_INFO_INSTANCE_DIR:
        if (li) {
            ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
            *(char **)info = dblayer_get_full_inst_dir(li, inst, NULL, 0);
            rc = 0;
        }
        break;
    case BACK_INFO_LOG_DIRECTORY: {
        if (li) {
            *(char **)info = NULL;
            rc = 0;
        }
        break;
    }
    case BACK_INFO_IS_ENTRYRDN: {
        *(int *)info = entryrdn_get_switch();
        break;
    }
    case BACK_INFO_INDEX_KEY : {
        rc = get_suffix_key(be, (struct _back_info_index_key *)info);
        break;
    }
    case BACK_INFO_DBENV_CLDB: {
        ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
        if (inst->inst_changelog) {
            rc = 0;
        } else {
            dbmdb_dbi_t *db;
            rc = dblayer_get_changelog(be, (dbi_db_t **)&db, MDB_CREATE);
        }
        if (rc == 0) {
            *(dbmdb_dbi_t **)info = inst->inst_changelog;
        } else {
            *(dbmdb_dbi_t **)info = NULL;
        }
        break;
    }
    case BACK_INFO_CLDB_FILENAME: {
        *(char **)info = BDB_CL_FILENAME;
        rc = 0;
        break;
    }
    default:
        break;
    }

    return rc;
}

int
dbmdb_set_info(Slapi_Backend *be, int cmd, void **info)
{
    int rc = -1;

    switch (cmd) {
    case BACK_INFO_INDEX_KEY : {
        rc = set_suffix_key(be, (struct _back_info_index_key *)info);
        break;
    }
    default:
        break;
    }

    return rc;
}

int
dbmdb_back_ctrl(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    switch (cmd) {
    case BACK_INFO_CRYPT_INIT: {
        back_info_crypt_init *crypt_init = (back_info_crypt_init *)info;
        Slapi_DN configdn;
        slapi_sdn_init(&configdn);
        be_getbasedn(be, &configdn);
        char *crypt_dn = slapi_ch_smprintf("%s,%s",
                        crypt_init->dn,
                        slapi_sdn_get_dn(&configdn));
        rc = back_crypt_init(crypt_init->be, crypt_dn,
                             crypt_init->encryptionAlgorithm,
                             &(crypt_init->state_priv));
        break;
    }
    case BACK_INFO_CRYPT_DESTROY: {
        back_info_crypt_destroy *crypt_init = (back_info_crypt_destroy *)info;
        rc = back_crypt_destroy(crypt_init->state_priv);
        break;
    }
    case BACK_INFO_CRYPT_ENCRYPT_VALUE: {
        back_info_crypt_value *crypt_value = (back_info_crypt_value *)info;
        rc = back_crypt_encrypt_value(crypt_value->state_priv, crypt_value->in,
                                      &(crypt_value->out));
        break;
    }
    case BACK_INFO_CRYPT_DECRYPT_VALUE: {
        back_info_crypt_value *crypt_value = (back_info_crypt_value *)info;
        rc = back_crypt_decrypt_value(crypt_value->state_priv, crypt_value->in,
                                      &(crypt_value->out));
        break;
    }
    case BACK_INFO_DBENV_CLDB_REMOVE: {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;

        ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
        if (li) {
            dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
            if (priv && priv->dblayer_env) {
                char *instancedir;
                dbmdb_dbi_t dbi = {0};
                dbi_db_t *ptdbi = (dbi_db_t*) &dbi;

                slapi_back_get_info(be, BACK_INFO_INSTANCE_DIR, (void **)&instancedir);
                rc = dbmdb_open_dbi_from_filename(&dbi, be, BDB_CL_FILENAME, NULL, 0);
                if (rc == MDB_NOTFOUND) {
                    /* Nothing to do */
                    rc = 0;
                } else if (rc == 0) {
                    rc = dbmdb_dbi_remove(MDB_CONFIG(li), &ptdbi);
                }
                inst->inst_changelog = NULL;
                slapi_ch_free_string(&instancedir);
            }
        }
        break;
    }
    case BACK_INFO_DBENV_CLDB_UPGRADE: {
        break;
    }
    case BACK_INFO_CLDB_GET_CONFIG: {
        /* get a config entry relative to the
         * backend config entry
         * Caller must free the returned entry (config->ce)
         * If it fails config->ce is left unchanged
         */
        back_info_config_entry *config = (back_info_config_entry *)info;
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        Slapi_DN configdn;
        slapi_sdn_init(&configdn);
        be_getbasedn(be, &configdn);
        char *config_dn = slapi_ch_smprintf("%s,%s",
                        config->dn,
                        slapi_sdn_get_dn(&configdn));
        Slapi_PBlock *search_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(search_pb, config_dn, LDAP_SCOPE_BASE, "objectclass=*",
                                     NULL, 0, NULL, NULL, li->li_identity, 0);
        slapi_search_internal_pb(search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (LDAP_SUCCESS == rc ) {
            Slapi_Entry **entries;
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if (entries && entries[0]) {
                config->ce = slapi_entry_dup(entries[0]);
            } else {
                rc = -1;
            }
        }
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
        slapi_ch_free_string(&config_dn);
        break;
    }
    case BACK_INFO_CLDB_SET_CONFIG: {
        /* This control option allows a plugin to set a backend configuration
         * entry without knowing the location of the backend config.
         * It passes an entry with a relative dn and this dn is expanded by the
         * backend config dn.
         */
        Slapi_DN fulldn;
        Slapi_DN configdn;
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        Slapi_Entry *config_entry = (Slapi_Entry *)info;

        slapi_sdn_init(&configdn);
        be_getbasedn(be, &configdn);
        char *newdn = slapi_ch_smprintf("%s,%s",
                        slapi_entry_get_dn_const(config_entry),
                        slapi_sdn_get_dn(&configdn));
        slapi_sdn_init(&fulldn);
        slapi_sdn_init_dn_byref(&fulldn, newdn);
        slapi_entry_set_sdn(config_entry, &fulldn);
        slapi_ch_free_string(&newdn);

        Slapi_PBlock *pb = slapi_pblock_new();
        slapi_pblock_init(pb);
        slapi_add_entry_internal_set_pb(pb, config_entry, NULL,
                                        li->li_identity, 0);
        slapi_add_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        slapi_pblock_destroy(pb);
        break;
    }
    default:
        break;
    }

    return rc;
}

dbi_error_t dbmdb_map_error(const char *funcname, int err)
{
    char *msg = NULL;

    switch (err) {
        case 0:
            return DBI_RC_SUCCESS;
        case MDB_KEYEXIST:
            return DBI_RC_KEYEXIST;
        case MDB_NOTFOUND:
            return DBI_RC_NOTFOUND;
        default:
            msg = mdb_strerror(err);
            if (!msg) {
                msg = "";
            }
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_map_error",
                "%s failed with db error %d : %s\n", funcname, err, msg);
            return DBI_RC_OTHER;
    }
}

/* Conversion a dbi_val_t* into a MDB_val* */
void dbmdb_dbival2dbt(dbi_val_t *dbi, MDB_val *dbt, PRBool isresponse)
{
/*
 * isresponse is true means that dbmdb_dbt2dbival(dbt, dbi, PR_FALSE)
 *  is called a few lines before dbmdb_dbival2dbt call
 * This means that if data pointer differs then the buffer has been
 * re alloced ==> should beware not to free it twice
 */
    if (!dbi || !dbt) {
        return;
    }
    dbt->mv_data = dbi->data;
    dbt->mv_size = dbi->size;
}

/* Conversion a MDB_val* into a dbi_val_t* */
int dbmdb_dbt2dbival(MDB_val *dbt, dbi_val_t *dbi, PRBool isresponse, int rc)
{
/*
 * isresponse is true means that dbmdb_dbival2dbt(dbt, dbi, PR_FALSE)
 *  is called a few lines before dbmdb_dbt2dbival call
 * This means that if data pointer differs then the buffer has been
 * re alloced ==> should beware not to free it twice
 */
    if (!dbi || !dbt || rc) {
        return rc;
    }
    if (dbi->data == dbt->mv_data) {
        /* Value has not changed ==> only update the size */
        dbi->size = dbt->mv_size;
        return rc;
    }

    if (dbi->flags & DBI_VF_READONLY) {
        /* trying to modify read only data */
        return DBI_RC_INVALID;
    }
    if (!isresponse) {
        dbi->flags = DBI_VF_READONLY;
        dbi->data = dbt->mv_data;
        dbi->size = dbt->mv_size;
        return rc;
    }
    if (dbt->mv_size > dbi->ulen) {
        if (dbi->flags & (DBI_VF_PROTECTED|DBI_VF_DONTGROW)) {
            return DBI_RC_BUFFER_SMALL;
        }
        /* Lets realloc the buffer */
        dbi->ulen = dbi->size = dbt->mv_size;
        dbi->data = slapi_ch_realloc(dbi->data, dbi->size);
    }
    memcpy(dbi->data, dbt->mv_data, dbi->size);
    return rc;
}

/**********************/
/* dbimpl.c callbacks */
/**********************/

char *dbmdb_public_get_db_filename(dbi_db_t *db)
{
    dbmdb_dbi_t *dbmdb_db = (dbmdb_dbi_t*)db;
    /* We may perhaps have to remove the backend name ... */
    return (char*)(dbmdb_db->dbname);
}

int dbmdb_public_bulk_free(dbi_bulk_t *bulkdata)
{
    /* No specific action required for mdb handling */
    return DBI_RC_SUCCESS;
}

int dbmdb_public_bulk_nextdata(dbi_bulk_t *bulkdata, dbi_val_t *data)
{
    dbmdb_bulkdata_t *dbmdb_data = bulkdata->v.data;
    int *idx = (int*) (&bulkdata->it);
    char *v = dbmdb_data->data.mv_data;
    int rc = 0;

    PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_DATA);
    if (dbmdb_data->use_multiple) {
        PR_ASSERT(dbmdb_data->data_size);
        if (*idx >= dbmdb_data->data.mv_size / dbmdb_data->data_size) {
            return DBI_RC_NOTFOUND;
        }
        v += dbmdb_data->data_size * (*idx)++;
        dblayer_value_set_buffer(bulkdata->be, data, v, dbmdb_data->data_size);
    } else {
        if (!dbmdb_data->op || (*idx)++ >= dbmdb_data->maxrecords) {
            return DBI_RC_NOTFOUND;
        }
        dblayer_value_set_buffer(bulkdata->be, data, v, dbmdb_data->data.mv_size);
        rc = mdb_cursor_get(dbmdb_data->cursor, &dbmdb_data->key, &dbmdb_data->data, dbmdb_data->op);
    }
    rc = dbmdb_map_error(__FUNCTION__, rc);
    return rc;
}

int dbmdb_public_bulk_nextrecord(dbi_bulk_t *bulkdata, dbi_val_t *key, dbi_val_t *data)
{
    dbmdb_bulkdata_t *dbmdb_data = bulkdata->v.data;
    int *idx = (int*) (&bulkdata->it);
    char *v = dbmdb_data->data.mv_data;
    int rc = 0;

    PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_RECORD);
    PR_ASSERT(!dbmdb_data->use_multiple);
    if (!dbmdb_data->op || *idx++ >= dbmdb_data->maxrecords) {
        return DBI_RC_NOTFOUND;
    }
    dblayer_value_set_buffer(bulkdata->be, data, v, dbmdb_data->data.mv_size);
    dblayer_value_set_buffer(bulkdata->be, key, dbmdb_data->key.mv_data, dbmdb_data->key.mv_size);
    rc = mdb_cursor_get(dbmdb_data->cursor, &dbmdb_data->key, &dbmdb_data->data, dbmdb_data->op);
    rc = dbmdb_map_error(__FUNCTION__, rc);
    return rc;
}

int dbmdb_public_bulk_init(dbi_bulk_t *bulkdata)
{
    /* No specific action required for mdb handling */
    return DBI_RC_SUCCESS;
}

int dbmdb_public_bulk_start(dbi_bulk_t *bulkdata)
{
    bulkdata->it = (void*) 0;
    return DBI_RC_SUCCESS;
}

int dbmdb_public_cursor_bulkop(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_bulk_t *bulkdata)
{
    dbmdb_bulkdata_t *dbmdb_data = bulkdata->v.data;
    MDB_cursor *dbmdb_cur = (MDB_cursor*)cursor->cur;
    MDB_val *mval;
    int rc = 0;

    if (!(cursor && cursor->cur))
        return DBI_RC_INVALID;

    bulkdata->v.size = sizeof *dbmdb_data;
    dbmdb_data->cursor = (MDB_cursor*)cursor->cur;
    dbmdb_dbival2dbt(key, &dbmdb_data->key, PR_FALSE);
    mdb_dbi_flags(mdb_cursor_txn(dbmdb_cur), mdb_cursor_dbi(dbmdb_cur), &dbmdb_data->dbi_flags);
    dbmdb_data->use_multiple = (bulkdata->v.flags & DBI_VF_BULK_RECORD) || (dbmdb_data->dbi_flags & MDB_DUPFIXED);
    PR_ASSERT(dbmdb_data->dbi_flags & MDB_DUPSORT);
    dbmdb_data->maxrecords = BULKOP_MAX_RECORDS;
    dbmdb_data->data.mv_data = NULL;
    dbmdb_data->data.mv_size = 0;
    dbmdb_data->op = 0;
    mval = &dbmdb_data->data;

    /* if dbmdb_data->use_multiple:
     *  WARNING lmdb documentation about GET_MULTIPLE is wrong:
     *   The data is not a MBD_val[2] array as documented but a single MDB_val and the size is the size of all
     *   returned items.
     * else:
     *   retrieve the first item in bulkdata->key and bulkdata->data and prepare to retieve next item in
     *   dbmdb_public_bulk_nextrecord or dbmdb_public_bulk_nextdata
     */
    switch (op)
    {
        case DBI_OP_MOVE_TO_FIRST:
                /* Returns dups of first entries */
            rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key, mval, MDB_FIRST);
            if (rc == 0) {
                dbmdb_data->op =  (bulkdata->v.flags & DBI_VF_BULK_RECORD) ? MDB_NEXT : MDB_NEXT_DUP;
                if (dbmdb_data->use_multiple) {
                    dbmdb_data->data_size = mval->mv_size;
                    memset(mval, 0, sizeof dbmdb_data->data);
                    rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key,  mval, MDB_GET_MULTIPLE);
                }
            }
            break;
        case DBI_OP_MOVE_TO_KEY:
                /* Move customer to the specified key and returns dups */
            rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key, mval, MDB_SET);
            if (rc == 0) {
                dbmdb_data->op =  (bulkdata->v.flags & DBI_VF_BULK_RECORD) ? MDB_NEXT : MDB_NEXT_DUP;
                if (dbmdb_data->use_multiple) {
                    dbmdb_data->data_size = mval->mv_size;
                    memset(mval, 0, sizeof dbmdb_data->data);
                    rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key,  mval, MDB_GET_MULTIPLE);
                }
            }
            break;
        case DBI_OP_NEXT_KEY:
            if (dbmdb_data->use_multiple) {
                memset(mval, 0, sizeof dbmdb_data->data);
                rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key,  mval, MDB_NEXT_MULTIPLE);
            } else {
                rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key, mval, MDB_NEXT_NODUP);
                if (rc == 0) {
                    dbmdb_data->op =  (bulkdata->v.flags & DBI_VF_BULK_RECORD) ? MDB_NEXT : MDB_NEXT_DUP;
                }
            }
            break;
        case DBI_OP_NEXT:
                /* Move cursor to next position and returns dups and/or nodups */
            PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_RECORD);
            PR_ASSERT(!dbmdb_data->use_multiple);
            rc = mdb_cursor_get(dbmdb_data->cursor,  &dbmdb_data->key, mval, MDB_NEXT);
            if (rc == 0) {
                dbmdb_data->op = MDB_NEXT;
            }
            break;
        case DBI_OP_NEXT_DATA:
                /* Return next blocks of dups */
            /* with lmdb all dups are returned by the multiple operation
             * so there is no need to iterate on next dups
             */
            if (!dbmdb_data->use_multiple && mval->mv_data) {
                /* There are still some data to work on */
                dbmdb_data->op = MDB_NEXT_DUP;
                rc = 0;
            } else {
                /* When usign multiple there is always a single bloc of dups */
                rc = MDB_NOTFOUND;
            }
            break;
        default:
            /* Unknown bulk operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    rc = dbmdb_map_error(__FUNCTION__, rc);
    rc = dbmdb_dbt2dbival(&dbmdb_data->key, key, PR_TRUE, rc);
    return rc;
}

int dbmdb_public_cursor_op(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    MDB_cursor *dbmdb_cur = (MDB_cursor*)cursor->cur;
    MDB_val dbmdb_key = {0};
    MDB_val dbmdb_data = {0};
    int rc = 0;

    if (dbmdb_cur == NULL) {
        return (op == DBI_OP_CLOSE) ? DBI_RC_SUCCESS : DBI_RC_INVALID;
    }

    dbmdb_dbival2dbt(key, &dbmdb_key, PR_FALSE);
    dbmdb_dbival2dbt(data, &dbmdb_data, PR_FALSE);
    switch (op)
    {
        case DBI_OP_MOVE_TO_KEY:
            rc = mdb_cursor_get(dbmdb_cur,  &dbmdb_key, &dbmdb_data, MDB_SET);
            break;
        case DBI_OP_MOVE_NEAR_KEY:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_SET_RANGE);
            break;
        case DBI_OP_MOVE_TO_DATA:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_GET_BOTH);
            break;
        case DBI_OP_MOVE_NEAR_DATA:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_GET_BOTH_RANGE);
            break;
        case DBI_OP_MOVE_TO_RECNO:
/* TODO */ PR_ASSERT(0);
/*            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, DB_SET_RECNO); */
            break;
        case DBI_OP_MOVE_TO_FIRST:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_FIRST);
            break;
        case DBI_OP_MOVE_TO_LAST:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_LAST);
            break;
        case DBI_OP_GET:
            /* not a dbmdb_cur operation (db operation) */
            PR_ASSERT(op != DBI_OP_GET);
            rc = DBI_RC_UNSUPPORTED;
            break;
        case DBI_OP_GET_RECNO:
/* TODO */ PR_ASSERT(0);
/*            rc = dbmdb_cur->c_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, DB_GET_RECNO); */
            break;
        case DBI_OP_NEXT:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_NEXT);
            break;
        case DBI_OP_NEXT_DATA:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_NEXT_DUP);
            break;
        case DBI_OP_NEXT_KEY:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_NEXT_NODUP);
            break;
        case DBI_OP_PREV:
            rc = mdb_cursor_get(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_PREV);
            break;
        case DBI_OP_PUT:
            /* not a dbmdb_cur operation (db operation) */
            PR_ASSERT(op != DBI_OP_PUT);
            rc = DBI_RC_UNSUPPORTED;
            break;
        case DBI_OP_REPLACE:
            rc = mdb_cursor_put(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_CURRENT);
            break;
        case DBI_OP_ADD:
            rc = mdb_cursor_put(dbmdb_cur, &dbmdb_key, &dbmdb_data, MDB_NODUPDATA);
            break;
        case DBI_OP_DEL:
            rc = mdb_cursor_del(dbmdb_cur, 0);
            break;
        case DBI_OP_CLOSE:
            mdb_cursor_close(dbmdb_cur);
            if (cursor->islocaltxn) {
                /* local txn is read only and should be aborted when closing the cursor */
                END_TXN(&cursor->txn, 1);
            }
            break;
        default:
            /* Unknown operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    rc = dbmdb_map_error(__FUNCTION__, rc);
    rc = dbmdb_dbt2dbival(&dbmdb_key, key, PR_TRUE, rc);
    rc = dbmdb_dbt2dbival(&dbmdb_data, data, PR_TRUE, rc);
    return rc;
}

int dbmdb_public_db_op(dbi_db_t *db,  dbi_txn_t *txn, dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    MDB_val dbmdb_key = {0};
    MDB_val dbmdb_data = {0};
    MDB_txn *mdb_txn = TXN(txn);
    dbmdb_dbi_t *dbmdb_db = (dbmdb_dbi_t*)db;
    MDB_dbi dbi = dbmdb_db->dbi;
    dbi_txn_t *ltxn = NULL;
    int rc = 0;

    dbmdb_dbival2dbt(key, &dbmdb_key, PR_FALSE);
    dbmdb_dbival2dbt(data, &dbmdb_data, PR_FALSE);
    if (!txn) {
        rc = START_TXN(&ltxn, NULL, ((op == DBI_OP_GET) ? TXNFL_RDONLY : 0));
        mdb_txn = TXN(ltxn);
    }
    switch (op)
    {
        case DBI_OP_GET:
            rc = mdb_get(mdb_txn, dbi, &dbmdb_key, &dbmdb_data);
            break;
        case DBI_OP_PUT:
if (strstr(dbmdb_db->dbname, "parent")) { slapi_log_err(SLAPI_LOG_INFO, __FUNCTION__, "[%d] parentid --> %s\n", __LINE__, (char*)(dbmdb_key.mv_data)); }
            rc = mdb_put(mdb_txn, dbi, &dbmdb_key, &dbmdb_data, 0);
            break;
        case DBI_OP_ADD:
            rc = mdb_put(mdb_txn, dbi, &dbmdb_key, &dbmdb_data, MDB_NODUPDATA);
            break;
        case DBI_OP_DEL:
            rc = mdb_del(mdb_txn, dbi, &dbmdb_key, &dbmdb_data);
            break;
        case DBI_OP_CLOSE:
            /* No need to close db instances with lmdb */
            /* But we should still free the dbmdb_dbi_t struct */
            slapi_ch_free_string((char**)&dbmdb_db->dbname);
            dbmdb_db->dbi = 0;
            dbmdb_db->env = NULL;
            slapi_ch_free((void **) &dbmdb_db);
            break;
        default:
            /* Unknown db operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    if (ltxn) {
        rc = END_TXN(&ltxn, rc);
    }
    rc = dbmdb_map_error(__FUNCTION__, rc);
    rc = dbmdb_dbt2dbival(&dbmdb_key, key, PR_TRUE, rc);
    rc = dbmdb_dbt2dbival(&dbmdb_data, data, PR_TRUE, rc);
    return rc;
}

int dbmdb_public_new_cursor(dbi_db_t *db,  dbi_cursor_t *cursor)
{
    dbmdb_dbi_t *dbi = (dbmdb_dbi_t*) db;
    int rc = 0;

    cursor->islocaltxn = PR_FALSE;
    if (!cursor->txn) {
        /* No txn is provided so it is a read only cursor
         * Let checks if a txn has been pushed on thread
         *   use it if that is the case
         *   otherwise begin a new local txn
         */
        rc = START_TXN(&cursor->txn, NULL, TXNFL_RDONLY);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_new_cursor",
                "Failed to get a local txn while opening a cursor on db %s . rc=%d %s\n",
                 dbi->dbname, rc, mdb_strerror(rc));
             return dbmdb_map_error(__FUNCTION__, rc);
        }
        cursor->islocaltxn = PR_TRUE;
    }
    rc = mdb_cursor_open(TXN(cursor->txn), dbi->dbi, (MDB_cursor**)&cursor->cur);
    if (rc && cursor->islocaltxn)
        END_TXN(&cursor->txn, rc);
    return dbmdb_map_error(__FUNCTION__, rc);
}

int dbmdb_public_value_free(dbi_val_t *data)
{
    /* No specific action required for mdb db handling */
    return DBI_RC_SUCCESS;
}

int dbmdb_public_value_init(dbi_val_t *data)
{
    /* No specific action required for mdb db handling */
    return DBI_RC_SUCCESS;
}

int
dbmdb_public_set_dup_cmp_fn(struct attrinfo *a, dbi_dup_cmp_t idx)
{
    /*
     * Do nothing here - dbmdb_entryrdn_compare_dups is now set
     * at dbmdb_open_dbname level (so it get also set for dbscan)
     */
    return 0;
}

int
dbmdb_dbi_txn_begin(dbi_env_t *env, PRBool readonly, dbi_txn_t *parent_txn, dbi_txn_t **txn)
{
    int rc = START_TXN(txn, parent_txn, (readonly?TXNFL_RDONLY:0));
    return dbmdb_map_error(__FUNCTION__, rc);
}

int
dbmdb_dbi_txn_commit(dbi_txn_t *txn)
{
    int rc = END_TXN(&txn, 0);
    return dbmdb_map_error(__FUNCTION__, rc);
}

int
dbmdb_dbi_txn_abort(dbi_txn_t *txn)
{
    END_TXN(&txn, 1);
    return 0;
}

int
dbmdb_get_entries_count(dbi_db_t *db, int *count)
{
    dbmdb_dbi_t *dbmdb_db = (dbmdb_dbi_t*)db;
    dbi_txn_t *txn = NULL;
    MDB_stat stats = {0};
    int rc = 0;

    rc = START_TXN(&txn, NULL, MDB_RDONLY);
    if (rc == 0)
        rc = mdb_stat(TXN(txn), dbmdb_db->dbi, &stats);
    if (rc == 0)
        *count = stats.ms_entries;
    END_TXN(&txn, 1);
    return dbmdb_map_error(__FUNCTION__, rc);
}

/* Get the number of duplicates for current key */
int
dbmdb_public_cursor_get_count(dbi_cursor_t *cursor, dbi_recno_t *count)
{
    size_t c = 0;
    MDB_cursor *cur = cursor->cur;
    int rc = mdb_cursor_count(cur, &c);
    *count = c;
    return dbmdb_map_error(__FUNCTION__, rc);
}

int find_mdb_home(const char *db_filename, char *home, const char **dbname)
{
    struct stat st;
    const char *pt2;
    char *pt;

    strncpy(home, db_filename, MAXPATHLEN);
    for(;;) {
        pt = home + strlen(home);
        if (pt+10 >= &home[MAXPATHLEN])
            return DBI_RC_NOTFOUND;
        strcpy(pt, "/data.mdb");
        if (stat(home, &st) == 0) {
            /* Found dbhome */
            *pt = 0;
            break;
        }
        /* Try again with upper directory */
        *pt = 0;
        pt = strrchr(home, '/');
        if (!pt)
            return DBI_RC_NOTFOUND;
        *pt = 0;
    }
    pt2 = db_filename+(pt-home);
    while (*pt2 == '/')
        pt2++;
    *dbname = pt2;
    return *pt2 ? 0 : DBI_RC_NOTFOUND;
}

int
dbmdb_public_private_open(backend *be, const char *db_filename, dbi_env_t **env, dbi_db_t **db)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dbmdb_ctx_t *ctx = (dbmdb_ctx_t*) slapi_ch_calloc(1, sizeof *ctx);
    dbmdb_dbi_t dbi = {0};
    const char *dbname = NULL;

    li->li_dblayer_config = ctx;
    int rc = find_mdb_home(db_filename, ctx->home, &dbname);
    if (rc)
        return DBI_RC_NOTFOUND;

    rc = dbmdb_make_env(ctx, 1, 0444);
    if (rc) {
        return dbmdb_map_error(__FUNCTION__, rc);
    }
    *env = ctx->env;

    rc = dbmdb_open_dbi_from_filename(&dbi, be, dbname, NULL, MDB_OPEN_DIRTY_DBI);
    if (rc) {
        return dbmdb_map_error(__FUNCTION__, rc);
    }
    dbmdb_mdbdbi2dbi_db(&dbi, db);

    return 0;
}


int
dbmdb_public_private_close(dbi_env_t **env, dbi_db_t **db)
{
    if (*db)
        dbmdb_public_db_op(*db, NULL, DBI_OP_CLOSE, NULL, NULL);
    *db = NULL;
    if (*env)
        mdb_env_close((MDB_env*)*env);
    *env = NULL;
    return 0;
}

static int dbmdb_force_checkpoint(struct ldbminfo *li)
{
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    int rc = mdb_env_sync(ctx->env, 1);
    return dbmdb_map_error(__FUNCTION__, rc);
}

/* check whether import is executed (or aborted) by other process or not */
int
dbmdb_public_in_import(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    int rval = 0;
    int len;
    int i;

    for (i=0; i<ctx->nbdbis; i++) {
        len = strlen(ctx->dbis[i].dbname);
        if (strncmp(li->li_directory, ctx->dbis[i].dbname, len) == 0 &&
            ctx->dbis[i].dbname[len] == '/') {
            continue;
        }
        if (ctx->dbis[i].state.state & DBIST_DIRTY) {
            rval = 1;
            break;
        }
    }
    return rval;
}
