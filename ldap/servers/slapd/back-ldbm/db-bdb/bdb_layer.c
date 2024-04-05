/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#include <sys/types.h>
#include <sys/statvfs.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "bdb_layer.h"
#include <prthread.h>
#include <prclist.h>
#include <glob.h>


#define DB_OPEN(oflags, db, txnid, file, database, type, flags, mode, rval)                                     \
    {                                                                                                           \
        if (((oflags)&DB_INIT_TXN) && ((oflags)&DB_INIT_LOG)) {                                                 \
            (rval) = ((db)->open)((db), (txnid), (file), (database), (type), (flags) | DB_AUTO_COMMIT, (mode)); \
        } else {                                                                                                \
            (rval) = ((db)->open)((db), (txnid), (file), (database), (type), (flags), (mode));                  \
        }                                                                                                       \
    }

#define TXN_BEGIN(env, parent_txn, tid, flags) \
    ((DB_ENV*)(env))->txn_begin((env), (parent_txn), (DB_TXN **)(tid), (flags))
#define TXN_COMMIT(txn, flags) ((DB_TXN*)(txn))->commit((txn), (flags))
#define TXN_ABORT(txn) ((DB_TXN*)(txn))->abort(txn)
#define TXN_CHECKPOINT(env, kbyte, min, flags) \
    (env)->txn_checkpoint((env), (kbyte), (min), (flags))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
    (env)->memp_stat((env), (gsp), (fsp), (flags))
#define LOCK_STAT(env, statp, flags, malloc) \
    (env)->lock_stat((env), (statp), (flags))
#define MEMP_TRICKLE(env, pct, nwrotep) \
    (env)->memp_trickle((env), (pct), (nwrotep))
#define LOG_ARCHIVE(env, listp, flags, malloc) \
    (env)->log_archive((env), (listp), (flags))
#define LOG_FLUSH(env, lsn) (env)->log_flush((env), (lsn))

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
    pthread_mutex_lock(&pEnv->bdb_thread_count_lock); \
    ++pEnv->bdb_thread_count;     \
    pthread_mutex_unlock(&pEnv->bdb_thread_count_lock)

#define DECR_THREAD_COUNT(pEnv)                  \
    pthread_mutex_lock(&pEnv->bdb_thread_count_lock);            \
    if (--pEnv->bdb_thread_count == 0) {     \
        pthread_cond_broadcast(&pEnv->bdb_thread_count_cv); \
    }                                            \
    pthread_mutex_unlock(&pEnv->bdb_thread_count_lock)

#define NEWDIR_MODE 0755
#define DB_REGION_PREFIX "__db."

/* Context used when walking database files */
typedef struct
{
    dbi_dbslist_t *list;
    size_t maxdbs;               /* Number of files */
    size_t nbdbs;                /* Number of files */
} dbi_dbslist_ctx_t;

static int bdb_perf_threadmain(void *param);
static int bdb_checkpoint_threadmain(void *param);
static int bdb_trickle_threadmain(void *param);
static int bdb_deadlock_threadmain(void *param);
static int bdb_commit_good_database(bdb_config *priv, int mode);
static int bdb_read_metadata(struct ldbminfo *li);
static int bdb_count_dbfiles_in_dir(char *directory, int *count, int recurse);
static int bdb_override_libdb_functions(void);
static int bdb_locks_monitoring_threadmain(void *param);
static int bdb_force_checkpoint(struct ldbminfo *li);
static int bdb_force_logrenewal(struct ldbminfo *li);
static int bdb_log_flush_threadmain(void *param);
static int bdb_delete_transaction_logs(const char *log_dir);
static int bdb_is_logfilename(const char *path);
static int bdb_start_log_flush_thread(struct ldbminfo *li);
static int bdb_start_deadlock_thread(struct ldbminfo *li);
static int bdb_start_checkpoint_thread(struct ldbminfo *li);
static int bdb_start_trickle_thread(struct ldbminfo *li);
static int bdb_start_perf_thread(struct ldbminfo *li);
static int bdb_start_txn_test_thread(struct ldbminfo *li);
static int bdb_start_locks_monitoring_thread(struct ldbminfo *li);
static int trans_batch_count = 0;
static int trans_batch_limit = 0;
static int trans_batch_txn_min_sleep = 50; /* ms */
static int trans_batch_txn_max_sleep = 50;
static PRBool log_flush_thread = PR_FALSE;
static int txn_in_progress_count = 0;
static int *txn_log_flush_pending = NULL;
static PRBool compaction_scheduled = PR_FALSE;

static pthread_mutex_t sync_txn_log_flush;
static pthread_cond_t sync_txn_log_flush_done;
static pthread_cond_t sync_txn_log_do_flush;


static int bdb_db_remove_ex(bdb_db_env *env, char const path[], char const dbName[], PRBool use_lock);
static int bdb_db_compact_one_db(DB *db, ldbm_instance *inst);
static int bdb_restore_file_check(struct ldbminfo *li);

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

/* This function compares two index keys.  It is assumed
   that the values are already normalized, since they should have
   been when the index was created (by int_values2keys).

   richm - actually, the current syntax compare functions
   always normalize both arguments.  We need to add an additional
   syntax compare function that does not normalize or takes
   an argument like value_cmp to specify to normalize or not.

   More fun - this function is used to compare both raw database
   keys (e.g. with the prefix '=' or '+' or '*' etc.) and without
   (in the case of two equality keys, we want to strip off the
   leading '=' to compare the actual values).  We only use the
   value_compare function if both keys are equality keys with
   some data after the equality prefix.  In every other case,
   we will just use a standard berval cmp function.

   see also DBTcmp
*/

int
bdb_bt_compare(DB *db, const DBT *dbt1, const DBT *dbt2)
{
    struct berval bv1, bv2;
    value_compare_fn_type syntax_cmp_fn = (value_compare_fn_type)db->app_private;

    if ((dbt1->data && (dbt1->size > 1) && (*((char *)dbt1->data) == EQ_PREFIX)) &&
        (dbt2->data && (dbt2->size > 1) && (*((char *)dbt2->data) == EQ_PREFIX))) {
        bv1.bv_val = (char *)dbt1->data + 1; /* remove leading '=' */
        bv1.bv_len = (ber_len_t)dbt1->size - 1;

        bv2.bv_val = (char *)dbt2->data + 1; /* remove leading '=' */
        bv2.bv_len = (ber_len_t)dbt2->size - 1;

        return syntax_cmp_fn(&bv1, &bv2);
    }

    /* else compare two "raw" index keys */
    bv1.bv_val = (char *)dbt1->data;
    bv1.bv_len = (ber_len_t)dbt1->size;

    bv2.bv_val = (char *)dbt2->data;
    bv2.bv_len = (ber_len_t)dbt2->size;

    return slapi_berval_cmp(&bv1, &bv2);
}


/* this flag is used if user remotely turned batching off */
#define FLUSH_REMOTEOFF 0

/* routine that allows batch value to be changed remotely:

    1. value = 0 turns batching off
    2. value = 1 makes behavior be like 5.0 but leaves batching on
    3. value > 1 changes batch value

    2 and 3 assume that nsslapd-db-transaction-batch-val is greater 0 at startup
*/

int
bdb_set_batch_transactions(void *arg __attribute__((unused)), void *value, char *errorbuf __attribute__((unused)), int phase, int apply)
{
    int val = (int)((uintptr_t)value);
    int retval = LDAP_SUCCESS;

    if (apply) {
        if (phase == CONFIG_PHASE_STARTUP) {
            trans_batch_limit = val;
        } else {
            if (val == 0) {
                if (log_flush_thread) {
                    pthread_mutex_lock(&sync_txn_log_flush);
                }
                trans_batch_limit = FLUSH_REMOTEOFF;
                if (log_flush_thread) {
                    log_flush_thread = PR_FALSE;
                    pthread_mutex_unlock(&sync_txn_log_flush);
                }
            } else if (val > 0) {
                if (trans_batch_limit == FLUSH_REMOTEOFF) {
                    /* this requires a server restart to take effect */
                    slapi_log_err(SLAPI_LOG_NOTICE, "dblayer_set_batch_transactions", "Enabling batch transactions "
                                                                                      "requires a server restart.\n");
                } else if (!log_flush_thread) {
                    /* we are already disabled, log a reminder of that fact. */
                    slapi_log_err(SLAPI_LOG_NOTICE, "dblayer_set_batch_transactions", "Batch transactions was "
                                                                                      "previously disabled, this update requires a server restart.\n");
                }
                trans_batch_limit = val;
            }
        }
    }
    return retval;
}

int
bdb_set_batch_txn_min_sleep(void *arg __attribute__((unused)), void *value, char *errorbuf __attribute__((unused)), int phase, int apply)
{
    int val = (int)((uintptr_t)value);
    int retval = LDAP_SUCCESS;

    if (apply) {
        if (phase == CONFIG_PHASE_STARTUP || phase == CONFIG_PHASE_INITIALIZATION) {
            trans_batch_txn_min_sleep = val;
        } else {
            if (val == 0) {
                if (log_flush_thread) {
                    pthread_mutex_lock(&sync_txn_log_flush);
                }
                trans_batch_txn_min_sleep = FLUSH_REMOTEOFF;
                if (log_flush_thread) {
                    log_flush_thread = PR_FALSE;
                    pthread_mutex_unlock(&sync_txn_log_flush);
                }
            } else if (val > 0) {
                if (trans_batch_txn_min_sleep == FLUSH_REMOTEOFF || !log_flush_thread) {
                    /* this really has no effect until batch transactions are enabled */
                    slapi_log_err(SLAPI_LOG_WARNING, "dblayer_set_batch_txn_min_sleep", "Warning batch transactions "
                                                                                        "is not enabled.\n");
                }
                trans_batch_txn_min_sleep = val;
            }
        }
    }
    return retval;
}

int
bdb_set_batch_txn_max_sleep(void *arg __attribute__((unused)), void *value, char *errorbuf __attribute__((unused)), int phase, int apply)
{
    int val = (int)((uintptr_t)value);
    int retval = LDAP_SUCCESS;

    if (apply) {
        if (phase == CONFIG_PHASE_STARTUP || phase == CONFIG_PHASE_INITIALIZATION) {
            trans_batch_txn_max_sleep = val;
        } else {
            if (val == 0) {
                if (log_flush_thread) {
                    pthread_mutex_lock(&sync_txn_log_flush);
                }
                trans_batch_txn_max_sleep = FLUSH_REMOTEOFF;
                if (log_flush_thread) {
                    log_flush_thread = PR_FALSE;
                    pthread_mutex_unlock(&sync_txn_log_flush);
                }
            } else if (val > 0) {
                if (trans_batch_txn_max_sleep == FLUSH_REMOTEOFF || !log_flush_thread) {
                    /* this really has no effect until batch transactions are enabled */
                    slapi_log_err(SLAPI_LOG_WARNING,
                                  "dblayer_set_batch_txn_max_sleep", "Warning batch transactions "
                                                                     "is not enabled.\n");
                }
                trans_batch_txn_max_sleep = val;
            }
        }
    }
    return retval;
}

void *
bdb_get_batch_transactions(void *arg __attribute__((unused)))
{
    return (void *)((uintptr_t)trans_batch_limit);
}

void *
bdb_get_batch_txn_min_sleep(void *arg __attribute__((unused)))
{
    return (void *)((uintptr_t)trans_batch_txn_min_sleep);
}

void *
bdb_get_batch_txn_max_sleep(void *arg __attribute__((unused)))
{
    return (void *)((uintptr_t)trans_batch_txn_max_sleep);
}

/*
    Threading: dblayer isolates upper layers from threading considerations
    Everything in dblayer is free-threaded. That is, you can have multiple
    threads performing operations on a database and not worry about things.
    Obviously, if you do something stupid, like move a cursor forward in
    one thread, and backwards in another at the same time, you get what you
    deserve. However, such a calling pattern will not crash your application !
*/

static int
bdb_txn_checkpoint(struct ldbminfo *li, bdb_db_env *env, PRBool busy_skip, PRBool db_force)
{
    int ret = 0;
    if (busy_skip && is_anyinstance_busy(li)) {
        return ret;
    }
    ret = TXN_CHECKPOINT(env->bdb_DB_ENV, db_force ? DB_FORCE : 0, 0, 0);
    return ret;
}


/*
 * return nsslapd-db-home-directory (bdb_dbhome_directory), if exists.
 * Otherwise, return nsslapd-directory (bdb_home_directory).
 *
 * if bdb_dbhome_directory exists, set 1 to dbhome.
 */
char *
bdb_get_home_dir(struct ldbminfo *li, int *dbhome)
{
    bdb_config *priv = (bdb_config *)li->li_dblayer_config;
    char *home_dir = li->li_directory;
    if (dbhome)
        *dbhome = 0;

    if (priv->bdb_dbhome_directory && *(priv->bdb_dbhome_directory)) {
        if (dbhome)
            *dbhome = 1;
        home_dir = priv->bdb_dbhome_directory;
    }
    if (NULL == home_dir) {
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_get_home_dir", "Db home directory is not set. "
                                                                 "Possibly %s (optionally %s) is missing in the config file.\n",
                      CONFIG_DIRECTORY, CONFIG_DB_HOME_DIRECTORY);
    }
    return home_dir;
}

/*
 * return the top db directory
 */
char *
bdb_get_db_dir(struct ldbminfo *li)
{
    return li->li_directory;
}

/* Helper function which deletes the persistent state of the database library
 * IMHO this should be in inside libdb, but keith won't have it.
 * Stop press---libdb now does delete these files on recovery, so we don't call this any more.
 */
static void
bdb_reset_env(struct ldbminfo *li)
{
    /* Remove the memory regions */
    dblayer_private *priv = li->li_dblayer_private;
    DB_ENV *pEnv = ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV;
    char *home_dir = bdb_get_home_dir(li, NULL);
    if (home_dir && *home_dir)
        pEnv->remove(pEnv, home_dir, DB_FORCE);
}

/* Function which calls libdb to override some system calls which
 * the library makes. We call this before calling any other function
 * in libdb.
 * Several OS use this, either partially or completely.
 * This will eventually change---we will simply pass to libdb
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
bdb_open_large(const char *path, int oflag, mode_t mode)
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
bdb_open_huge_file(const char *path, int oflag, int mode)
{
    return bdb_open_large(path, oflag, (mode_t)mode);
}

/* Helper function for large seeks, db4.3 */
static int
bdb_seek43_large(int fd, off64_t offset, int whence)
{
    off64_t ret = 0;

    ret = lseek64(fd, offset, whence);

    return (ret < 0) ? errno : 0;
}

/* helper function for large fstat -- this depends on 'struct stat64' having
 * the following members:
 *    off64_t        st_size;
 *      long        st_blksize;
 */
static int
bdb_ioinfo_large(const char *path __attribute__((unused)), int fd, u_int32_t *mbytesp, u_int32_t *bytesp, u_int32_t *iosizep)
{
    struct stat64 sb;

    if (fstat64(fd, &sb) < 0)
        return (errno);

    /* Return the size of the file. */
    if (mbytesp)
        *mbytesp = (u_int32_t)(sb.st_size / (off64_t)MEGABYTE);
    if (bytesp)
        *bytesp = (u_int32_t)(sb.st_size % (off64_t)MEGABYTE);

    if (iosizep)
        *iosizep = (u_int32_t)(sb.st_blksize);
    return 0;
}
/* Helper function to tell if a file exists */
/* On Solaris, if you use stat() on a file >4Gbytes, it fails with EOVERFLOW,
   causing us to think that the file does not exist when it in fact does */
static int
bdb_exists_large(const char *path, int *isdirp)
{
    struct stat64 sb;

    if (stat64(path, &sb) != 0)
        return (errno);

    if (isdirp != NULL)
        *isdirp = S_ISDIR(sb.st_mode);

    return (0);
}

#else /* DB_USE_64LFS */

int
bdb_open_huge_file(const char *path, int oflag, int mode)
{
    return open(path, oflag, mode);
}

#endif /* DB_USE_64LFS */


static int
bdb_override_libdb_functions(void)
{
#ifdef DB_USE_64LFS
    int major = 0;
    int minor = 0;

    /* Find out whether we are talking to a 2.3 or 2.4+ libdb */
    db_version(&major, &minor, NULL);

#ifndef irix
    /* irix doesn't have open64() */
    db_env_set_func_open((int (*)(const char *, int, ...))bdb_open_large);
#endif /* !irix */
    db_env_set_func_ioinfo(bdb_ioinfo_large);
    db_env_set_func_exists(bdb_exists_large);
    db_env_set_func_seek((int (*)(int, off_t, int))bdb_seek43_large);

    slapi_log_err(SLAPI_LOG_TRACE, "dblayer_override_libdb_function", "Enabled 64-bit files\n");
#endif /* DB_USE_64LFS */
    return 0;
}

static void
bdb_select_ncache(size_t cachesize, int *ncachep)
{
    /* First thing, if the user asked to use a particular ncache,
     * we let them, and don't override it here.
     */
    if (*ncachep) {
        return;
    }
/* If the user asked for a cache that's larger than 4G,
     * we _must_ select an ncache >0 , such that each
     * chunk is <4G. This is because DB won't accept a
     * larger chunk.
     */
#if defined(__LP64__) || defined(_LP64)
    if ((sizeof(cachesize) > 4) && (cachesize > (4L * GIGABYTE))) {
        *ncachep = (cachesize / (4L * GIGABYTE)) + 1;
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_select_ncache", "Setting ncache to: %d to keep each chunk below 4Gbytes\n",
                      *ncachep);
    }
#endif
}

void
bdb_free(void *ptr)
{
    slapi_ch_free(&ptr);
}

static void
bdb_init_dbenv(DB_ENV *pEnv, bdb_config *conf, dblayer_private *priv)
{
    size_t mysize;
    int myncache = 1;

    mysize = conf->bdb_cachesize;
    myncache = conf->bdb_ncache;
    bdb_select_ncache(mysize, &myncache);
    conf->bdb_ncache = myncache;

    bdb_set_env_debugging(pEnv, conf);

    pEnv->set_lg_max(pEnv, conf->bdb_logfile_size);
    pEnv->set_cachesize(pEnv, mysize / GIGABYTE, mysize % GIGABYTE, myncache);
    pEnv->set_lk_max_locks(pEnv, conf->bdb_lock_config);
    pEnv->set_lk_max_objects(pEnv, conf->bdb_lock_config);
    pEnv->set_lk_max_lockers(pEnv, conf->bdb_lock_config);

    /* shm_key required for named_regions (DB_SYSTEM_MEM) */
    pEnv->set_shm_key(pEnv, conf->bdb_shm_key);

    /* increase max number of active transactions */
    pEnv->set_tx_max(pEnv, conf->bdb_tx_max);

    pEnv->set_alloc(pEnv, (void *)slapi_ch_malloc, (void *)slapi_ch_realloc, bdb_free);

    /*
     * The log region is used to store filenames and so needs to be
     * increased in size from the default for a large number of files.
     */
    pEnv->set_lg_regionmax(pEnv, 1 * 1048576); /* 1 MB */
}


static void
bdb_dump_config_tracing(struct ldbminfo *li)
{
    bdb_config *conf =(bdb_config *)li->li_dblayer_config;
    dblayer_private *priv = li->li_dblayer_private;
    if (conf->bdb_home_directory) {
        slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "home_directory=%s\n", conf->bdb_home_directory);
    }
    if (conf->bdb_log_directory) {
        slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "log_directory=%s\n", conf->bdb_log_directory);
    }
    if (conf->bdb_dbhome_directory) {
        slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "dbhome_directory=%s\n", conf->bdb_dbhome_directory);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "trickle_percentage=%d\n", conf->bdb_trickle_percentage);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "page_size=%" PRIu32 "\n", conf->bdb_page_size);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "index_page_size=%" PRIu32 "\n", conf->bdb_index_page_size);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "cachesize=%" PRIu64 "\n", conf->bdb_cachesize);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "previous_cachesize=%" PRIu64 "\n", conf->bdb_previous_cachesize);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "ncache=%d\n", conf->bdb_ncache);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "previous_ncache=%d\n", conf->bdb_previous_ncache);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "recovery_required=%d\n", conf->bdb_recovery_required);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "durable_transactions=%d\n", conf->bdb_durable_transactions);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "checkpoint_interval=%d\n", conf->bdb_checkpoint_interval);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "transaction_batch_val=%d\n", trans_batch_limit);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "circular_logging=%d\n", conf->bdb_circular_logging);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "idl_divisor=%d\n", priv->dblayer_idl_divisor);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "logfile_size=%" PRIu64 "\n", conf->bdb_logfile_size);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "logbuf_size=%" PRIu64 "\n", conf->bdb_logbuf_size);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "file_mode=%d\n", priv->dblayer_file_mode);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "cache_config=%d\n", conf->bdb_cache_config);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "lib_version=%d\n", conf->bdb_lib_version);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "spin_count=%d\n", conf->bdb_spin_count);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "named_regions=%d\n", conf->bdb_named_regions);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "private mem=%d\n", conf->bdb_private_mem);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "private import mem=%d\n", conf->bdb_private_import_mem);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "shm_key=%ld\n", conf->bdb_shm_key);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "lockdown=%d\n", conf->bdb_lockdown);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "locks=%d\n", conf->bdb_lock_config);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "previous_locks=%d\n", conf->bdb_previous_lock_config);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_dump_config_tracing", "tx_max=%d\n", conf->bdb_tx_max);
}

/* Check a given filesystem directory for access we need */
#define DBLAYER_DIRECTORY_READ_ACCESS 1
#define DBLAYER_DIRECTORY_WRITE_ACCESS 2
#define DBLAYER_DIRECTORY_READWRITE_ACCESS 3
static int
bdb_grok_directory(char *directory, int flags)
{
    /* First try to open the directory using NSPR */
    /* If that fails, we can tell whether it's because it cannot be created or
     * we don't have any permission to access it */
    /* If that works, proceed to try to access files in the directory */
    char filename[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRFileInfo64 info;

    dirhandle = PR_OpenDir(directory);
    if (NULL == dirhandle) {
        /* it does not exist or wrong file is there */
        /* try delete and mkdir */
        PR_Delete(directory);
        return mkdir_p(directory, 0700);
    }

    while (NULL !=
           (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name) {
            break;
        }
        PR_snprintf(filename, MAXPATHLEN, "%s/%s", directory, direntry->name);

        /* Right now this is set up to only look at files here.
         * With multiple instances of the backend the are now other directories
         * in the db home directory.  This function wasn't ment to deal with
         * other directories, so we skip them. */
        if (PR_GetFileInfo64(filename, &info) == PR_SUCCESS &&
            info.type == PR_FILE_DIRECTORY) {
            /* go into it (instance dir) */
            int retval = bdb_grok_directory(filename, flags);
            PR_CloseDir(dirhandle);
            return retval;
        }

        /* If we are here, it means that the directory exists, that we can read
         * from it, and that there is at least one file there */
        /* We will try to open that file now if we were asked for read access */
        if (flags) {
            PRFileDesc *prfd;
            PRIntn open_flags = 0;
            char *access_string = NULL;

            if (DBLAYER_DIRECTORY_READ_ACCESS & flags) {
                open_flags = PR_RDONLY;
            }
            if (DBLAYER_DIRECTORY_WRITE_ACCESS & flags) {
                open_flags = PR_RDWR;
            }
            /* Let's hope that on Solaris we get to open large files OK */
            prfd = PR_Open(filename, open_flags, 0);
            if (NULL == prfd) {
                if (DBLAYER_DIRECTORY_READ_ACCESS == flags) {
                    access_string = "read";
                } else {
                    if (DBLAYER_DIRECTORY_READ_ACCESS & flags) {
                        access_string = "write";
                    } else {
                        access_string = "****";
                    }
                }
                /* If we're here, it means that we did not have the requested
                 * permission on this file */
                slapi_log_err(SLAPI_LOG_WARNING,
                              "bdb_grok_directory", "No %s permission to file %s\n",
                              access_string, filename);
            } else {
                PR_Close(prfd); /* okay */
            }
        }
    }
    PR_CloseDir(dirhandle);
    return 0;
}

static void
bdb_set_data_dir(bdb_db_env *pEnv, char **data_directories)
{
    char **dirp;

    if (!(pEnv->bdb_priv_flags & DBLAYER_PRIV_SET_DATA_DIR)) {
        for (dirp = data_directories; dirp && *dirp; dirp++) {
            pEnv->bdb_DB_ENV->set_data_dir(pEnv->bdb_DB_ENV, *dirp);
        }
        pEnv->bdb_priv_flags |= DBLAYER_PRIV_SET_DATA_DIR;
    }
}

static int
bdb_inst_exists(ldbm_instance *inst, char *dbname)
{
    PRStatus prst;
    char id2entry_file[MAXPATHLEN];
    char *parent_dir = inst->inst_parent_dir_name;
    char sep = get_sep(parent_dir);
    char *dbnamep;
    if (dbname)
        dbnamep = dbname;
    else
        dbnamep = ID2ENTRY LDBM_FILENAME_SUFFIX;
    PR_snprintf(id2entry_file, sizeof(id2entry_file), "%s%c%s%c%s", parent_dir, sep, inst->inst_dir_name,
                sep, dbnamep);
    prst = PR_Access(id2entry_file, PR_ACCESS_EXISTS);
    if (PR_SUCCESS == prst)
        return 1;
    return 0;
}

static void
bdb_free_env(void **arg)
{
    bdb_db_env **env = (bdb_db_env **)arg;
    if (NULL == env || NULL == *env) {
        return;
    }
    if ((*env)->bdb_env_lock) {
        slapi_destroy_rwlock((*env)->bdb_env_lock);
        (*env)->bdb_env_lock = NULL;
    }
    pthread_mutex_destroy(&((*env)->bdb_thread_count_lock));
    pthread_cond_destroy(&((*env)->bdb_thread_count_cv));

    slapi_ch_free((void **)env);
    return;
}

/*
 * create a new DB_ENV and fill it with the goodies from dblayer_private
 */
static int
bdb_make_env(bdb_db_env **env, struct ldbminfo *li)
{
    bdb_config *conf = (bdb_config *)li->li_dblayer_config;
    bdb_db_env *pEnv;
    char *db_dir = NULL;
    char *log_dir = NULL;
    int ret;
    Object *inst_obj;
    ldbm_instance *inst = NULL;
    pthread_condattr_t condAttr;

    pEnv = (bdb_db_env *)slapi_ch_calloc(1, sizeof(bdb_db_env));

    pthread_mutex_init(&pEnv->bdb_thread_count_lock, NULL);
    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_cond_init(&pEnv->bdb_thread_count_cv, &condAttr);
    pthread_condattr_destroy(&condAttr); /* no longer needed */

    if ((ret = db_env_create(&pEnv->bdb_DB_ENV, 0)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_make_env", "Failed to create DB_ENV (returned: %d).\n",
                      ret);
    }

    pEnv->bdb_DB_ENV->set_flags(pEnv->bdb_DB_ENV, DB_REGION_INIT, 1);

    /* Here we overide various system functions called by libdb */
    ret = bdb_override_libdb_functions();
    if (ret != 0) {
        goto fail;
    }

    if (conf->bdb_spin_count != 0) {
        pEnv->bdb_DB_ENV->mutex_set_tas_spins(pEnv->bdb_DB_ENV,
                                                  conf->bdb_spin_count);
    }

    bdb_dump_config_tracing(li);

    /* set data dir to avoid having absolute paths in the transaction log */
    for (inst_obj = objset_first_obj(li->li_instance_set);
         inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (inst->inst_parent_dir_name) {
            if (!charray_utf8_inlist(conf->bdb_data_directories,
                                     inst->inst_parent_dir_name)) {
                charray_add(&(conf->bdb_data_directories),
                            slapi_ch_strdup(inst->inst_parent_dir_name));
            }
        }
    }
    /* also set the main db directory as potential parent  */
    db_dir = bdb_get_db_dir(li);
    if (db_dir && *db_dir &&
        !charray_utf8_inlist(conf->bdb_data_directories, db_dir)) {
        charray_add(&(conf->bdb_data_directories), slapi_ch_strdup(db_dir));
    }

    /* user specified log dir */
    log_dir = (char *)bdb_config_db_logdirectory_get_ext(li);
    if (log_dir && *log_dir) {
        pEnv->bdb_DB_ENV->set_lg_dir(pEnv->bdb_DB_ENV,log_dir);
    }

    /* set up cache sizes */
    bdb_init_dbenv(pEnv->bdb_DB_ENV, conf, li->li_dblayer_private);

    pEnv->bdb_env_lock = slapi_new_rwlock();

    if (pEnv->bdb_env_lock) {
        *env = pEnv;
        pEnv = NULL; /* do not free below */
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_make_env", "Failed to create RWLock (returned: %d).\n",
                      ret);
    }

fail:
    if (pEnv) {
        slapi_ch_array_free(conf->bdb_data_directories);
        conf->bdb_data_directories = NULL;
        if (pEnv->bdb_DB_ENV) {
            pEnv->bdb_DB_ENV->close(pEnv->bdb_DB_ENV, 0);
        }
        bdb_free_env((void **)&pEnv); /* pEnv is now garbage */
    }
    return ret;
}

/*
 *  Get the total size of all the __db files
 */
static PRUint64
bdb_get_region_size(const char *dir)
{
    PRFileInfo64 info;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRUint64 region_size = 0;

    dirhandle = PR_OpenDir(dir);
    if (NULL == dirhandle) {
        return region_size;
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name) {
            continue;
        }
        if (0 == strncmp(direntry->name, DB_REGION_PREFIX, 5)) {
            char filename[MAXPATHLEN];

            PR_snprintf(filename, MAXPATHLEN, "%s/%s", dir, direntry->name);
            if (PR_GetFileInfo64(filename, &info) != PR_FAILURE) {
                region_size += info.size;
            }
        }
    }
    PR_CloseDir(dirhandle);

    return region_size;
}

/*
 *  Check that there is enough room for the dbcache and region files.
 *  We can ignore this check if using db_home_dir and shared/private memory.
 */
static int
bdb_no_diskspace(struct ldbminfo *li, int dbenv_flags)
{
    struct statvfs dbhome_buf;
    struct statvfs db_buf;
    int using_region_files = !(dbenv_flags & (DB_PRIVATE | DB_SYSTEM_MEM));
    /* value of 10 == 10% == little more than the average overhead calculated for very large files on 64-bit system for bdb 4.7 */
    uint64_t expected_siz = li->li_dbcachesize + li->li_dbcachesize / 10; /* dbcache + region files */
    uint64_t fsiz;
    char *region_dir;

    if (statvfs(li->li_directory, &db_buf) < 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_no_diskspace", "Cannot get file system info for (%s); file system corrupted?\n",
                      li->li_directory);
        return 1;
    } else {
        /*
         *  If db_home_directory is set, and it's not the same as the db_directory,
         *  then check the disk space.
         */
        if (BDB_CONFIG(li)->bdb_dbhome_directory &&
            strcmp(BDB_CONFIG(li)->bdb_dbhome_directory, "") &&
            strcmp(li->li_directory, BDB_CONFIG(li)->bdb_dbhome_directory)) {
            /* Calculate the available space as long as we are not using shared memory */
            if (using_region_files) {
                if (statvfs(BDB_CONFIG(li)->bdb_dbhome_directory, &dbhome_buf) < 0) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "bdb_no_diskspace", "Cannot get file system info for (%s); file system corrupted?\n",
                                  BDB_CONFIG(li)->bdb_dbhome_directory);
                    return 1;
                }
                fsiz = ((uint64_t)dbhome_buf.f_bavail) * ((uint64_t)dbhome_buf.f_bsize);
                region_dir = BDB_CONFIG(li)->bdb_dbhome_directory;
            } else {
                /* Shared/private memory.  No need to check disk space, return success */
                return 0;
            }
        } else {
            /* Ok, just check the db directory */
            region_dir = li->li_directory;
            fsiz = ((PRUint64)db_buf.f_bavail) * ((PRUint64)db_buf.f_bsize);
        }
        /* Adjust the size for the region files */
        fsiz += bdb_get_region_size(region_dir);

        /* Check if we have enough space */
        if (fsiz < expected_siz) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_no_diskspace", "No enough space left on device (%s) (%" PRIu64 " bytes); "
                                          "at least %" PRIu64 " bytes space is needed for db region files\n",
                          region_dir, fsiz, expected_siz);
            return 1;
        }

        return 0;
    }
}

/*
 * This function is called after all the config options have been read in,
 * so we can do real initialization work here.
 */
#define DBCONFLEN 3
#define CATASTROPHIC (bdb_db_env *)-1

int
bdb_start(struct ldbminfo *li, int dbmode)
{
    /*
     * So, here we open our DB_ENV session. We store it away for future use.
     * We also check to see if we exited cleanly last time. If we didn't,
     * we try to recover. If recovery fails, we're hosed.
     * We also create the thread which handles checkpointing and logfile
     * truncation here.
     */
    int return_value = -1;
    bdb_config *conf = NULL;
    dblayer_private *priv = NULL;
    bdb_db_env *pEnv = NULL;
    char *region_dir = NULL; /* directory to place region files */
    char *log_dir = NULL;    /* directory to place txn log files */
    int open_flags = 0;

    PR_ASSERT(NULL != li);

    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;

    if (NULL == priv) {
        /* you didn't call init successfully */
        return -1;
    }

    if (NULL != priv->dblayer_env) {
        if (CATASTROPHIC == priv->dblayer_env) {
            slapi_log_err(SLAPI_LOG_CRIT,
                          "bdb_start", "DB previously failed to start.\n");
            return -1;
        } else {
            slapi_log_err(SLAPI_LOG_WARNING,
                          "bdb_start", "DB already started.\n");
            return 0;
        }
    }

    /* DBDB we should pick these up in our config routine, and do away with
     *  the li_ one */
    if (NULL == li->li_directory || '\0' == *li->li_directory) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "bdb_start", "DB directory is not specified.\n");
        return -1;
    }
    PR_Lock(li->li_config_mutex);
    /* li->li_directory comes from nsslapd-directory */
    /* bdb_home_directory is freed in bdb_post_close.
     * li_directory needs to live beyond dblayer. */
    slapi_ch_free_string(&conf->bdb_home_directory);
    conf->bdb_home_directory = slapi_ch_strdup(li->li_directory);
    conf->bdb_cachesize = li->li_dbcachesize;
    conf->bdb_lock_config = li->li_dblock;
    priv->dblayer_file_mode = li->li_mode;
    conf->bdb_ncache = li->li_dbncache;
    PR_Unlock(li->li_config_mutex);

    /* use nsslapd-db-home-directory (bdb_dbhome_directory), if set */
    /* Otherwise, nsslapd-directory (bdb_home_directory). */
    region_dir = bdb_get_home_dir(li, NULL);
    if (!region_dir || !(*region_dir)) {
        return -1;
    }
    if (!bdb_version_exists(li, region_dir)) {
        bdb_version_write(li, region_dir, NULL, DBVERSION_ALL);
    }

    /* Check here that the database directory both exists, and that we have
     * the appropriate access to it */
    return_value = bdb_grok_directory(region_dir,
                                          DBLAYER_DIRECTORY_READWRITE_ACCESS);
    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                "Can't start because the database directory \"%s\" either doesn't exist, or is not accessible\n",
                region_dir);
        return return_value;
    }

    log_dir = conf->bdb_log_directory; /* nsslapd-db-logdirectory */
    if (log_dir && *log_dir) {
        /* checking the user defined log dir's accessability */
        return_value = bdb_grok_directory(log_dir,
                                              DBLAYER_DIRECTORY_READWRITE_ACCESS);
        if (0 != return_value) {
            slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                    "Can't start because the log directory \"%s\" either doesn't exist, or is not accessible\n",
                    log_dir);
            return return_value;
        }
    }

    /* Sanity check on cache size on platforms which allow us to figure out
     * the available phys mem */
    slapi_pal_meminfo *mi = spal_meminfo_get();
    util_cachesize_result result = util_is_cachesize_sane(mi, &(conf->bdb_cachesize));
    if (result == UTIL_CACHESIZE_ERROR) {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start", "Unable to determine if cachesize was valid!!!");
    } else if (result == UTIL_CACHESIZE_REDUCED) {
        /* In some cases we saw this go to 0, prevent this. */
        if (conf->bdb_cachesize < MINCACHESIZE) {
            conf->bdb_cachesize = MINCACHESIZE;
        }
        /* Oops---looks like the admin misconfigured, let's warn them */
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start",
            "Likely CONFIGURATION ERROR - dbcachesize is configured to use more than the available "
            "memory, decreased to (%" PRIu64 " bytes).\n", conf->bdb_cachesize);
        li->li_dbcachesize = conf->bdb_cachesize;
    }
    spal_meminfo_destroy(mi);

    /* fill in DB_ENV stuff from the common configuration */
    return_value = bdb_make_env(&pEnv, li);
    if (return_value != 0)
        return return_value;

    if ((DBLAYER_NORMAL_MODE | DBLAYER_CLEAN_RECOVER_MODE) & dbmode) {
        /* Now, we read our metadata */
        return_value = bdb_read_metadata(li);
        if (0 != return_value) {
            /* The error message was output by bdb_read_metadata() */
            return -1;
        }
        if (bdb_restore_file_check(li)) {
            dblayer_set_restored();
        }
    }

    bdb_free_env(&priv->dblayer_env);
    priv->dblayer_env = pEnv;

    open_flags = DB_CREATE | DB_INIT_MPOOL | DB_THREAD;

    if (conf->bdb_enable_transactions) {
        open_flags |= (DB_INIT_TXN | DB_INIT_LOG | DB_INIT_LOCK);
        if (conf->bdb_recovery_required) {
            open_flags |= DB_RECOVER;
            if (DBLAYER_RESTORE_MODE & dbmode) {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start",
                        "Recovering database after restore from archive.\n");
            } else if (DBLAYER_CLEAN_RECOVER_MODE & dbmode) {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start",
                        "Clean up db environment and start from archive.\n");
            } else {
                glob_t globbuf;
                char file_pattern[MAXPATHLEN];

                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start",
                        "Detected Disorderly Shutdown last time Directory Server was running, recovering database.\n");
                slapi_disorderly_shutdown(PR_TRUE);

                /* Better wipe out the region files to help ensure a clean start */
                PR_snprintf(file_pattern, MAXPATHLEN, "%s/%s", region_dir, "__db.*");
                if (glob(file_pattern, 0, NULL, &globbuf) == 0) {
                    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
                        (void) remove(globbuf.gl_pathv[i]);
                    }
                    globfree(&globbuf);
                }
            }
        }
        switch (dbmode & DBLAYER_RESTORE_MASK) {
        case DBLAYER_RESTORE_MODE:
            open_flags |= DB_RECOVER_FATAL;
            open_flags &= ~DB_RECOVER; /* shouldn't set both */
            if (!(dbmode & DBLAYER_NO_DBTHREADS_MODE))
                dbmode = DBLAYER_NORMAL_MODE; /* to restart helper threads */
            break;
        case DBLAYER_RESTORE_NO_RECOVERY_MODE:
            open_flags &= ~(DB_RECOVER | DB_RECOVER_FATAL);
            if (!(dbmode & DBLAYER_NO_DBTHREADS_MODE))
                dbmode = DBLAYER_NORMAL_MODE; /* to restart helper threads */
        }
    }

    if (conf->bdb_private_mem) {
        slapi_log_err(SLAPI_LOG_INFO, "bdb_start",
                      "Server is running with nsslapd-db-private-mem on; "
                      "No other process is allowed to access the database\n");
        open_flags |= DB_PRIVATE;
    }

    if (conf->bdb_named_regions) {
        open_flags |= DB_SYSTEM_MEM;
    }

    if (conf->bdb_lockdown) {
        open_flags |= DB_LOCKDOWN;
    }


    /* Is the cache being re-sized ? (If we're just doing an archive or export,
     * we don't care if the cache is being re-sized) */
    if ((conf->bdb_previous_cachesize || conf->bdb_previous_ncache) &&
        (conf->bdb_previous_lock_config) &&
        ((conf->bdb_cachesize != conf->bdb_previous_cachesize) ||
         (conf->bdb_ncache != conf->bdb_previous_ncache) ||
         (conf->bdb_lock_config != conf->bdb_previous_lock_config)) &&
        !(dbmode & (DBLAYER_ARCHIVE_MODE | DBLAYER_EXPORT_MODE))) {
        if (conf->bdb_cachesize != conf->bdb_previous_cachesize) {
            slapi_log_err(SLAPI_LOG_INFO, "bdb_start", "Resizing db cache size: %" PRIu64 " -> %" PRIu64 "\n",
                          conf->bdb_previous_cachesize, conf->bdb_cachesize);
        }
        if (conf->bdb_ncache != conf->bdb_previous_ncache) {
            slapi_log_err(SLAPI_LOG_INFO, "bdb_start", "Resizing db cache count: %d -> %d\n",
                          conf->bdb_previous_ncache, conf->bdb_ncache);
        }
        if (conf->bdb_lock_config != conf->bdb_previous_lock_config) {
            /*
             * The default value of nsslapd-db-locks is BDB_LOCKS_MIN.
             * We don't allow lower value than that.
             */
            if (conf->bdb_lock_config <= BDB_LOCK_NB_MIN) {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start", "New max db lock count is too small.  "
                              "Resetting it to the default value %d.\n",
                              BDB_LOCK_NB_MIN);
                conf->bdb_lock_config = BDB_LOCK_NB_MIN;
            }
            if (conf->bdb_lock_config != conf->bdb_previous_lock_config) {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start", "Resizing max db lock count: %d -> %d\n",
                              conf->bdb_previous_lock_config, conf->bdb_lock_config);
            }
        }
        bdb_reset_env(li);
        /*
         * Once pEnv->remove (via bdb_reset_env) has been called,
         * the DB_ENV (pEnv) needs to be created again.
         */
        if ((return_value = bdb_make_env(&pEnv, li)) != 0) {
            slapi_log_err(SLAPI_LOG_CRIT,
                          "bdb_start", "Failed to create DBENV (returned: %d).\n",
                          return_value);
        }
        bdb_free_env(&priv->dblayer_env);
        priv->dblayer_env = pEnv;
    }

    /* transactions enabled and logbuf size greater than sleepycat's default */
    if (conf->bdb_enable_transactions && (conf->bdb_logbuf_size > 0)) {
        if (conf->bdb_logbuf_size >= 32768) {
            pEnv->bdb_DB_ENV->set_lg_bsize(pEnv->bdb_DB_ENV, conf->bdb_logbuf_size);
        } else {
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start",
                "Using default value for log bufsize because configured value (%" PRIu64 ") is too small.\n",
                conf->bdb_logbuf_size);
        }
    }

    /* check if there's enough disk space to start */
    if (bdb_no_diskspace(li, open_flags)) {
        return ENOSPC;
    }

    bdb_set_data_dir(pEnv, conf->bdb_data_directories);
    /* If we're doing recovery, we MUST open the env single-threaded ! */
    if ((open_flags & DB_RECOVER) || (open_flags & DB_RECOVER_FATAL)) {
        /* Recover, then close, then open again */
        int recover_flags = open_flags & ~DB_THREAD;
        if (DBLAYER_CLEAN_RECOVER_MODE & dbmode) /* upgrade case */
        {
            DB_ENV *thisenv = pEnv->bdb_DB_ENV;
            return_value = thisenv->remove(thisenv, region_dir, DB_FORCE);
            if (0 != return_value) {
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "Failed to remove old db env in %s: %s\n",
                        region_dir, dblayer_strerror(return_value));
                return return_value;
            }
            dbmode = DBLAYER_NORMAL_MODE;

            if ((return_value = bdb_make_env(&pEnv, li)) != 0) {
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "Failed to create DBENV (returned: %d).\n", return_value);
                return return_value;
            }
        }


        return_value = (pEnv->bdb_DB_ENV->open)(
            pEnv->bdb_DB_ENV,
            region_dir,
            recover_flags,
            priv->dblayer_file_mode);
        if (0 != return_value) {
            if (return_value == ENOMEM) {
                /*
                 * https://blackflag.mcom.com/show_bug.cgi?id=557319
                 * Crash ns-slapd while running scalab01 after restart slapd
                 */
                slapi_log_err(SLAPI_LOG_CRIT,
                              "bdb_start", "mmap in opening database environment (recovery mode) "
                              "failed trying to allocate %" PRIu64 " bytes. (OS err %d - %s)\n",
                              li->li_dbcachesize, return_value, dblayer_strerror(return_value));
                bdb_free_env(&priv->dblayer_env);
                priv->dblayer_env = CATASTROPHIC;
            } else {
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start", "Database Recovery Process FAILED. "
                              "The database is not recoverable. err=%d: %s\n",
                              return_value, dblayer_strerror(return_value));
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "Please make sure there is enough disk space for dbcache (%" PRIu64 " bytes) and db region files\n",
                        li->li_dbcachesize);
            }
            return return_value;
        } else {
            open_flags &= ~(DB_RECOVER | DB_RECOVER_FATAL);
            pEnv->bdb_DB_ENV->close(pEnv->bdb_DB_ENV, 0);
            if ((return_value = bdb_make_env(&pEnv, li)) != 0) {
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "Failed to create DBENV (returned: %d).\n", return_value);
                return return_value;
            }
            bdb_free_env(&priv->dblayer_env);
            priv->dblayer_env = pEnv;
            bdb_set_data_dir(pEnv, conf->bdb_data_directories);
        }
    }

    if ((!conf->bdb_durable_transactions) ||
        ((conf->bdb_enable_transactions) && (trans_batch_limit > 0))) {
        pEnv->bdb_DB_ENV->set_flags(pEnv->bdb_DB_ENV, DB_TXN_WRITE_NOSYNC, 1);
    }
    /* ldbm2index uses transactions but sets the transaction flag to off - we
       need to dblayer_init_pvt_txn in that case */
    dblayer_init_pvt_txn();
    if (!((DBLAYER_IMPORT_MODE | DBLAYER_INDEX_MODE) & dbmode)) {
        pEnv->bdb_openflags = open_flags;
        return_value = (pEnv->bdb_DB_ENV->open)(
            pEnv->bdb_DB_ENV,
            region_dir,
            open_flags,
            priv->dblayer_file_mode);


        /* Now attempt to start up the checkpoint and deadlock threads */
        /* note: need to be '==', not '&' to omit DBLAYER_NO_DBTHREADS_MODE */
        if ((DBLAYER_NORMAL_MODE == dbmode) &&
            (0 == return_value)) {
            /* update the dbversion file */
            bdb_version_write(li, region_dir, NULL, DBVERSION_ALL);

            /* if dblayer_close then bdb_start is called,
               this flag is set */
            conf->bdb_stop_threads = 0;
            if (0 != (return_value = bdb_start_deadlock_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = bdb_start_checkpoint_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = bdb_start_log_flush_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = bdb_start_trickle_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = bdb_start_perf_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = bdb_start_locks_monitoring_thread(li))) {
                return return_value;
            }

            /* We need to free the memory to avoid a leak
             * Also, we have to evaluate if the performance counter
             * should be preserved or not for database restore.
             * Look - https://github.com/389ds/389-ds-base/issues/4073
             */
            if (conf->perf_private) {
                bdb_perfctrs_terminate(&conf->perf_private, pEnv->bdb_DB_ENV);
            }
            /* Now open the performance counters stuff */
            bdb_perfctrs_init(li, &(conf->perf_private));
            if (getenv(TXN_TESTING)) {
                bdb_start_txn_test_thread(li);
            }
        }
        if (return_value != 0) {
            if (return_value == ENOMEM) {
                /*
                 * https://blackflag.mcom.com/show_bug.cgi?id=557319
                 * Crash ns-slapd while running scalab01 after restart slapd
                 */
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "mmap in opening database environment failed trying to allocate %" PRIu64 " bytes. (OS err %d - %s)\n",
                        li->li_dbcachesize, return_value, dblayer_strerror(return_value));
                bdb_free_env(&priv->dblayer_env);
                priv->dblayer_env = CATASTROPHIC;
            } else {
                slapi_log_err(SLAPI_LOG_CRIT, "bdb_start",
                        "Opening database environment (%s) failed. err=%d: %s\n",
                        region_dir, return_value, dblayer_strerror(return_value));
            }
        }
        return return_value;
    }
    return 0;
}

/*
 * If import cache autosize is enabled:
 *    nsslapd-import-cache-autosize: -1 or 1 ~ 99
 * calculate the import cache size.
 * If import cache is disabled:
 *    nsslapd-import-cache-autosize: 0
 * get the nsslapd-import-cachesize.
 * Calculate the memory size left after allocating the import cache size.
 *
 * Note: this function is called only if the import is executed as a stand
 * alone command line (ldif2db).
 */
int
bdb_check_and_set_import_cache(struct ldbminfo *li)
{
    uint64_t import_cache = 0;
    char s[64]; /* big enough to hold %ld */
    /* Get our platform memory values. */
    slapi_pal_meminfo *mi = spal_meminfo_get();

    if (mi == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "check_and_set_import_cache", "Failed to get system memory infomation\n");
        return ENOENT;
    }
    slapi_log_err(SLAPI_LOG_INFO, "check_and_set_import_cache", "pagesize: %" PRIu64 ", available bytes %" PRIu64 ", process usage %" PRIu64 " \n", mi->pagesize_bytes, mi->system_available_bytes, mi->process_consumed_bytes);

    /*
     * default behavior for ldif2db import cache,
     * nsslapd-import-cache-autosize==-1,
     * autosize 50% mem to import cache
     */
    if (li->li_import_cache_autosize < 0) {
        li->li_import_cache_autosize = 50;
    }

    /* sanity check */
    if (li->li_import_cache_autosize >= 100) {
        slapi_log_err(SLAPI_LOG_NOTICE,
                      "check_and_set_import_cache",
                      "Import cache autosizing value (nsslapd-import-cache-autosize) should not be "
                      "greater than or equal to 100%%. Reset to 50%%.\n");
        li->li_import_cache_autosize = 50;
    }

    if (li->li_import_cache_autosize == 0) {
        /* user specified importCache */
        import_cache = li->li_import_cachesize;

    } else {
        /* autosizing importCache */
        /* ./125 instead of ./100 is for adjusting the BDB overhead. */
        import_cache = (li->li_import_cache_autosize * mi->system_available_bytes) / 125;
    }

    if (util_is_cachesize_sane(mi, &import_cache) == UTIL_CACHESIZE_ERROR) {

        slapi_log_err(SLAPI_LOG_INFO, "check_and_set_import_cache", "Import failed to run: unable to validate system memory limits.\n");
        spal_meminfo_destroy(mi);
        return ENOMEM;
    }

    slapi_log_err(SLAPI_LOG_INFO, "check_and_set_import_cache", "Import allocates %" PRIu64 "KB import cache.\n", import_cache / 1024);
    if (li->li_import_cache_autosize > 0) {
        /* import cache autosizing */
        /* set the calculated import cache size to the config */
        sprintf(s, "%" PRIu64, import_cache);
        bdb_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, s);
    }
    spal_meminfo_destroy(mi);
    return 0;
}

/*
 * Creates the db handler with the proper pagesize.
 * This function should be systematically used instead of db_create when the handler
 * is used to open a database.
 * ( db_create may still be used when removing or renaming a database )
 */
int
dbbdb_create_db_for_open(backend *be, const char *funcname, int open_flags, DB **dbp, DB_ENV *dbenv)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    bdb_config *conf = (bdb_config *)li->li_dblayer_config;
    int rval;

    rval = db_create(dbp, dbenv, 0);
    if (rval) {
        slapi_log_err(SLAPI_LOG_ERR, funcname, "Unable to create db handler! %d\n", rval);
        return rval;
    }

    if (open_flags & DB_CREATE) {
        unsigned pagesize = (conf->bdb_page_size == 0) ? DBLAYER_PAGESIZE : conf->bdb_page_size;
        rval = (*dbp)->set_pagesize(*dbp, pagesize);
        if (rval) {
            slapi_log_err(SLAPI_LOG_ERR,
                          funcname, "dbp->set_pagesize(%" PRIu32 ") failed %d\n", pagesize, rval);
            return rval;
        }
    }
    return rval;
}


/* mode is one of
 * DBLAYER_NORMAL_MODE,
 * DBLAYER_INDEX_MODE,
 * DBLAYER_IMPORT_MODE,
 * DBLAYER_EXPORT_MODE
 */
int
bdb_instance_start(backend *be, int mode)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    bdb_config *conf;
    dblayer_private *priv;
    bdb_db_env *pEnv;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int return_value = -1;

    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    pEnv = priv->dblayer_env;
    if (CATASTROPHIC == pEnv || NULL == pEnv) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_instance_start", "DB Instance %s: dbenv is not available (0x%p).\n",
                      inst ? inst->inst_name : "unknown", pEnv);
        return return_value;
    }

    if (NULL != inst->inst_id2entry) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "bdb_instance_start", "DB instance \"%s\" already started.\n",
                      inst->inst_name);
        return 0;
    }

    if (attrcrypt_init(inst)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_instance_start", "Unable to initialize attrcrypt system for %s\n",
                      inst->inst_name);
        return return_value;
    }

    /* Get the name of the directory that holds index files
     * for this instance. */
    if (dblayer_get_instance_data_dir(be) != 0) {
        /* Problem getting the name of the directory that holds the
         * index files for this instance. */
        return return_value;
    }

    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp) {
        return_value = bdb_grok_directory(inst_dirp,
                                              DBLAYER_DIRECTORY_READWRITE_ACCESS);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_instance_start",
                      "Can't start because the database instance "
                      "directory is NULL\n");
        goto errout;
    }
    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_instance_start",
                      "Can't start because the database instance "
                      "directory \"%s\" either doesn't exist, "
                      "or the db files are not accessible\n",
                      inst_dirp);
        goto errout;
    }

    if (mode & DBLAYER_NORMAL_MODE) {
        /* In normal mode (not db2ldif, ldif2db, etc.) we need to deal with
         * the dbversion file here. */

        /* Read the dbversion file if there is one, and create it
         * if it doesn't exist. */
        if (bdb_version_exists(li, inst_dirp)) {
            char *ldbmversion = NULL;
            char *dataversion = NULL;

            if (bdb_version_read(li, inst_dirp, &ldbmversion, &dataversion) != 0) {
                slapi_log_err(SLAPI_LOG_WARNING, "bdb_instance_start", "Unable to read dbversion "
                                                                           "file in %s\n",
                              inst->inst_dir_name);
            } else {
                int rval = 0;
                /* check the DBVERSION and reset idl-switch if needed (DS6.2) */
                /* from the next major rel, we won't do this and just upgrade */
                if (!(li->li_flags & LI_FORCE_MOD_CONFIG)) {
                    bdb_adjust_idl_switch(ldbmversion, li);
                }
                slapi_ch_free_string(&ldbmversion);

                /* check to make sure these instance was made with the correct
                 * version. */
                rval = bdb_check_db_inst_version(inst);
                if (rval & DBVERSION_NOT_SUPPORTED) {
                    slapi_log_err(SLAPI_LOG_ERR, "bdb_instance_start", " DB Instance %s does not have the "
                                                                           "expected version\n",
                                  inst->inst_name);
                    PR_ASSERT(0);
                    slapi_ch_free_string(&dataversion);
                    return_value = -1;
                    goto errout;
                } else if (rval & DBVERSION_NEED_DN2RDN) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "bdb_instance_start", "%s is on, while the instance %s is in the DN format. "
                                                            "Please run dn2rdn to convert the database format.\n",
                                  CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
                    slapi_ch_free_string(&dataversion);
                    return_value = -1;
                    goto errout;
                } else if (rval & DBVERSION_NEED_RDN2DN) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "bdb_instance_start", "%s is off, while the instance %s is in the RDN "
                                                            "format. Please change the value to on in dse.ldif.\n",
                                  CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
                    slapi_ch_free_string(&dataversion);
                    return_value = -1;
                    goto errout;
                }

                /* record the dataversion */
                if (dataversion != NULL && *dataversion != '\0') {
                    inst->inst_dataversion = dataversion;
                } else {
                    slapi_ch_free_string(&dataversion);
                }

                rval = bdb_ldbm_upgrade(inst, rval);
                if (0 != rval) {
                    slapi_log_err(SLAPI_LOG_ERR, "bdb_instance_start", "Upgrading instance %s failed\n",
                                  inst->inst_name);
                    PR_ASSERT(0);
                    return_value = -1;
                    goto errout;
                }
            }
        } else {
            /* The dbversion file didn't exist, so we'll create one. */
            bdb_version_write(li, inst_dirp, NULL, DBVERSION_ALL);
        }
    } /* on import we don't mess with the dbversion file except to write it
       * when done with the import. */

    /* Now attempt to open id2entry */
    {
        char *id2entry_file;
        int open_flags = 0;
        DB *dbp;
        char *subname;
        bdb_db_env *mypEnv;

        id2entry_file = slapi_ch_smprintf("%s/%s", inst->inst_dir_name,
                                          ID2ENTRY LDBM_FILENAME_SUFFIX);

        open_flags = DB_CREATE | DB_THREAD;

        /* The subname argument allows applications to have
         * subdatabases, i.e., multiple databases inside of a single
         * physical file. This is useful when the logical databases
         * are both numerous and reasonably small, in order to
         * avoid creating a large number of underlying files.
         */
        subname = NULL;
        mypEnv = NULL;
        if (mode & (DBLAYER_IMPORT_MODE | DBLAYER_INDEX_MODE)) {
            size_t cachesize;
            char *data_directories[2] = {0, 0};
            /* [605974] delete DB_PRIVATE:
             * to make import visible to the other process */
            int oflags = DB_CREATE | DB_INIT_MPOOL | DB_THREAD;
            /*
             * but nsslapd-db-private-import-mem should work with import,
             * as well */
            if (conf->bdb_private_import_mem) {
                slapi_log_err(SLAPI_LOG_INFO,
                              "bdb_instance_start", "Import is running with "
                                                        "nsslapd-db-private-import-mem on; "
                                                        "No other process is allowed to access the database\n");
                oflags |= DB_PRIVATE;
            }
            PR_Lock(li->li_config_mutex);
            /* import cache checking and autosizing is available only
             * for the command line */
            if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
                return_value = bdb_check_and_set_import_cache(li);
                if (return_value) {
                    goto out;
                }
            }
            cachesize = li->li_import_cachesize;
            PR_Unlock(li->li_config_mutex);

            if (cachesize < 1048576) {
                /* make it at least 1M */
                cachesize = 1048576;
            }
            conf->bdb_cachesize = cachesize;
            /* We always auto-calculate ncache for the import region */
            conf->bdb_ncache = 0;

            /* use our own env */
            return_value = bdb_make_env(&mypEnv, li);
            if (return_value != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_instance_start", "Unable to create new DB_ENV for import/export! %d\n",
                              return_value);
                goto out;
            }
            /* do not assume import cache size is under 1G */
            mypEnv->bdb_DB_ENV->set_cachesize(mypEnv->bdb_DB_ENV,
                                                  cachesize / GIGABYTE,
                                                  cachesize % GIGABYTE,
                                                  conf->bdb_ncache);
            /* probably want to change this -- but for now, create the
             * mpool files in the instance directory.
             */
            mypEnv->bdb_openflags = oflags;
            data_directories[0] = inst->inst_parent_dir_name;
            bdb_set_data_dir(mypEnv, data_directories);
            return_value = (mypEnv->bdb_DB_ENV->open)(mypEnv->bdb_DB_ENV,
                                                          inst_dirp,
                                                          oflags,
                                                          priv->dblayer_file_mode);
            if (return_value != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_instance_start", "Unable to open new DB_ENV for import/export! %d\n",
                              return_value);
                goto out;
            }
            inst->inst_db = mypEnv;
        } else {
            mypEnv = pEnv;
        }

        inst->inst_id2entry = NULL;
        return_value = dbbdb_create_db_for_open(be, "bdb_instance_start", open_flags,
                                                (DB**)&inst->inst_id2entry, mypEnv->bdb_DB_ENV);
        if (0 != return_value) {
            goto out;
        }
        dbp = inst->inst_id2entry;

        if ((charray_get_index(conf->bdb_data_directories,
                               inst->inst_parent_dir_name) != 0) &&
            !bdb_inst_exists(inst, NULL)) {
            char *abs_id2entry_file = NULL;
            /* create a file with abs path, then try again */

            abs_id2entry_file = slapi_ch_smprintf("%s%c%s", inst_dirp,
                                                  get_sep(inst_dirp), ID2ENTRY LDBM_FILENAME_SUFFIX);
            DB_OPEN(mypEnv->bdb_openflags,
                    dbp, NULL /* txnid */, abs_id2entry_file, subname, DB_BTREE,
                    open_flags, priv->dblayer_file_mode, return_value);
            dbp->close(dbp, 0);
            return_value = dbbdb_create_db_for_open(be, "bdb_instance_start", open_flags,
                                                    (DB**)&inst->inst_id2entry, mypEnv->bdb_DB_ENV);
            if (0 != return_value)
                goto out;
            dbp = inst->inst_id2entry;

            slapi_ch_free_string(&abs_id2entry_file);
        }
        DB_OPEN(mypEnv->bdb_openflags,
                dbp, NULL /* txnid */, id2entry_file, subname, DB_BTREE,
                open_flags, priv->dblayer_file_mode, return_value);
        if (0 != return_value) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_instance_start", "dbp->open(\"%s\") failed: %s (%d)\n",
                          id2entry_file, dblayer_strerror(return_value),
                          return_value);
            /* if it's a newly created backend instance,
             * need to check the inst_parent_dir already exists and
             * set as a data dir */
            if (strstr(dblayer_strerror(return_value),
                       "No such file or directory")) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_instance_start", "Instance %s is not registered as a db data directory. "
                                                        "Please restart the server to create it.\n",
                              inst ? inst->inst_name : "unknown");
            } else if (strstr(dblayer_strerror(return_value),
                              "Permission denied")) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_instance_start", "Instance directory %s may not be writable\n",
                              inst_dirp);
            }

            goto out;
        }
    out:
        slapi_ch_free_string(&id2entry_file);
    }

    if (0 == return_value) {
        /* get nextid from disk now */
        get_ids_from_disk(be);
    }

    if (mode & DBLAYER_NORMAL_MODE) {
        bdb_version_write(li, inst_dirp, NULL, DBVERSION_ALL);
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
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_instance_start", "Backend '%s' "
                                                                "has no IDs left. DATABASE MUST BE REBUILT.\n",
                      be->be_name);
        return 1;
    }

    if (return_value != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_instance_start", "Failure %s (%d)\n",
                      dblayer_strerror(return_value), return_value);
    }
errout:
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return return_value;
}

/*
 * dblayer_get_aux_id2entry:
 * - create a dedicated db env and db handler for id2entry.
 * - introduced for upgradedb not to share the env and db handler with
 *   other index files to support multiple passes and merge.
 * - Argument path is for returning the full path for the id2entry.db#,
 *   if the memory to store the address of the full path is given.  The
 *   caller is supposed to release the full path.
 */
int
bdb_get_aux_id2entry(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path)
{
    return bdb_get_aux_id2entry_ext(be, ppDB, ppEnv, path, 0);
}

/*
 * flags:
 * DBLAYER_AUX_ID2ENTRY_TMP -- create id2entry_tmp.db#
 *
 * - if non-NULL *ppEnv is given, env is already open.
 *   Just open an id2entry[_tmp].db#.
 * - Argument path is for returning the full path for the id2entry[_tmp].db#,
 *   if the memory to store the address of the full path is given.  The
 *   caller is supposed to release the full path.
 */
int
bdb_get_aux_id2entry_ext(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path, int flags)
{
    ldbm_instance *inst;
    bdb_db_env *mypEnv = NULL;
    DB *dbp = NULL;
    int rval = 1;
    struct ldbminfo *li = NULL;
    bdb_config *oconf = NULL;
    bdb_config *conf = NULL;
    dblayer_private *priv = NULL;
    char *subname = NULL;
    int envflags = 0;
    int dbflags = 0;
    size_t cachesize;
    PRFileInfo64 prfinfo;
    PRStatus prst;
    char *id2entry_file = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    char *data_directories[2] = {0, 0};

    PR_ASSERT(NULL != be);

    if ((NULL == ppEnv) || (NULL == ppDB)) {
        slapi_log_err(SLAPI_LOG_ERR, "dblayer_get_aux_id2entry_ext", "No memory for DB_ENV or DB handle\n");
        goto done;
    }
    *ppDB = NULL;
    inst = (ldbm_instance *)be->be_instance_info;
    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "No instance/env: persistent id2entry is not available\n");
        goto done;
    }

    li = inst->inst_li;
    if (NULL == li) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "No ldbm info: persistent id2entry is not available\n");
        goto done;
    }

    priv = li->li_dblayer_private;
    oconf = (bdb_config *)li->li_dblayer_config;
    if (NULL == oconf) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "No dblayer info: persistent id2entry is not available\n");
        goto done;
    }
    conf = (bdb_config *)slapi_ch_calloc(1, sizeof(bdb_config));
    memcpy(conf, oconf, sizeof(bdb_config));
    conf->bdb_spin_count = 0;

    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp) {
        conf->bdb_home_directory = slapi_ch_smprintf("%s/dbenv", inst_dirp);
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "Instance dir is NULL: persistent id2entry is not available\n");
        goto done;
    }
    conf->bdb_log_directory = slapi_ch_strdup(conf->bdb_home_directory);

    prst = PR_GetFileInfo64(inst_dirp, &prfinfo);
    if (PR_FAILURE == prst || PR_FILE_DIRECTORY != prfinfo.type) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "No inst dir: persistent id2entry is not available\n");
        goto done;
    }

    prst = PR_GetFileInfo64(conf->bdb_home_directory, &prfinfo);
    if (PR_SUCCESS == prst) {
        ldbm_delete_dirs(conf->bdb_home_directory);
    }
    rval = mkdir_p(conf->bdb_home_directory, 0700);
    if (rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "Can't create env dir: persistent id2entry is not available\n");
        goto done;
    }

    /* use our own env if not passed */
    if (!*ppEnv) {
        rval = bdb_make_env(&mypEnv, li);
        if (rval) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dblayer_get_aux_id2entry_ext", "Unable to create new DB_ENV for import/export! %d\n", rval);
            goto err;
        }
    }

    envflags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE;
    cachesize = DEFAULT_DBCACHE_SIZE;

    if (!*ppEnv) {
        mypEnv->bdb_DB_ENV->set_cachesize(mypEnv->bdb_DB_ENV,
                                              0, cachesize, conf->bdb_ncache);

        /* probably want to change this -- but for now, create the
         * mpool files in the instance directory.
         */
        mypEnv->bdb_openflags = envflags;
        data_directories[0] = inst->inst_parent_dir_name;
        bdb_set_data_dir(mypEnv, data_directories);
        rval = (mypEnv->bdb_DB_ENV->open)(mypEnv->bdb_DB_ENV,
                                              conf->bdb_home_directory, envflags, priv->dblayer_file_mode);
        if (rval) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dblayer_get_aux_id2entry_ext", "Unable to open new DB_ENV for upgradedb/reindex %d\n", rval);
            goto err;
        }
        *ppEnv = mypEnv->bdb_DB_ENV;
    }
    rval = dbbdb_create_db_for_open(be, "dblayer_get_aux_id2entry_ext", dbflags, &dbp, *ppEnv);
    if (rval) {
        goto err;
    }

    if (flags & DBLAYER_AUX_ID2ENTRY_TMP) {
        id2entry_file = slapi_ch_smprintf("%s/%s_tmp%s",
                                          inst->inst_dir_name, ID2ENTRY, LDBM_FILENAME_SUFFIX);
        dbflags = DB_CREATE;
    } else {
        id2entry_file = slapi_ch_smprintf("%s/%s",
                                          inst->inst_dir_name, ID2ENTRY LDBM_FILENAME_SUFFIX);
    }

    PR_ASSERT(bdb_inst_exists(inst, NULL));
    DB_OPEN(envflags, dbp, NULL /* txnid */, id2entry_file, subname, DB_BTREE,
            dbflags, priv->dblayer_file_mode, rval);
    if (rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_get_aux_id2entry_ext", "dbp->open(\"%s\") failed: %s (%d)\n",
                      id2entry_file, dblayer_strerror(rval), rval);
        if (strstr(dblayer_strerror(rval), "Permission denied")) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dblayer_get_aux_id2entry_ext", "Instance directory %s may not be writable\n", inst_dirp);
        }
        goto err;
    }
    *ppDB = dbp;
    rval = 0; /* to make it sure ... */
    goto done;
err:
    if (*ppEnv) {
        (*ppEnv)->close(*ppEnv, 0);
        *ppEnv = NULL;
    }
    if (conf->bdb_home_directory) {
        ldbm_delete_dirs(conf->bdb_home_directory);
    }
done:
    if (path) {
        if (0 == rval) { /* return the path only when successfull */
            *path = slapi_ch_smprintf("%s/%s", inst->inst_parent_dir_name,
                                      id2entry_file);
        } else {
            *path = NULL;
        }
    }
    slapi_ch_free_string(&id2entry_file);
    if (priv) {
        slapi_ch_free_string(&conf->bdb_home_directory);
        slapi_ch_free_string(&conf->bdb_log_directory);
    }
    /* Don't free priv->bdb_data_directories since priv doesn't own the memory */
    slapi_ch_free((void **)&conf);
    bdb_free_env((void **)&mypEnv);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

int
bdb_release_aux_id2entry(backend *be, DB *pDB, DB_ENV *pEnv)
{
    ldbm_instance *inst;
    char *envdir = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;

    inst = (ldbm_instance *)be->be_instance_info;
    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_release_aux_id2entry", "No instance/env: persistent id2entry is not available\n");
        goto done;
    }

    inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                          inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp) {
        envdir = slapi_ch_smprintf("%s/dbenv", inst_dirp);
    }

done:
    if (pDB) {
        pDB->close(pDB, 0);
    }
    if (pEnv) {
        pEnv->close(pEnv, 0);
    }
    if (envdir) {
        ldbm_delete_dirs(envdir);
        slapi_ch_free_string(&envdir);
    }
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return 0;
}


void
bdb_pre_close(struct ldbminfo *li)
{
    dblayer_private *priv = 0;
    bdb_config *conf;
    PRInt32 threadcount = 0;

    PR_ASSERT(NULL != li);
    priv = li->li_dblayer_private;
    conf = (bdb_config *)li->li_dblayer_config;
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    if (conf->bdb_stop_threads || !pEnv) /* already stopped.  do nothing... */
        return;

    /* first, see if there are any housekeeping threads running */
    pthread_mutex_lock(&pEnv->bdb_thread_count_lock);
    threadcount = pEnv->bdb_thread_count;
    pthread_mutex_unlock(&pEnv->bdb_thread_count_lock);

    if (threadcount) {
        PRIntervalTime cvwaittime = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL * 100);
        int timedout = 0;
        /* Print handy-dandy log message */
        slapi_log_err(SLAPI_LOG_INFO, "bdb_pre_close", "Waiting for %d database threads to stop\n",
                      threadcount);
        pthread_mutex_lock(&pEnv->bdb_thread_count_lock);
        /* Tell them to stop - we wait until the last possible moment to invoke
           this.  If we do this much sooner than this, we could find ourselves
           in a situation where the threads see the stop_threads and exit before
           we can issue the WaitCondVar below, which means the last thread to
           exit will do a NotifyCondVar that has nothing waiting.  If we do this
           inside the lock, we will ensure that the threads will block until we
           issue the WaitCondVar below */
        conf->bdb_stop_threads = 1;
        /* Wait for them to exit */
        while (pEnv->bdb_thread_count > 0) {
            struct timespec current_time = {0};
            PRIntervalTime before = PR_IntervalNow();
            /* There are 3 ways to wake up from this WaitCondVar:
               1) The last database thread exits and calls NotifyCondVar - thread_count
               should be 0 in this case
               2) Timeout - in this case, thread_count will be > 0 - bad
               3) A bad error occurs - bad - will be reported as a timeout
            */
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += DBLAYER_SLEEP_INTERVAL / 10; /* cvwaittime but in seconds */
            pthread_cond_timedwait(&pEnv->bdb_thread_count_cv, &pEnv->bdb_thread_count_lock, &current_time);
            if (pEnv->bdb_thread_count > 0) {
                /* still at least 1 thread running - see if this is a timeout */
                if ((PR_IntervalNow() - before) >= cvwaittime) {
                    threadcount = pEnv->bdb_thread_count;
                    timedout = 1;
                    break;
                }
                /* else just a spurious interrupt */
            }
        }
        pthread_mutex_unlock(&pEnv->bdb_thread_count_lock);
        if (timedout) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_pre_close", "Timeout after [%d] milliseconds; leave %d database thread(s)...\n",
                          (DBLAYER_SLEEP_INTERVAL * 100), threadcount);
            priv->dblayer_bad_stuff_happened = 1;
            goto timeout_escape;
        }
    }
    slapi_log_err(SLAPI_LOG_INFO, "bdb_pre_close", "All database threads now stopped\n");
timeout_escape:
    return;
}

int
bdb_post_close(struct ldbminfo *li, int dbmode)
{
    bdb_config *conf = 0;
    int return_value = 0;
    PR_ASSERT(NULL != li);
    dblayer_private *priv = li->li_dblayer_private;
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    conf = (bdb_config *)li->li_dblayer_config;

    /* We close all the files we ever opened, and call pEnv->close. */
    if (NULL == pEnv) /* db env is already closed. do nothing. */
        return return_value;
    /* Shutdown the performance counter stuff */
    if (DBLAYER_NORMAL_MODE & dbmode) {
        if (conf->perf_private) {
            bdb_perfctrs_terminate(&conf->perf_private, pEnv->bdb_DB_ENV);
        }
    }

    /* Now release the db environment */
    return_value = pEnv->bdb_DB_ENV->close(pEnv->bdb_DB_ENV, 0);
    bdb_free_env((void **)&pEnv); /* pEnv is now garbage */
    priv->dblayer_env = NULL;

    if (0 == return_value && !((DBLAYER_ARCHIVE_MODE | DBLAYER_EXPORT_MODE) & dbmode) && !priv->dblayer_bad_stuff_happened) {
        bdb_commit_good_database(conf, priv->dblayer_file_mode);
    }
    if (conf->bdb_data_directories) {
        /* bdb_data_directories are set in bdb_make_env via
         * dblayer_start, which is paired with dblayer_close. */
        /* no need to release bdb_home_directory,
         * which is one of bdb_data_directories */
        charray_free(conf->bdb_data_directories);
        conf->bdb_data_directories = NULL;
    }
    if (g_get_shutdown()) {
        /* if the dblayer is closed temporarily
         * eg. in online restore keep the directory settings
         */
        slapi_ch_free_string(&conf->bdb_dbhome_directory);
        slapi_ch_free_string(&conf->bdb_home_directory);
        slapi_ch_free_string(&conf->bdb_compactdb_time);
    }

    return return_value;
}

/*
 * This function is called when the server is shutting down, or when the
 * backend is being disabled (e.g. backup/restore).
 * This is not safe to call while other threads are calling into the open
 * databases !!!   So: DON'T !
 */
int
bdb_close(struct ldbminfo *li, int dbmode)
{
    backend *be = NULL;
    ldbm_instance *inst;
    Object *inst_obj;
    int return_value = 0;
    int shutdown = g_get_shutdown();

    bdb_pre_close(li);

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

    if (return_value != 0) {
        /* force recovery next startup if any close failed */
        dblayer_private *priv;
        PR_ASSERT(NULL != li);
        priv = li->li_dblayer_private;
        PR_ASSERT(NULL != priv);
        priv->dblayer_bad_stuff_happened = 1;
    }

    return_value |= bdb_post_close(li, dbmode);

    return return_value;
}

/* API to remove the environment */
int
bdb_remove_env(struct ldbminfo *li)
{
    DB_ENV *env = NULL;
    char *home_dir = NULL;
    int rc = db_env_create(&env, 0);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_remove_env", "Failed to create DB_ENV (returned: %d)\n", rc);
        return rc;
    }
    if (NULL == li) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_remove_env", "No ldbm info is given\n");
        return -1;
    }

    home_dir = bdb_get_home_dir(li, NULL);
    if (home_dir) {
        rc = env->remove(env, home_dir, 0);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_remove_env", "Failed to remove DB environment files. "
                                                "Please remove %s/__db.00# (# is 1 through 6)\n",
                          home_dir);
        }
    }
    return rc;
}

#if !defined(DB_DUPSORT)
#define DB_DUPSORT 0
#endif

/* bdb_idl_new_compare_dups: comparing ID, pass to libdb for callback */
int bdb_idl_new_compare_dups(
    DB * db __attribute__((unused)),
    const DBT *a,
    const DBT *b)
{
    ID a_copy, b_copy;
    memmove(&a_copy, a->data, sizeof(ID));
    memmove(&b_copy, b->data, sizeof(ID));
    return a_copy - b_copy;
}

static int
_dblayer_set_db_callbacks(bdb_config *conf, DB *dbp, struct attrinfo *ai)
{
    int idl_use_new = 0;
    int rc = 0;

    /* With the new idl design, the large 8Kbyte pages we use are not
       optimal. The page pool churns very quickly as we add new IDs under a
       sustained add load. Smaller pages stop this happening so much and
       consequently make us spend less time flushing dirty pages on checkpoints.
       But 8K is still a good page size for id2entry. So we now allow different
       page sizes for the primary and secondary indices.
       Filed as bug: 604654
     */
    if (idl_get_idl_new()) {
        rc = dbp->set_pagesize(
            dbp,
            (conf->bdb_index_page_size == 0) ? DBLAYER_INDEX_PAGESIZE : conf->bdb_index_page_size);
    } else {
        rc = dbp->set_pagesize(
            dbp,
            (conf->bdb_page_size == 0) ? DBLAYER_PAGESIZE : conf->bdb_page_size);
    }
    if (rc)
        return rc;

    /*
     * If using the "new" idl, set the flags and the compare function.
     * If using the "old" idl, we still need to set the index DB flags
     * for the attribute "entryRDN".
     */
    if (((idl_use_new = idl_get_idl_new()) ||
         0 == strcasecmp(ai->ai_type, LDBM_ENTRYRDN_STR)) &&
        !(ai->ai_indexmask & INDEX_VLV)) {
        /* set the flags */
        rc = dbp->set_flags(dbp, DB_DUP | DB_DUPSORT);
        if (rc)
            return rc;
        /* set the compare function */
        if (ai->ai_dup_cmp_fn) {
            /* If set, use the special dup compare callback */
            rc = dbp->set_dup_compare(dbp, ai->ai_dup_cmp_fn);
        } else if (idl_use_new) {
            rc = dbp->set_dup_compare(dbp, bdb_idl_new_compare_dups);
        }
        if (rc)
            return rc;
    }

    if (ai->ai_indexmask & INDEX_VLV) {
        /*
         * Need index with record numbers for
         * Virtual List View index
         */
        rc = dbp->set_flags(dbp, DB_RECNUM);
        if (rc)
            return rc;
    } else if (ai->ai_key_cmp_fn) { /* set in attr_index_config() */
        /*
          This is so that we can have ordered keys in the index, so that
          greater than/less than searches work on indexed attrs.  We had
          to introduce this when we changed the integer key format from
          a 32/64 bit value to a normalized string value.  The default
          bdb key cmp is based on length and lexicographic order, which
          does not work with integer strings.

          NOTE: If we ever need to use app_private for something else, we
          will have to create some sort of data structure with different
          fields for different uses.  We will also need to have a new()
          function that creates and allocates that structure, and a
          destroy() function that destroys the structure, and make sure
          to call it when the DB* is closed and/or freed.
        */
        dbp->app_private = (void *)ai->ai_key_cmp_fn;
        dbp->set_bt_compare(dbp, bdb_bt_compare);
    }
    return rc;
}

/* Routines for opening and closing random files in the DB_ENV.
   Used by ldif2db merging code currently.

   Return value:
       Success: 0
    Failure: -1
 */
int
bdb_get_db(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int open_flags = 0;
    char *file_name = NULL;
    char *rel_path = NULL;
    bdb_db_env *pENV = 0;
    bdb_config *conf = NULL;
    dblayer_private *priv = NULL;
    int return_value = 0;
    DB *dbp = NULL;
    char *subname = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;

    PR_ASSERT(NULL != li);
    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    if (NULL == inst->inst_dir_name) {
        if (dblayer_get_instance_data_dir(be) != 0)
            return -1;
    }

    if (NULL != inst->inst_parent_dir_name) {
        if (!charray_utf8_inlist(conf->bdb_data_directories,
                                 inst->inst_parent_dir_name) &&
            !is_fullpath(inst->inst_dir_name))

        {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dblayer_open_file", "The instance path %s is not registered for db_data_dir, "
                                               "although %s is a relative path.\n",
                          inst->inst_parent_dir_name, inst->inst_dir_name);
            return -1;
        }
    }

    pENV = (bdb_db_env *)priv->dblayer_env;
    if (inst->inst_db)
        pENV = (bdb_db_env *)inst->inst_db;

    PR_ASSERT(NULL != pENV);
    file_name = slapi_ch_smprintf("%s%s", indexname, LDBM_FILENAME_SUFFIX);
    rel_path = slapi_ch_smprintf("%s/%s", inst->inst_dir_name, file_name);

    open_flags = DB_THREAD;
    if (open_flag & DBOPEN_CREATE)
        open_flags |= DB_CREATE;
    if (open_flag & DBOPEN_TRUNCATE)
        open_flags |= DB_TRUNCATE;

    if (!ppDB)
        goto out;
    return_value = dbbdb_create_db_for_open(be, "dblayer_open_file", open_flags, (DB**)ppDB, pENV->bdb_DB_ENV);
    if (0 != return_value)
        goto out;

    dbp = (DB*)*ppDB;
    if (ai) {
        return_value = _dblayer_set_db_callbacks(conf, dbp, ai);
        if (return_value) {
            goto out;
        }
    }

    /* The subname argument allows applications to have
     * subdatabases, i.e., multiple databases inside of a single
     * physical file. This is useful when the logical databases
     * are both numerous and reasonably small, in order to
     * avoid creating a large number of underlying files.
     */
    /* If inst_parent_dir_name is not the primary DB dir &&
     * the index file does not exist */
    if ((charray_get_index(conf->bdb_data_directories,
                           inst->inst_parent_dir_name) > 0) &&
        !bdb_inst_exists(inst, file_name)) {
        char *abs_file_name = NULL;
        /* create a file with abs path, then try again */

        inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
        if (!inst_dirp || !*inst_dirp) {
            return_value = -1;
            goto out;
        }
        abs_file_name = slapi_ch_smprintf("%s%c%s",
                                          inst_dirp, get_sep(inst_dirp), file_name);
        DB_OPEN(pENV->bdb_openflags,
                dbp, NULL /* txnid */, abs_file_name, subname, DB_BTREE,
                open_flags, priv->dblayer_file_mode, return_value);
        dbp->close(dbp, 0);
        return_value = dbbdb_create_db_for_open(be, "dblayer_open_file", open_flags,(DB**)ppDB, pENV->bdb_DB_ENV);
        if (0 != return_value) {
            goto out;
        }
        dbp = (DB*)*ppDB;
        if (ai) {
            return_value = _dblayer_set_db_callbacks(conf, dbp, ai);
            if (return_value) {
                goto out;
            }
        }

        slapi_ch_free_string(&abs_file_name);
    }
    DB_OPEN(pENV->bdb_openflags,
            dbp, NULL, /* txnid */ rel_path, subname, DB_BTREE,
            open_flags, priv->dblayer_file_mode, return_value);
out:
    slapi_ch_free((void **)&file_name);
    slapi_ch_free((void **)&rel_path);
    if (inst_dirp != inst_dir) {
        slapi_ch_free_string(&inst_dirp);
    }
    /* close the database handle to avoid handle leak */
    if (dbp && (return_value != 0)) {
        bdb_close_file(&dbp);
    }
    return return_value;
}

int
bdb_close_file(DB **db)
{
    if (db) {
        DB *dbp = *db;
        *db = NULL; /* To avoid to leave stale DB, set NULL before closing. */
        return dbp->close(dbp, 0);
    }
    return 1;
}


/*
  bdb_db_remove assumptions:

  No environment has the given database open.

*/

static int
bdb_db_remove_ex(bdb_db_env *env, char const path[], char const dbName[], PRBool use_lock)
{
    DB_ENV *db_env = 0;
    int rc;
    DB *db;

    if (env) {
        if (use_lock)
            slapi_rwlock_wrlock(env->bdb_env_lock); /* We will be causing logging activity */
        db_env = env->bdb_DB_ENV;
    }

    rc = db_create(&db, db_env, 0); /* must use new handle to database */
    if (0 != rc) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_db_remove_ex", "Failed to create db (%d) %s\n",
                      rc, dblayer_strerror(rc));
        goto done;
    }
    rc = db->remove(db, path, dbName, 0); /* kiss the db goodbye! */

done:
    if (env) {
        if (use_lock)
            slapi_rwlock_unlock(env->bdb_env_lock);
    }

    return rc;
}


int
bdb_db_remove(bdb_db_env *env, char const path[], char const dbName[])
{
    return (bdb_db_remove_ex(env, path, dbName, PR_TRUE));
}

static int
bdb_db_compact_one_db(DB *db, ldbm_instance *inst)
{
    DBTYPE type;
    int rc = 0;
    back_txn txn;
    DB_COMPACT c_data = {0};

    rc = db->get_type(db, &type);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_db_compact_one_db",
                      "compactdb: failed to determine db type for %s: db error - %d %s\n",
                      inst->inst_name, rc, db_strerror(rc));
        return rc;
    }

    rc = dblayer_txn_begin(inst->inst_be, NULL, &txn);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_db_compact_one_db", "compactdb: transaction begin failed: %d\n", rc);
        return rc;
    }
    /*
     * https://docs.oracle.com/cd/E17275_01/html/api_reference/C/BDB-C_APIReference.pdf
     * "DB_FREELIST_ONLY
     * Do no page compaction, only returning pages to the filesystem that are already free and at the end
     * of the file. This flag must be set if the database is a Hash access method database."
     *
     */

    uint32_t compact_flags = DB_FREE_SPACE;
    if (type == DB_HASH) {
        compact_flags |= DB_FREELIST_ONLY;
    }
    rc = db->compact(db, txn.back_txn_txn, NULL /*start*/, NULL /*stop*/,
                     &c_data, compact_flags, NULL /*end*/);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_db_compact_one_db",
                      "compactdb: failed to compact %s; db error - %d %s\n",
                      inst->inst_name, rc, db_strerror(rc));
        if ((rc = dblayer_txn_abort(inst->inst_be, &txn))) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_db_compact_one_db", "compactdb: failed to abort txn (%s) db error - %d %s\n",
                          inst->inst_name, rc, db_strerror(rc));
        }
    } else {
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_db_compact_one_db",
                      "compactdb: compact %s - %d pages freed\n",
                      inst->inst_name, c_data.compact_pages_free);
        if ((rc = dblayer_txn_commit(inst->inst_be, &txn))) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_db_compact_one_db", "compactdb: failed to commit txn (%s) db error - %d %s\n",
                          inst->inst_name, rc, db_strerror(rc));
        }
    }

    return rc;
}

#define DBLAYER_CACHE_DELAY PR_MillisecondsToInterval(250)
int
bdb_rm_db_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_checkpoint)
{
    struct ldbminfo *li = NULL;
    dblayer_private *priv;
    bdb_db_env *pEnv = NULL;
    ldbm_instance *inst = NULL;
    dblayer_handle *handle = NULL;
    char dbName[MAXPATHLEN] = {0};
    char *dbNamep = NULL;
    char *p;
    int dbbasenamelen, dbnamelen;
    int rc = 0;
    DB *db = 0;

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
    priv = li->li_dblayer_private;
    if (NULL == priv) {
        return rc;
    }
    pEnv = (bdb_db_env *)priv->dblayer_env;
    if (NULL == pEnv) { /* db does not exist */
        return rc;
    }
    /* Added for bug 600401. Somehow the checkpoint thread deadlocked on
     index file with this function, index file couldn't be removed on win2k.
     Force a checkpoint here to break deadlock.
    */
    if (0 == no_force_checkpoint) {
	bdb_force_checkpoint(li);
    }

    if (0 == dblayer_get_index_file(be, a, (dbi_db_t**)&db, 0 /* Don't create an index file
                                                   if it does not exist. */)) {
        if (use_lock)
            slapi_rwlock_wrlock(pEnv->bdb_env_lock); /* We will be causing logging activity */
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
            bdb_close_file((DB**)&(handle->dblayer_dbp));

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
            dbNamep = dblayer_get_full_inst_dir(li, inst, dbName, MAXPATHLEN);
            if (dbNamep && *dbNamep) {
                dbbasenamelen = strlen(dbNamep);
                dbnamelen = dbbasenamelen + strlen(a->ai_type) + 6;
                if (dbnamelen > MAXPATHLEN) {
                    dbNamep = (char *)slapi_ch_realloc(dbNamep, dbnamelen);
                }
                p = dbNamep + dbbasenamelen;
                sprintf(p, "%c%s%s", get_sep(dbNamep), a->ai_type, LDBM_FILENAME_SUFFIX);
                rc = bdb_db_remove_ex(pEnv, dbNamep, 0, 0);
                a->ai_dblayer = NULL;
            } else {
                rc = -1;
            }
            if (dbNamep != dbName) {
                slapi_ch_free_string(&dbNamep);
            }
            slapi_ch_free((void **)&handle);
        } else {
            /* no handle to close */
        }
        PR_Unlock(inst->inst_handle_list_mutex);
        if (use_lock)
            slapi_rwlock_unlock(pEnv->bdb_env_lock);
    }

    return rc;
}


/*
 * Transaction stuff. The idea is that the caller doesn't need to
 * know the transaction mechanism underneath (because the caller is
 * typically a few calls up the stack from any DB stuff).
 * Sadly, in slapd there was no handy structure associated with
 * an LDAP operation, and passed around everywhere, so we had
 * to invent the back_txn structure.
 * The lower levels of the back-end look into this structure, and
 * take out the DB_TXN they need.
 */

int
bdb_txn_begin(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    bdb_config *conf = NULL;
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

    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    if (txn) {
        txn->back_txn_txn = NULL;
    }

    if (conf->bdb_enable_transactions) {
        int txn_begin_flags;
        DB_TXN *new_txn_back_txn_txn = NULL;

        bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;
        if (use_lock)
            slapi_rwlock_rdlock(pEnv->bdb_env_lock);
        if (!parent_txn) {
            /* see if we have a stored parent txn */
            back_txn *par_txn_txn = dblayer_get_pvt_txn();
            if (par_txn_txn) {
                parent_txn = par_txn_txn->back_txn_txn;
            }
        }
        if (conf->bdb_txn_wait) {
            txn_begin_flags = 0;
        } else {
            txn_begin_flags = DB_TXN_NOWAIT;
        }
        return_value = TXN_BEGIN(pEnv->bdb_DB_ENV,
                                 (DB_TXN *)parent_txn,
                                 &new_txn_back_txn_txn,
                                 txn_begin_flags);
        if (0 != return_value) {
            if (use_lock)
                slapi_rwlock_unlock(pEnv->bdb_env_lock);
        } else {
            new_txn.back_txn_txn = new_txn_back_txn_txn;
            /* this txn is now our current transaction for current operations
               and new parent for any nested transactions created */
            if (use_lock && log_flush_thread) {
                int txn_id = new_txn_back_txn_txn->id(new_txn_back_txn_txn);
                pthread_mutex_lock(&sync_txn_log_flush);
                txn_in_progress_count++;
                slapi_log_err(SLAPI_LOG_BACKLDBM, "dblayer_txn_begin_ext",
                              "Batchcount: %d, txn_in_progress: %d, curr_txn: %x\n",
                              trans_batch_count, txn_in_progress_count, txn_id);
                pthread_mutex_unlock(&sync_txn_log_flush);
            }
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
bdb_txn_commit(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    bdb_config *conf = NULL;
    dblayer_private *priv = NULL;
    DB_TXN *db_txn = NULL;
    back_txn *cur_txn = NULL;
    int txn_id = 0;
    int txn_batch_slot = 0;

    PR_ASSERT(NULL != li);

    conf = (bdb_config *)li->li_dblayer_config;
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
    if (NULL != db_txn &&
        1 != conf->bdb_stop_threads &&
        priv->dblayer_env &&
        conf->bdb_enable_transactions) {
        bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;
        txn_id = db_txn->id(db_txn);
        return_value = TXN_COMMIT(db_txn, 0);
        /* if we were given a transaction, and it is the same as the
           current transaction in progress, pop it off the stack
           or, if no transaction was given, we must be using the
           current one - must pop it */
        if (!txn || (cur_txn && (cur_txn->back_txn_txn == db_txn))) {
            dblayer_pop_pvt_txn();
        }
        if (txn) {
            /* this handle is no longer value - set it to NULL */
            txn->back_txn_txn = NULL;
        }
        if ((conf->bdb_durable_transactions) && use_lock) {
            if (trans_batch_limit > 0 && log_flush_thread) {
                /* let log_flush thread do the flushing */
                pthread_mutex_lock(&sync_txn_log_flush);
                txn_batch_slot = trans_batch_count++;
                txn_log_flush_pending[txn_batch_slot] = txn_id;
                slapi_log_err(SLAPI_LOG_BACKLDBM, "dblayer_txn_commit_ext",
                              "(before notify): batchcount: %d, txn_in_progress: %d, curr_txn: %x\n",
                              trans_batch_count,
                              txn_in_progress_count, txn_id);
                /*
                 * The log flush thread will periodically flush the txn log,
                 * but in two cases it should be notified to do it immediately:
                 * - the batch limit is passed
                 * - there is no other outstanding txn
                 */
                if (trans_batch_count > trans_batch_limit ||
                    trans_batch_count == txn_in_progress_count)
                {
                    pthread_cond_signal(&sync_txn_log_do_flush);
                }
                /*
                 * We need to wait until the txn has been flushed before continuing
                 * and returning success to the client, nit to vialate durability
                 * PR_WaitCondvar releases and reaquires the lock
                 */
                while (txn_log_flush_pending[txn_batch_slot] == txn_id) {
                    pthread_cond_wait(&sync_txn_log_flush_done, &sync_txn_log_flush);
                }
                txn_in_progress_count--;
                slapi_log_err(SLAPI_LOG_BACKLDBM, "dblayer_txn_commit_ext",
                              "(before unlock): batchcount: %d, txn_in_progress: %d, curr_txn %x\n",
                              trans_batch_count,
                              txn_in_progress_count, txn_id);
                pthread_mutex_unlock(&sync_txn_log_flush);
            } else if (trans_batch_limit == FLUSH_REMOTEOFF) { /* user remotely turned batching off */
                LOG_FLUSH(pEnv->bdb_DB_ENV, 0);
            }
        }
        if (use_lock)
            slapi_rwlock_unlock(pEnv->bdb_env_lock);
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
bdb_txn_abort(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dblayer_private *priv = NULL;
    DB_TXN *db_txn = NULL;
    back_txn *cur_txn = NULL;

    PR_ASSERT(NULL != li);

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
    if (NULL != db_txn &&
        priv->dblayer_env &&
        BDB_CONFIG(li)->bdb_enable_transactions) {
        int txn_id = db_txn->id(db_txn);
        bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;
        if (use_lock && log_flush_thread) {
            pthread_mutex_lock(&sync_txn_log_flush);
            txn_in_progress_count--;
            pthread_mutex_unlock(&sync_txn_log_flush);
            slapi_log_err(SLAPI_LOG_BACKLDBM, "dblayer_txn_abort_ext",
                          "Batchcount: %d, txn_in_progress: %d, curr_txn: %x\n",
                          trans_batch_count, txn_in_progress_count, txn_id);
        }
        return_value = TXN_ABORT(db_txn);
        /* if we were given a transaction, and it is the same as the
           current transaction in progress, pop it off the stack
           or, if no transaction was given, we must be using the
           current one - must pop it */
        if (!txn || (cur_txn && (cur_txn->back_txn_txn == db_txn))) {
            dblayer_pop_pvt_txn();
        }
        if (txn) {
            /* this handle is no longer value - set it to NULL */
            txn->back_txn_txn = NULL;
        }
        if (use_lock)
            slapi_rwlock_unlock(pEnv->bdb_env_lock);
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

uint32_t
bdb_get_optimal_block_size(struct ldbminfo *li)
{
    uint32_t page_size = 0;

    PR_ASSERT(NULL != li);

    page_size = (BDB_CONFIG(li)->bdb_page_size == 0) ? DBLAYER_PAGESIZE : BDB_CONFIG(li)->bdb_page_size;
    if (li->li_dblayer_private->dblayer_idl_divisor == 0) {
        return page_size - DB_EXTN_PAGE_HEADER_SIZE;
    } else {
        return page_size / li->li_dblayer_private->dblayer_idl_divisor;
    }
}



/* code which implements checkpointing and log file truncation */

/*
 * create a thread for perf_threadmain
 */
static int
bdb_start_perf_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread(PR_USER_THREAD,
                                (VFP)(void *)bdb_perf_threadmain, li,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_UNJOINABLE_THREAD,
                                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "bdb_start_perf_thread",
                      "Failed to create database perf thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return_value = -1;
    }
    return return_value;
}


/* Performance thread */
static int
bdb_perf_threadmain(void *param)
{
    struct ldbminfo *li = NULL;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    dblayer_private *priv = li->li_dblayer_private;
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(pEnv);

    while (!BDB_CONFIG(li)->bdb_stop_threads) {
        /* sleep for a while, updating perf counters if we need to */
        bdb_perfctrs_wait(1000, BDB_CONFIG(li)->perf_private, pEnv->bdb_DB_ENV);
    }

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_perf_threadmain", "Leaving bdb_perf_threadmain\n");
    return 0;
}


/*
 * create a thread for bdb_locks_monitoring_threadmain
 */
static int
bdb_start_locks_monitoring_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (li->li_dblock_monitoring) {
        if (NULL == PR_CreateThread(PR_USER_THREAD,
                                    (VFP)(void *)bdb_locks_monitoring_threadmain, li,
                                    PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                    PR_UNJOINABLE_THREAD,
                                    SLAPD_DEFAULT_THREAD_STACKSIZE)) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "bdb_start_locks_monitoring_thread",
                        "Failed to create database locks monitoring thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                        prerr, slapd_pr_strerror(prerr));
            return_value = -1;
        }
    }
    return return_value;
}


/* DB Locks Monitoring thread */
static int
bdb_locks_monitoring_threadmain(void *param)
{
    int ret = 0;
    uint64_t current_locks = 0;
    uint64_t max_locks = 0;
    uint32_t lock_exhaustion = 0;
    PRIntervalTime interval;
    struct ldbminfo *li = NULL;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    dblayer_private *priv = li->li_dblayer_private;
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(pEnv);

    while (!BDB_CONFIG(li)->bdb_stop_threads) {
        if (bdb_uses_locking(pEnv->bdb_DB_ENV)) {
            DB_LOCK_STAT *lockstat = NULL;
            ret = LOCK_STAT(pEnv->bdb_DB_ENV, &lockstat, 0, (void *)slapi_ch_malloc);
            if (0 == ret) {
                current_locks = lockstat->st_nlocks;
                max_locks = lockstat->st_maxlocks;
                if (max_locks){
                    lock_exhaustion = (uint32_t)((double)current_locks / (double)max_locks * 100.0);
                } else {
                    lock_exhaustion = 0;
                }
                if ((li->li_dblock_threshold) &&
                    (lock_exhaustion >= li->li_dblock_threshold)) {
                    slapi_atomic_store_32((int32_t *)&(li->li_dblock_threshold_reached), 1, __ATOMIC_RELAXED);
                } else {
                    slapi_atomic_store_32((int32_t *)&(li->li_dblock_threshold_reached), 0, __ATOMIC_RELAXED);
                }
            }
            slapi_ch_free((void **)&lockstat);
        }
        interval = PR_MillisecondsToInterval(slapi_atomic_load_32((int32_t *)&(li->li_dblock_monitoring_pause), __ATOMIC_RELAXED));
        DS_Sleep(interval);
    }

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_locks_monitoring_threadmain", "Leaving bdb_locks_monitoring_threadmain\n");
    return 0;
}


/*
 * create a thread for deadlock_threadmain
 */
static int
bdb_start_deadlock_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread(PR_USER_THREAD,
                                (VFP)(void *)bdb_deadlock_threadmain, li,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_UNJOINABLE_THREAD,
                                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "bdb_start_deadlock_thread",
                      "Failed to create database deadlock thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return_value = -1;
    }
    return return_value;
}

static const u_int32_t default_flags = DB_NEXT;

/* this is the loop delay - how long after we release the db pages
   until we acquire them again */
#define TXN_TEST_LOOP_WAIT(msecs)                                      \
    do {                                                               \
        if (msecs) {                                                   \
            DS_Sleep(PR_MillisecondsToInterval(slapi_rand() % msecs)); \
        }                                                              \
    } while (0)

/* this is how long we hold the pages open until we close the cursors */
#define TXN_TEST_PAGE_HOLD(msecs)                                      \
    do {                                                               \
        if (msecs) {                                                   \
            DS_Sleep(PR_MillisecondsToInterval(slapi_rand() % msecs)); \
        }                                                              \
    } while (0)

typedef struct txn_test_iter
{
    DB *db;
    DBC *cur;
    uint64_t cnt;
    const char *attr;
    uint32_t flags;
    backend *be;
} bdb_txn_test_iter;

typedef struct txn_test_cfg
{
    PRUint32 hold_msec;
    PRUint32 loop_msec;
    uint32_t flags;
    int use_txn;
    char **indexes;
    int verbose;
} bdb_txn_test_cfg;

static bdb_txn_test_iter *
bdb_new_bdb_txn_test_iter(DB *db, const char *attr, backend *be, uint32_t flags)
{
    bdb_txn_test_iter *tti = (bdb_txn_test_iter *)slapi_ch_malloc(sizeof(bdb_txn_test_iter));
    tti->db = db;
    tti->cur = NULL;
    tti->cnt = 0;
    tti->attr = attr;
    tti->flags = default_flags | flags;
    tti->be = be;
    return tti;
}

static void
bdb_init_bdb_txn_test_iter(bdb_txn_test_iter *tti)
{
    if (tti->cur) {
        if (tti->cur->dbp && (tti->cur->dbp->open_flags == 0x58585858)) {
            /* already closed? */
        } else if (tti->be && (tti->be->be_state != BE_STATE_STARTED)) {
            /* already closed? */
        } else {
            tti->cur->c_close(tti->cur);
        }
        tti->cur = NULL;
    }
    tti->cnt = 0;
    tti->flags = default_flags;
}

static void
bdb_free_bdb_txn_test_iter(bdb_txn_test_iter *tti)
{
    bdb_init_bdb_txn_test_iter(tti);
    slapi_ch_free((void **)&tti);
}

static void
bdb_free_ttilist(bdb_txn_test_iter ***ttilist, uint64_t *tticnt)
{
    if (!ttilist || !*ttilist || !**ttilist) {
        return;
    }
    while (*tticnt > 0) {
        (*tticnt)--;
        bdb_free_bdb_txn_test_iter((*ttilist)[*tticnt]);
    }
    slapi_ch_free((void *)ttilist);
}

static void
bdb_init_ttilist(bdb_txn_test_iter **ttilist, uint64_t tticnt)
{
    if (!ttilist || !*ttilist) {
        return;
    }
    while (tticnt > 0) {
        tticnt--;
        bdb_init_bdb_txn_test_iter(ttilist[tticnt]);
    }
}

static void
bdb_print_ttilist(bdb_txn_test_iter **ttilist, uint64_t tticnt)
{
    while (tticnt > 0) {
        tticnt--;
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_txn_test_threadmain", "attr [%s] cnt [%" PRIu64 "]\n",
                      ttilist[tticnt]->attr, ttilist[tticnt]->cnt);
    }
}

#define TXN_TEST_IDX_OK_IF_NULL "nscpEntryDN"

static void
bdb_txn_test_init_cfg(bdb_txn_test_cfg *cfg)
{
    static char *indexlist = "aci,entryrdn,numsubordinates,uid,ancestorid,objectclass,uniquemember,cn,parentid,nsuniqueid,sn,id2entry," TXN_TEST_IDX_OK_IF_NULL;
    char *indexlist_copy = NULL;

    cfg->hold_msec = getenv(TXN_TEST_HOLD_MSEC) ? atoi(getenv(TXN_TEST_HOLD_MSEC)) : 200;
    cfg->loop_msec = getenv(TXN_TEST_LOOP_MSEC) ? atoi(getenv(TXN_TEST_LOOP_MSEC)) : 10;
    cfg->flags = getenv(TXN_TEST_USE_RMW) ? DB_RMW : 0;
    cfg->use_txn = getenv(TXN_TEST_USE_TXN) ? 1 : 0;
    if (getenv(TXN_TEST_INDEXES)) {
        indexlist_copy = slapi_ch_strdup(getenv(TXN_TEST_INDEXES));
    } else {
        indexlist_copy = slapi_ch_strdup(indexlist);
    }
    cfg->indexes = slapi_str2charray(indexlist_copy, ",");
    slapi_ch_free_string(&indexlist_copy);
    cfg->verbose = getenv(TXN_TEST_VERBOSE) ? 1 : 0;

    slapi_log_err(SLAPI_LOG_ERR, "bdb_txn_test_init_cfg",
                  "Config hold_msec [%d] loop_msec [%d] rmw [%d] txn [%d] indexes [%s]\n",
                  cfg->hold_msec, cfg->loop_msec, cfg->flags, cfg->use_txn,
                  getenv(TXN_TEST_INDEXES) ? getenv(TXN_TEST_INDEXES) : indexlist);
}

static int
bdb_txn_test_threadmain(void *param)
{
    struct ldbminfo *li = NULL;
    Object *inst_obj;
    int rc = 0;
    bdb_txn_test_iter **ttilist = NULL;
    uint64_t tticnt = 0;
    DB_TXN *txn = NULL;
    bdb_txn_test_cfg cfg = {0};
    uint64_t counter = 0;
    char keybuf[8192];
    char databuf[8192];
    int dbattempts = 0;
    int dbmaxretries = 3;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    dblayer_private *priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    INCR_THREAD_COUNT(pEnv);

    bdb_txn_test_init_cfg(&cfg);

    if(!BDB_CONFIG(li)->bdb_enable_transactions) {
        goto end;
    }

wait_for_init:
    bdb_free_ttilist(&ttilist, &tticnt);
    DS_Sleep(PR_MillisecondsToInterval(1000));
    if (BDB_CONFIG(li)->bdb_stop_threads) {
        goto end;
    }
    dbattempts++;
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        char **idx = NULL;
        ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);
        backend *be = inst->inst_be;

        if (be->be_state != BE_STATE_STARTED) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_txn_test_threadmain", "Backend not started, retrying\n");
            object_release(inst_obj);
            goto wait_for_init;
        }

        for (idx = cfg.indexes; idx && *idx; ++idx) {
            DB *db = NULL;
            if (be->be_state != BE_STATE_STARTED) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_txn_test_threadmain", "Backend not started, retrying\n");
                object_release(inst_obj);
                goto wait_for_init;
            }

            if (!strcmp(*idx, "id2entry")) {
                (void) dblayer_get_id2entry(be, (dbi_db_t**)&db);
                if (db == NULL) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "bdb_txn_test_threadmain", "id2entry database not found or not ready yet, retrying\n");
                    object_release(inst_obj);
                    goto wait_for_init;
                }
            } else {
                struct attrinfo *ai = NULL;
                ainfo_get(be, *idx, &ai);
                if (NULL == ai) {
                    if (dbattempts >= dbmaxretries) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "bdb_txn_test_threadmain", "Index [%s] not found or not ready yet, skipping\n",
                                      *idx);
                        continue;
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "bdb_txn_test_threadmain", "Index [%s] not found or not ready yet, retrying\n",
                                      *idx);
                        object_release(inst_obj);
                        goto wait_for_init;
                    }
                }
                if (dblayer_get_index_file(be, ai, (dbi_db_t**)&db, 0) || (NULL == db)) {
                    if ((NULL == db) && strcasecmp(*idx, TXN_TEST_IDX_OK_IF_NULL)) {
                        if (dbattempts >= dbmaxretries) {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "bdb_txn_test_threadmain", "Database file for index [%s] not found or not ready yet, skipping\n",
                                          *idx);
                            continue;
                        } else {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "bdb_txn_test_threadmain", "Database file for index [%s] not found or not ready yet, retrying\n",
                                          *idx);
                            object_release(inst_obj);
                            goto wait_for_init;
                        }
                    }
                }
            }
            if (db) {
                ttilist = (bdb_txn_test_iter **)slapi_ch_realloc((char *)ttilist, sizeof(bdb_txn_test_iter *) * (tticnt + 1));
                ttilist[tticnt++] = bdb_new_bdb_txn_test_iter(db, *idx, be, cfg.flags);
            }
        }
    }

    slapi_log_err(SLAPI_LOG_ERR, "bdb_txn_test_threadmain", "Starting main txn stress loop\n");
    bdb_print_ttilist(ttilist, tticnt);

    while (!BDB_CONFIG(li)->bdb_stop_threads) {
    retry_txn:
        bdb_init_ttilist(ttilist, tticnt);
        if (txn) {
            TXN_ABORT(txn);
            txn = NULL;
        }
        if (cfg.use_txn) {
            rc = TXN_BEGIN(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV, NULL, &txn, 0);
            if (rc || !txn) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_txn_test_threadmain", "Failed to create a new transaction, err=%d (%s)\n",
                              rc, dblayer_strerror(rc));
            }
        } else {
            rc = 0;
        }
        if (!rc) {
            DBT key;
            DBT data;
            uint64_t ii;
            uint64_t donecnt = 0;
            uint64_t cnt = 0;

            /* phase 1 - open a cursor to each db */
            if (cfg.verbose) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_txn_test_threadmain", "Starting [%" PRIu64 "] indexes\n", tticnt);
            }
            for (ii = 0; ii < tticnt; ++ii) {
                bdb_txn_test_iter *tti = ttilist[ii];

            retry_cursor:
                if (BDB_CONFIG(li)->bdb_stop_threads) {
                    goto end;
                }
                if (tti->be->be_state != BE_STATE_STARTED) {
                    if (txn) {
                        TXN_ABORT(txn);
                        txn = NULL;
                    }
                    goto wait_for_init;
                }
                if (tti->db->open_flags == 0xdbdbdbdb) {
                    if (txn) {
                        TXN_ABORT(txn);
                        txn = NULL;
                    }
                    goto wait_for_init;
                }
                rc = tti->db->cursor(tti->db, txn, &tti->cur, 0);
                if (DB_LOCK_DEADLOCK == rc) {
                    if (cfg.verbose) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "bdb_txn_test_threadmain", "Cursor create deadlock - retry\n");
                    }
                    if (cfg.use_txn) {
                        goto retry_txn;
                    } else {
                        goto retry_cursor;
                    }
                } else if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "bdb_txn_test_threadmain", "Failed to create a new cursor, err=%d (%s)\n",
                                  rc, dblayer_strerror(rc));
                }
            }

            memset(&key, 0, sizeof(key));
            key.flags = DB_DBT_USERMEM;
            key.data = keybuf;
            key.ulen = sizeof(keybuf);
            memset(&data, 0, sizeof(data));
            data.flags = DB_DBT_USERMEM;
            data.data = databuf;
            data.ulen = sizeof(databuf);
            /* phase 2 - iterate over each cursor at the same time until
               1) get error
               2) get deadlock
               3) all cursors are exhausted
            */
            while (donecnt < tticnt) {
                for (ii = 0; ii < tticnt; ++ii) {
                    bdb_txn_test_iter *tti = ttilist[ii];
                    if (tti->cur) {
                    retry_get:
                        if (BDB_CONFIG(li)->bdb_stop_threads) {
                            goto end;
                        }
                        if (tti->be->be_state != BE_STATE_STARTED) {
                            if (txn) {
                                TXN_ABORT(txn);
                                txn = NULL;
                            }
                            goto wait_for_init;
                        }
                        if (tti->db->open_flags == 0xdbdbdbdb) {
                            if (txn) {
                                TXN_ABORT(txn);
                                txn = NULL;
                            }
                            goto wait_for_init;
                        }
                        rc = tti->cur->c_get(tti->cur, &key, &data, tti->flags);
                        if (DB_LOCK_DEADLOCK == rc) {
                            if (cfg.verbose) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "bdb_txn_test_threadmain", "Cursor get deadlock - retry\n");
                            }
                            if (cfg.use_txn) {
                                goto retry_txn;
                            } else {
                                goto retry_get;
                            }
                        } else if (DB_NOTFOUND == rc) {
                            donecnt++;                         /* ran out of this one */
                            tti->flags = DB_FIRST | cfg.flags; /* start over until all indexes are done */
                        } else if (rc) {
                            if ((DB_BUFFER_SMALL != rc) || cfg.verbose) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "bdb_txn_test_threadmain", "Failed to read a cursor, err=%d (%s)\n",
                                              rc, dblayer_strerror(rc));
                            }
                            tti->cur->c_close(tti->cur);
                            tti->cur = NULL;
                            donecnt++;
                        } else {
                            tti->cnt++;
                            tti->flags = default_flags | cfg.flags;
                            cnt++;
                        }
                    }
                }
            }
            TXN_TEST_PAGE_HOLD(cfg.hold_msec);
            /*bdb_print_ttilist(ttilist, tticnt);*/
            bdb_init_ttilist(ttilist, tticnt);
            if (cfg.verbose) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_txn_test_threadmain", "Finished [%" PRIu64 "] indexes [%" PRIu64 "] records\n", tticnt, cnt);
            }
            TXN_TEST_LOOP_WAIT(cfg.loop_msec);
        } else {
            TXN_TEST_LOOP_WAIT(cfg.loop_msec);
        }
        counter++;
        if (!(counter % 40)) {
            /* some operations get completely stuck - so every once in a while,
               pause to allow those ops to go through */
            DS_Sleep(PR_SecondsToInterval(1));
        }
    }

end:
    slapi_ch_array_free(cfg.indexes);
    bdb_free_ttilist(&ttilist, &tticnt);
    if (txn) {
        TXN_ABORT(txn);
    }
    DECR_THREAD_COUNT(pEnv);
    return 0;
}

/*
 * create a thread for transaction deadlock testing
 */
static int
bdb_start_txn_test_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread(PR_USER_THREAD,
                                (VFP)(void *)bdb_txn_test_threadmain, li,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_UNJOINABLE_THREAD,
                                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "bdb_start_txn_test_thread",
                      "Failed to create txn test thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return_value = -1;
    }
    return return_value;
}

/* deadlock thread main function */

static int
bdb_deadlock_threadmain(void *param)
{
    int rval = -1;
    struct ldbminfo *li = NULL;
    PRIntervalTime interval; /*NSPR timeout stuffy*/
    u_int32_t flags = 0;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    dblayer_private *priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    INCR_THREAD_COUNT(pEnv);

    interval = PR_MillisecondsToInterval(100);
    while (!BDB_CONFIG(li)->bdb_stop_threads) {
        if (BDB_CONFIG(li)->bdb_enable_transactions) {
            DB_ENV *db_env = ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV;
            u_int32_t deadlock_policy = BDB_CONFIG(li)->bdb_deadlock_policy;

            if (bdb_uses_locking(db_env) && (deadlock_policy > DB_LOCK_NORUN)) {
                int rejected = 0;

                rval = db_env->lock_detect(db_env, flags, deadlock_policy, &rejected);
                if (rval != 0) {
                    slapi_log_err(SLAPI_LOG_CRIT,
                                  "bdb_deadlock_threadmain", "Serious Error---Failed in deadlock detect (aborted at 0x%x), err=%d (%s)\n",
                                  rejected, rval, dblayer_strerror(rval));
                } else if (rejected) {
                    slapi_log_err(SLAPI_LOG_TRACE, "bdb_deadlock_threadmain", "Found and rejected %d lock requests\n", rejected);
                }
            }
        }
        DS_Sleep(interval);
    }

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_deadlock_threadmain", "Leaving bdb_deadlock_threadmain\n");
    return 0;
}

#define bdb_checkpoint_debug_message(debug, ...)                       \
    if (debug) {                                                   \
        slapi_log_err(SLAPI_LOG_DEBUG, "CHECKPOINT", __VA_ARGS__); \
    }

/* this thread tries to do two things:
    1. catch a group of transactions that are pending allowing a worker thread
       to work
    2. flush any left over transactions ( a single transaction for example)
*/

static int
bdb_start_log_flush_thread(struct ldbminfo *li)
{
    int return_value = 0;
    int max_threads = config_get_threadnumber();

    if ((BDB_CONFIG(li)->bdb_durable_transactions) &&
        (BDB_CONFIG(li)->bdb_enable_transactions) && (trans_batch_limit > 0))
    {
        /* initialize the synchronization objects for the log_flush and worker threads */
        pthread_condattr_t condAttr;

        pthread_mutex_init(&sync_txn_log_flush, NULL);
        pthread_condattr_init(&condAttr);
        pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
        pthread_cond_init(&sync_txn_log_do_flush, &condAttr);
        pthread_cond_init(&sync_txn_log_flush_done, NULL);
        pthread_condattr_destroy(&condAttr); /* no longer needed */

        txn_log_flush_pending = (int *)slapi_ch_malloc(max_threads * sizeof(int));
        log_flush_thread = PR_TRUE;
        if (NULL == PR_CreateThread(PR_USER_THREAD,
                                    (VFP)(void *)bdb_log_flush_threadmain, li,
                                    PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                    PR_UNJOINABLE_THREAD,
                                    SLAPD_DEFAULT_THREAD_STACKSIZE)) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_start_log_flush_thread", "Failed to create database log flush thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
            return_value = -1;
        }
    }
    return return_value;
}

/* this thread tries to do two things:
    1. catch a group of transactions that are pending allowing a worker thread
       to work
    2. flush any left over transactions ( a single transaction for example)
*/

static int
bdb_log_flush_threadmain(void *param)
{
    PRIntervalTime interval_flush, interval_def;
    PRIntervalTime last_flush = 0;
    int i;
    int do_flush = 0;

    PR_ASSERT(NULL != param);
    struct ldbminfo *li = (struct ldbminfo *)param;
    dblayer_private *priv = li->li_dblayer_private;
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    INCR_THREAD_COUNT(pEnv);

    interval_flush = PR_MillisecondsToInterval(trans_batch_txn_min_sleep);
    interval_def = PR_MillisecondsToInterval(300); /*used while no txn or txn batching */
    /* LK this is only needed if online change of
     * of txn config is supported ???
     */
    while ((!BDB_CONFIG(li)->bdb_stop_threads) && (log_flush_thread)) {
        if (BDB_CONFIG(li)->bdb_enable_transactions) {
            if (trans_batch_limit > 0) {
                /* synchronize flushing thread with workers */
                pthread_mutex_lock(&sync_txn_log_flush);
                if (!log_flush_thread) {
                    /* batch transactions was disabled while waiting for the lock */
                    pthread_mutex_unlock(&sync_txn_log_flush);
                    break;
                }
                slapi_log_err(SLAPI_LOG_BACKLDBM, "bdb_log_flush_threadmain", "(in loop): batchcount: %d, "
                                                                          "txn_in_progress: %d\n",
                              trans_batch_count, txn_in_progress_count);
                /*
                 * if here, do flush the txn logs if any of the following conditions are met
                 * - batch limit exceeded
                 * - no more active transaction, no need to wait
                 * - do_flush indicate that the max waiting interval is exceeded
                 */
                if (trans_batch_count >= trans_batch_limit || trans_batch_count >= txn_in_progress_count || do_flush) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "bdb_log_flush_threadmain", "(working): batchcount: %d, "
                                                                              "txn_in_progress: %d\n",
                                  trans_batch_count, txn_in_progress_count);
                    LOG_FLUSH(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV, 0);
                    for (i = 0; i < trans_batch_count; i++) {
                        txn_log_flush_pending[i] = 0;
                    }
                    trans_batch_count = 0;
                    last_flush = PR_IntervalNow();
                    do_flush = 0;
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "bdb_log_flush_threadmain", "(before notify): batchcount: %d, "
                                                                              "txn_in_progress: %d\n",
                                  trans_batch_count, txn_in_progress_count);
                    pthread_cond_broadcast(&sync_txn_log_flush_done);
                }
                /* wait until flushing conditions are met */
                while ((trans_batch_count == 0) ||
                       (trans_batch_count < trans_batch_limit && trans_batch_count < txn_in_progress_count))
                {
                    struct timespec current_time = {0};
                    /* convert milliseconds to nano seconds */
                    int32_t nano_sec_sleep = trans_batch_txn_max_sleep * 1000000;
                    if (BDB_CONFIG(li)->bdb_stop_threads)
                        break;
                    if (PR_IntervalNow() - last_flush > interval_flush) {
                        do_flush = 1;
                        break;
                    }
                    clock_gettime(CLOCK_MONOTONIC, &current_time);
                    if (current_time.tv_nsec + nano_sec_sleep > 1000000000) {
                        /* nano sec will overflow, just bump the seconds */
                        current_time.tv_sec++;
                    } else {
                        current_time.tv_nsec += nano_sec_sleep;
                    }
                    pthread_cond_timedwait(&sync_txn_log_do_flush, &sync_txn_log_flush, &current_time);
                }
                pthread_mutex_unlock(&sync_txn_log_flush);
                slapi_log_err(SLAPI_LOG_BACKLDBM, "bdb_log_flush_threadmain", "(wakeup): batchcount: %d, "
                                                                          "txn_in_progress: %d\n",
                              trans_batch_count, txn_in_progress_count);
            } else {
                DS_Sleep(interval_def);
            }
        } else {
            DS_Sleep(interval_def);
        }
    }

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_log_flush_threadmain", "Leaving bdb_log_flush_threadmain\n");
    return 0;
}

/*
 * Get the time in seconds when the compaction should occur
 */
static time_t
bdb_get_tod_expiration(char *expire_time)
{
    time_t todays_elapsed_time, now = time(NULL);
    struct tm *tm_struct = localtime(&now);
    char hour_str[3] = {0};
    char min_str[3] = {0};
    char *s = expire_time;
    char *endp = NULL;
    int32_t hour, min, expiring_time;

    /* Get today's start time */
    todays_elapsed_time = (tm_struct->tm_hour * 3600) + (tm_struct->tm_min * 60) + (tm_struct->tm_sec);

    /* Get the hour and minute and calculate the expiring TOD.  The time was
     * already validated in bdb_config.c:  HH:MM */
    hour_str[0] = *s++;
    hour_str[1] = *s++;
    s++;  /* skip colon */
    min_str[0] = *s++;
    min_str[1] = *s++;
    hour = strtoll(hour_str, &endp, 10);
    min = strtoll(min_str, &endp, 10);
    expiring_time = (hour * 60 * 60) + (min * 60);

    /* Calculate the time in seconds when the compaction should start, midnight
     * requires special treatment (for both current time and configured TOD) */
    if (expiring_time == 0) {
        /* Compaction TOD configured for midnight */
        if (todays_elapsed_time == 0) {
            /* It's currently midnight, compact now! */
            return 0;
        } else {
            /* Return the time until it's midnight */
            return _SEC_PER_DAY - todays_elapsed_time;
        }
    } else if (todays_elapsed_time == 0) {
        /* It's currently midnight, just use the configured TOD */
        return expiring_time;
    } else if (todays_elapsed_time > expiring_time) {
        /* We missed TOD today, do it tomorrow */
        return _SEC_PER_DAY - (todays_elapsed_time - expiring_time);
    } else {
        /* Compaction is coming up later today */
        return expiring_time - todays_elapsed_time;
    }
}

static void
bdb_compact(time_t when, void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    Object *inst_obj;
    ldbm_instance *inst;
    DB *db = NULL;
    int rc = 0;

    for (inst_obj = objset_first_obj(li->li_instance_set);
         inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        rc = dblayer_get_id2entry(inst->inst_be, (dbi_db_t **)&db);
        if (!db || rc) {
            continue;
        }
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_compact", "Compacting DB start: %s\n",
                      inst->inst_name);

        rc = bdb_db_compact_one_db(db, inst);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_compact",
                          "Failed to compact id2entry for %s; db error - %d %s\n",
                          inst->inst_name, rc, db_strerror(rc));
            break;
        }

        /* Time to compact the DB's */
        bdb_force_checkpoint(li);
        bdb_do_compact(li, PR_FALSE);
        bdb_force_checkpoint(li);

        /* Now reset the timer and compacting flag */
        rc = bdb_db_compact_one_db(db, inst);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_compact",
                          "Failed to compact for %s; db error - %d %s\n",
                          inst->inst_name, rc, db_strerror(rc));
            break;
        }
    }
    compaction_scheduled = PR_FALSE;
}

/*
 * create a thread for checkpoint_threadmain
 */
static int
bdb_start_checkpoint_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread(PR_USER_THREAD,
                                (VFP)(void *)bdb_checkpoint_threadmain, li,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_UNJOINABLE_THREAD,
                                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_start_checkpoint_thread", "Failed to create database checkpoint thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return_value = -1;
    }
    return return_value;
}

/*
 * checkpoint thread -- borrow the timing for compacting id2entry, and eventually changelog, as well.
 */
static int
bdb_checkpoint_threadmain(void *param)
{
    PRIntervalTime interval;
    int rval = -1;
    struct ldbminfo *li = NULL;
    int debug_checkpointing = 0;
    char *home_dir = NULL;
    char **list = NULL;
    char **listp = NULL;
    bdb_db_env *penv = NULL;
    struct timespec checkpoint_expire;
    struct timespec compactdb_expire;
    time_t compactdb_interval_update = 0;
    time_t checkpoint_interval_update = 0;
    time_t compactdb_interval = 0;
    time_t checkpoint_interval = 0;
    int32_t compactdb_time = 0;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    dblayer_private *priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    INCR_THREAD_COUNT(pEnv);

    interval = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL * 10);
    home_dir = bdb_get_home_dir(li, NULL);
    if (NULL == home_dir || '\0' == *home_dir) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_checkpoint_threadmain", "Failed due to missing db home directory info\n");
        goto error_return;
    }

    /* work around a problem with newly created environments */
    bdb_force_checkpoint(li);

    PR_Lock(li->li_config_mutex);
    checkpoint_interval = (time_t)BDB_CONFIG(li)->bdb_checkpoint_interval;
    compactdb_interval = (time_t)BDB_CONFIG(li)->bdb_compactdb_interval;
    penv = (bdb_db_env *)priv->dblayer_env;
    debug_checkpointing = BDB_CONFIG(li)->bdb_debug_checkpointing;
    PR_Unlock(li->li_config_mutex);

    /* assumes bdb_force_checkpoint worked */
    /*
     * Importantly, the use of this api is not affected by backwards time steps
     * and the like. Because this use relative system time, rather than utc,
     * it makes it much more reliable to run.
     */
    slapi_timespec_expire_at(compactdb_interval, &compactdb_expire);
    slapi_timespec_expire_at(checkpoint_interval, &checkpoint_expire);

    while (!BDB_CONFIG(li)->bdb_stop_threads) {
        PR_Lock(li->li_config_mutex);
        checkpoint_interval_update = (time_t)BDB_CONFIG(li)->bdb_checkpoint_interval;
        compactdb_interval_update = (time_t)BDB_CONFIG(li)->bdb_compactdb_interval;
        PR_Unlock(li->li_config_mutex);

        if (compactdb_interval_update != compactdb_interval) {
            /* Compact interval was changed, so reset the timer */
            slapi_timespec_expire_at(compactdb_interval_update, &compactdb_expire);
        }

        /* Sleep for a while ...
         * Why aren't we sleeping exactly the right amount of time ?
         * Answer---because the interval might be changed after the server
         * starts up */
        DS_Sleep(interval);

        if (0 == BDB_CONFIG(li)->bdb_enable_transactions) {
            continue;
        }

        /* If the checkpoint has been updated OR we have expired */
        if (checkpoint_interval != checkpoint_interval_update ||
            slapi_timespec_expire_check(&checkpoint_expire) == TIMER_EXPIRED) {

            /* If our interval has changed, update it. */
            checkpoint_interval = checkpoint_interval_update;

            if (!bdb_uses_transactions(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV)) {
                continue;
            }

            /* now checkpoint */
            bdb_checkpoint_debug_message(debug_checkpointing,
                                     "bdb_checkpoint_threadmain - Starting checkpoint\n");
            rval = bdb_txn_checkpoint(li, (bdb_db_env *)priv->dblayer_env,
                                          PR_TRUE, PR_FALSE);
            bdb_checkpoint_debug_message(debug_checkpointing,
                                     "bdb_checkpoint_threadmain - Checkpoint Done\n");
            if (rval != 0) {
                /* bad error */
                slapi_log_err(SLAPI_LOG_CRIT,
                              "bdb_checkpoint_threadmain", "Serious Error---Failed to checkpoint database, "
                                                       "err=%d (%s)\n",
                              rval, dblayer_strerror(rval));
                if (LDBM_OS_ERR_IS_DISKFULL(rval)) {
                    operation_out_of_disk_space();
                    goto error_return;
                }
            }

            rval = LOG_ARCHIVE(penv->bdb_DB_ENV, &list,
                               DB_ARCH_ABS, (void *)slapi_ch_malloc);
            if (rval) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_checkpoint_threadmain",
                              "log archive failed - %s (%d)\n",
                              dblayer_strerror(rval), rval);
            } else {
                for (listp = list; listp && *listp != NULL; ++listp) {
                    if (BDB_CONFIG(li)->bdb_circular_logging) {
                        bdb_checkpoint_debug_message(debug_checkpointing,
                                                 "Deleting %s\n", *listp);
                        unlink(*listp);
                    } else {
                        char new_filename[MAXPATHLEN];
                        PR_snprintf(new_filename, sizeof(new_filename),
                                    "%s.old", *listp);
                        bdb_checkpoint_debug_message(debug_checkpointing,
                                                 "Renaming %s -> %s\n", *listp, new_filename);
                        if (rename(*listp, new_filename) != 0) {
                            slapi_log_err(SLAPI_LOG_ERR, "bdb_checkpoint_threadmain", "Failed to rename log (%s) to (%s)\n",
                                          *listp, new_filename);
                            rval = -1;
                            goto error_return;
                        }
                    }
                }
                slapi_ch_free((void **)&list);
                /* Note: references inside the returned memory need not be
                 * individually freed. */
            }
            slapi_timespec_expire_at(checkpoint_interval, &checkpoint_expire);
        }

        /* Compacting DB borrowing the timing of the log flush */

        /*
         * Remember that if compactdb_interval is 0, timer_expired can
         * never occur unless the value in compactdb_interval changes.
         *
         * this could have been a bug in fact, where compactdb_interval
         * was 0, if you change while running it would never take effect ....
         */
        if (compactdb_interval_update != compactdb_interval ||
            (slapi_timespec_expire_check(&compactdb_expire) == TIMER_EXPIRED && !compaction_scheduled))
        {
            /* Get the time in second when the compaction should occur */
            PR_Lock(li->li_config_mutex);
            compactdb_time = bdb_get_tod_expiration((char *)BDB_CONFIG(li)->bdb_compactdb_time);
            PR_Unlock(li->li_config_mutex);

            /* Start compaction event */
            compaction_scheduled = PR_TRUE;
            slapi_eq_once_rel(bdb_compact, (void *)li, slapi_current_rel_time_t() + compactdb_time);

            /* reset interval timer */
            compactdb_interval = compactdb_interval_update;
            slapi_timespec_expire_at(compactdb_interval, &compactdb_expire);
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_checkpoint_threadmain", "Check point before leaving\n");
    rval = bdb_force_checkpoint(li);
error_return:

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_checkpoint_threadmain", "Leaving bdb_checkpoint_threadmain\n");
    return rval;
}

/*
 * create a thread for trickle_threadmain
 */
static int
bdb_start_trickle_thread(struct ldbminfo *li)
{
    int return_value = 0;
    bdb_config *priv = (bdb_config *)li->li_dblayer_config;

    if (priv->bdb_trickle_percentage == 0)
        return return_value;

    if (NULL == PR_CreateThread(PR_USER_THREAD,
                                (VFP)(void *)bdb_trickle_threadmain, li,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_UNJOINABLE_THREAD,
                                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "bdb_start_trickle_thread",
                      "Failed to create database trickle thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return_value = -1;
    }
    return return_value;
}

static int
bdb_trickle_threadmain(void *param)
{
    PRIntervalTime interval; /*NSPR timeout stuffy*/
    int rval = -1;
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;
    int debug_checkpointing = 0;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo *)param;

    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    bdb_db_env *pEnv = (bdb_db_env *)priv->dblayer_env;

    INCR_THREAD_COUNT(pEnv);

    interval = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL);
    debug_checkpointing = BDB_CONFIG(li)->bdb_debug_checkpointing;
    while (!BDB_CONFIG(li)->bdb_stop_threads) {
        DS_Sleep(interval); /* 622855: wait for other threads fully started */
        if (BDB_CONFIG(li)->bdb_enable_transactions) {
            if (bdb_uses_mpool(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV) &&
                (0 != BDB_CONFIG(li)->bdb_trickle_percentage)) {
                int pages_written = 0;
                if ((rval = MEMP_TRICKLE(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV,
                                         BDB_CONFIG(li)->bdb_trickle_percentage,
                                         &pages_written)) != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "bdb_trickle_threadmain", "Serious Error---Failed to trickle, err=%d (%s)\n",
                                  rval, dblayer_strerror(rval));
                }
                if (pages_written > 0) {
                    bdb_checkpoint_debug_message(debug_checkpointing, "bdb_trickle_threadmain - Trickle thread wrote %d pages\n",
                                             pages_written);
                }
            }
        }
    }

    DECR_THREAD_COUNT(pEnv);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_trickle_threadmain", "Leaving bdb_trickle_threadmain priv\n");
    return 0;
}



/* Helper function for monitor stuff */
int
bdb_memp_stat(struct ldbminfo *li, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp)
{
    DB_ENV *env = NULL;

    PR_ASSERT(NULL != li);

    dblayer_private *priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    env = ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV;
    PR_ASSERT(NULL != env);

    return MEMP_STAT(env, gsp, fsp, 0, (void *)slapi_ch_malloc);
}

/* import wants this one */
int
bdb_memp_stat_instance(ldbm_instance *inst, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp)
{
    DB_ENV *env = NULL;

    PR_ASSERT(NULL != inst);

    if (((bdb_db_env *)inst->inst_db)->bdb_DB_ENV) {
        env = ((bdb_db_env *)inst->inst_db)->bdb_DB_ENV;
    } else {
        dblayer_private *priv = inst->inst_li->li_dblayer_private;
        PR_ASSERT(NULL != priv);
        env = ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV;
    }
    PR_ASSERT(NULL != env);

    return MEMP_STAT(env, gsp, fsp, 0, (void *)slapi_ch_malloc);
}

/* Helper functions for recovery */

#define DB_LINE_LENGTH 80

static int
bdb_commit_good_database(bdb_config *conf, int mode)
{
    /* Write out the guard file */
    char filename[MAXPATHLEN];
    char line[DB_LINE_LENGTH * 2];
    PRFileDesc *prfd;
    int return_value = 0;
    int num_bytes;

    PR_snprintf(filename, sizeof(filename), "%s/guardian", conf->bdb_home_directory);

    prfd = PR_Open(filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, mode);
    if (NULL == prfd) {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_commit_good_database", "Failed to write guardian file %s, database corruption possible" SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
                      filename, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        return -1;
    }
    PR_snprintf(line, sizeof(line), "cachesize:%lu\nncache:%d\nversion:%d\nlocks:%d\n",
                (long unsigned int)conf->bdb_cachesize, conf->bdb_ncache, DB_VERSION_MAJOR, conf->bdb_lock_config);
    num_bytes = strlen(line);
    return_value = slapi_write_buffer(prfd, line, num_bytes);
    if (return_value != num_bytes) {
        goto error;
    }
    return_value = PR_Close(prfd);
    if (PR_SUCCESS == return_value) {
        return 0;
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_commit_good_database",
                      "Failed to write guardian file, database corruption possible\n");
        (void)PR_Delete(filename);
        return -1;
    }
error:
    (void)PR_Close(prfd);
    (void)PR_Delete(filename);
    return -1;
}

/* read the guardian file from db/ and possibly recover the database */
static int
bdb_read_metadata(struct ldbminfo *li)
{
    char filename[MAXPATHLEN];
    char *buf;
    char *thisline;
    char *nextline;
    char **dirp;
    PRFileDesc *prfd;
    PRFileInfo64 prfinfo;
    int return_value = 0;
    PRInt32 byte_count = 0;
    char attribute[513];
    char value[129], delimiter;
    int number = 0;
    bdb_config *conf = (bdb_config *)li->li_dblayer_config;
    dblayer_private *priv = li->li_dblayer_private;

    /* bdb_recovery_required is initialized in dblayer_init;
     * and might be set 1 in bdb_check_db_version;
     * we don't want to override it
     * priv->bdb_recovery_required = 0; */
    conf->bdb_previous_cachesize = 0;
    conf->bdb_previous_ncache = 0;
    conf->bdb_previous_lock_config = 0;
    /* Open the guard file and read stuff, then delete it */
    PR_snprintf(filename, sizeof(filename), "%s/guardian", conf->bdb_home_directory);

    memset(&prfinfo, '\0', sizeof(PRFileInfo64));
    (void)PR_GetFileInfo64(filename, &prfinfo);

    prfd = PR_Open(filename, PR_RDONLY, priv->dblayer_file_mode);
    if (NULL == prfd || 0 == prfinfo.size) {
        /* file empty or not present--means the database needs recovered */
        /* Note count is correctly zerod! */
        int count = 0;
        for (dirp = conf->bdb_data_directories; dirp && *dirp; dirp++) {
            bdb_count_dbfiles_in_dir(*dirp, &count, 1 /* recurse */);
            if (count > 0) {
                conf->bdb_recovery_required = 1;
                return 0;
            }
        }
        return 0; /* no files found; no need to run recover start */
    }
    /* So, we opened the file, now let's read the cache size and version stuff
     */
    buf = slapi_ch_calloc(1, prfinfo.size + 1);
    byte_count = slapi_read_buffer(prfd, buf, prfinfo.size);
    if (byte_count < 0) {
        /* something bad happened while reading */
        conf->bdb_recovery_required = 1;
    } else {
        buf[byte_count] = '\0';
        thisline = buf;
        while (1) {
            /* Find the end of the line */
            nextline = strchr(thisline, '\n');
            if (NULL != nextline) {
                *nextline++ = '\0';
                while ('\n' == *nextline) {
                    nextline++;
                }
            }
            sscanf(thisline, "%512[a-z]%c%128s", attribute, &delimiter, value);
            if (0 == strcmp("cachesize", attribute)) {
                conf->bdb_previous_cachesize = strtoul(value, NULL, 10);
            } else if (0 == strcmp("ncache", attribute)) {
                number = atoi(value);
                conf->bdb_previous_ncache = number;
            } else if (0 == strcmp("version", attribute)) {
            } else if (0 == strcmp("locks", attribute)) {
                number = atoi(value);
                conf->bdb_previous_lock_config = number;
            }
            if (NULL == nextline || '\0' == *nextline) {
                /* Nothing more to read */
                break;
            }
            thisline = nextline;
        }
    }
    slapi_ch_free((void **)&buf);
    (void)PR_Close(prfd);
    return_value = PR_Delete(filename); /* very important that this happen ! */
    if (PR_SUCCESS != return_value) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "bdb_read_metadata", "Failed to delete guardian file, "
                                       "database corruption possible\n");
    }
    return return_value;
}

/* handy routine for checkpointing the db */
static int
bdb_force_checkpoint(struct ldbminfo *li)
{
    int ret = 0, i;
    dblayer_private *priv = li->li_dblayer_private;
    bdb_db_env *pEnv;

    if (NULL == priv || NULL == priv->dblayer_env) {
        /* already terminated.  nothing to do */
        return -1;
    }

    pEnv = (bdb_db_env *)priv->dblayer_env;

    if (BDB_CONFIG(li)->bdb_enable_transactions) {

        slapi_log_err(SLAPI_LOG_TRACE, "bdb_force_checkpoint", "Checkpointing database ...\n");

        /*
     * DB workaround. Newly created environments do not know what the
     * previous checkpoint LSN is. The default LSN of [0][0] would
     * cause us to read all log files from very beginning during a
     * later recovery. Taking two checkpoints solves the problem.
     */

        for (i = 0; i < 2; i++) {
            ret = bdb_txn_checkpoint(li, pEnv, PR_FALSE, PR_TRUE);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_force_checkpoint", "Checkpoint FAILED, error %s (%d)\n",
                              dblayer_strerror(ret), ret);
                break;
            }
        }
    }

    return ret;
}

/* routine to force all existing transaction logs to be cleared
 * This is necessary if the transaction logs can contain references
 * to no longer existing files, but would be processed in a fatal
 * recovery (like in backup/restore).
 * There is no straight forward way to do this, but the following
 * scenario should work:
 *
 * 1. check for no longer needed transaction logs by
 *      calling log_archive()
 * 2. delete these logs (1and2 similar to checkpointing
 * 3. force a checkpoint
 * 4. use log_printf() to write a "comment" to the current txn log
 *      force a checkpoint
 *      this could be done by writing once about 10MB or
 *      by writing smaller chunks in a loop
 * 5. force a checkpoint and check again
 *  if a txn log to remove exists remove it and we are done
 *  else repeat step 4
 *
 * NOTE: double check if force_checkpoint also does remove txn files
 * then the check would have to be modified
 */
static int
bdb_force_logrenewal(struct ldbminfo *li)
{
    return 0;
}

static int
_dblayer_delete_aux_dir(struct ldbminfo *li, char *path)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char filename[MAXPATHLEN];
    dblayer_private *priv = NULL;
    bdb_db_env *pEnv = NULL;
    int rc = -1;

    if (NULL == li || NULL == path) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "_dblayer_delete_aux_dir", "Invalid LDBM info (0x%p) "
                                                 "or path (0x%p)\n",
                      li, path);
        return rc;
    }
    priv = li->li_dblayer_private;
    if (priv) {
        pEnv = (bdb_db_env *)priv->dblayer_env;
    }
    dirhandle = PR_OpenDir(path);
    if (!dirhandle) {
        return 0; /* The dir does not exist. */
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle,
                                          PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (!direntry->name)
            break;
        PR_snprintf(filename, sizeof(filename), "%s/%s", path, direntry->name);
        if (pEnv &&
            /* PL_strcmp takes NULL arg */
            (PL_strcmp(LDBM_FILENAME_SUFFIX, strrchr(direntry->name, '.')) == 0)) {
            rc = bdb_db_remove_ex(pEnv, filename, 0, PR_TRUE);
        } else {
            rc = ldbm_delete_dirs(filename);
        }
    }
    PR_CloseDir(dirhandle);
    PR_RmDir(path);
    return rc;
}

/* TEL:  Added startdb flag.  If set (1), the DB environment will be started so
 * that bdb_db_remove_ex will be used to remove the database files instead
 * of simply deleting them.  That is important when doing a selective restoration
 * of a single backend (FRI).  If not set (0), the traditional remove is used.
 */
static int
_bdb_delete_instance_dir(ldbm_instance *inst, int startdb)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char filename[MAXPATHLEN];
    struct ldbminfo *li = inst->inst_li;
    dblayer_private *priv = NULL;
    bdb_db_env *pEnv = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int rval = 0;

    if (NULL == li) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "_bdb_delete_instance_dir", "NULL LDBM info\n");
        rval = -1;
        goto done;
    }

    if (startdb) {
        /* close immediately; no need to run db threads */
        rval = bdb_start(li, DBLAYER_NORMAL_MODE | DBLAYER_NO_DBTHREADS_MODE);
        if (rval) {
            slapi_log_err(SLAPI_LOG_ERR, "_bdb_delete_instance_dir", "bdb_start failed! %s (%d)\n",
                          dblayer_strerror(rval), rval);
            goto done;
        }
    }

    priv = li->li_dblayer_private;
    if (NULL != priv) {
        pEnv = (bdb_db_env *)priv->dblayer_env;
    }

    if (inst->inst_dir_name == NULL)
        dblayer_get_instance_data_dir(inst->inst_be);

    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp) {
        dirhandle = PR_OpenDir(inst_dirp);
    }
    if (!dirhandle) {
        if (PR_GetError() == PR_FILE_NOT_FOUND_ERROR) {
            /* the directory does not exist... that's not an error */
            rval = 0;
            goto done;
        }
        if (inst_dirp && *inst_dirp) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "_bdb_delete_instance_dir", "inst_dir is NULL\n");
        } else {
            slapi_log_err(SLAPI_LOG_ERR,
                          "_bdb_delete_instance_dir", "PR_OpenDir(%s) failed (%d): %s\n",
                          inst_dirp, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        }
        rval = -1;
        goto done;
    }

    /*
        Note the use of PR_Delete here as opposed to using
        sleepycat to "remove" the file. Reason: One should
        not expect logging to be able to recover the wholesale
        removal of a complete directory... a directory that includes
        files outside the scope of sleepycat's logging. rwagner

        ADDITIONAL COMMENT:
        libdb41 is more strict on the transaction log control.
        Even if checkpoint is forced before this delete function,
        no log regarding the file deleted found in the log file,
        following checkpoint repeatedly complains with these error messages:
        libdb: <path>/mail.db4: cannot sync: No such file or directory
        libdb: txn_checkpoint: failed to flush the buffer cache
                               No such file or directory
    */

    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT |
                                                         PR_SKIP_DOT_DOT))) {
        if (!direntry->name)
            break;
        PR_snprintf(filename, MAXPATHLEN, "%s/%s", inst_dirp, direntry->name);
        if (pEnv &&
            /* PL_strcmp takes NULL arg */
            (PL_strcmp(LDBM_FILENAME_SUFFIX, strrchr(direntry->name, '.')) == 0)) {
            if (strcmp(direntry->name, BDB_CL_FILENAME) == 0) {
                /* do not delete the changelog, if it no longer
                 * matches the database it will be recreated later
                 */
                continue;
            }
            rval = bdb_db_remove_ex(pEnv, filename, 0, PR_TRUE);
        } else {
            rval = ldbm_delete_dirs(filename);
        }
    }
    PR_CloseDir(dirhandle);
    if (pEnv && startdb) {
        rval = dblayer_close(li, DBLAYER_NORMAL_MODE);
        if (rval) {
            slapi_log_err(SLAPI_LOG_ERR, "_bdb_delete_instance_dir", "dblayer_close failed! %s (%d)\n",
                          dblayer_strerror(rval), rval);
        }
    }
done:
    /* remove the directory itself too */
    /* no
    if (0 == rval)
        PR_RmDir(inst_dirp);
    */
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

/* delete the db3 files in a specific backend instance --
 * this is probably only used for import.
 * assumption: dblayer is open, but the instance has been closed.
 */
int
bdb_delete_instance_dir(backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int ret = bdb_force_checkpoint(li);

    if (ret != 0) {
        return ret;
    } else {
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        return _bdb_delete_instance_dir(inst, 0);
    }
}


static int
bdb_delete_database_ex(struct ldbminfo *li, char *cldir)
{
    Object *inst_obj;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRFileInfo64 fileinfo;
    char filename[MAXPATHLEN];
    char *log_dir;
    int ret;

    PR_ASSERT(NULL != li);
    PR_ASSERT(NULL != (dblayer_private *)li->li_dblayer_private);

    /* delete each instance */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);

        if (inst->inst_be->be_instance_info != NULL) {
            ret = _bdb_delete_instance_dir(inst, 0 /* Do not start DB environment: traditional */);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_delete_database_ex", "Failed (%d)\n", ret);
                return ret;
            }
        }
    }

    /* changelog path is given; delete it, too. */
    if (cldir) {
        ret = _dblayer_delete_aux_dir(li, cldir);
        if (ret) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_delete_database_ex", "Failed to delete \"%s\"\n",
                          cldir);
            return ret;
        }
    }

    /* now smash everything else in the db/ dir */
    if (BDB_CONFIG(li)->bdb_home_directory == NULL){
        slapi_log_err(SLAPI_LOG_ERR, "bdb_delete_database_ex",
            "bdb_home_directory is NULL, can not proceed\n");
        return -1;
    }
    dirhandle = PR_OpenDir(BDB_CONFIG(li)->bdb_home_directory);
    if (!dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_delete_database_ex", "PR_OpenDir (%s) failed (%d): %s\n",
                      BDB_CONFIG(li)->bdb_home_directory,
                      PR_GetError(), slapd_pr_strerror(PR_GetError()));
        return -1;
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT |
                                                         PR_SKIP_DOT_DOT))) {
        int rval_tmp = 0;
        if (!direntry->name)
            break;

        PR_snprintf(filename, MAXPATHLEN, "%s/%s", BDB_CONFIG(li)->bdb_home_directory,
                    direntry->name);

        /* Do not call PR_Delete on the instance directories if they exist.
         * It would not work, but we still should not do it. */
        rval_tmp = PR_GetFileInfo64(filename, &fileinfo);
        if (rval_tmp == PR_SUCCESS && fileinfo.type != PR_FILE_DIRECTORY) {
            /* Skip deleting log files; that should be handled below.
             * (Note, we don't want to use "filename," because that is qualified and would
             * not be compatibile with what bdb_is_logfilename expects.) */
            if (!bdb_is_logfilename(direntry->name)) {
                PR_Delete(filename);
            }
        }
    }

    PR_CloseDir(dirhandle);
    /* remove transaction logs */
    if ((NULL != BDB_CONFIG(li)->bdb_log_directory) &&
        (0 != strlen(BDB_CONFIG(li)->bdb_log_directory))) {
        log_dir = BDB_CONFIG(li)->bdb_log_directory;
    } else {
        log_dir = bdb_get_home_dir(li, NULL);
    }
    if (log_dir && *log_dir) {
        ret = bdb_delete_transaction_logs(log_dir);
        if (ret) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_delete_database_ex", "bdb_delete_transaction_logs failed (%d)\n", ret);
            return -1;
        }
    }
    return 0;
}

/* delete an entire db/ directory, including all instances under it!
 * this is used mostly for restores.
 * dblayer is assumed to be closed.
 */
int
bdb_delete_db(struct ldbminfo *li)
{
    return bdb_delete_database_ex(li, NULL);
}


/*
 * Return the size of the database (in kilobytes).  XXXggood returning
 * the size in units of kb is really a hack, and is done because we
 * didn't have NSPR support for 64-bit file offsets originally (now we do)
 * Caveats:
 * - We can still return incorrect results if an individual file is
 *   larger than fit in a PRUint32.
 * - PR_GetFileInfo64 doesn't do any special processing for symlinks,
 *   nor does it inform us if the file is a symlink.  Nice.  So if
 *   a file in the db directory is a symlink, the size we return
 *   will probably be way too small.
 */
int
bdb_database_size(struct ldbminfo *li, unsigned int *size)
{
    bdb_config *priv = NULL;
    int return_value = 0;
    char filename[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    unsigned int cumulative_size = 0;
    unsigned int remainder = 0;
    PRFileInfo64 info;

    PR_ASSERT(NULL != li);
    priv = (bdb_config *)li->li_dblayer_config;
    PR_ASSERT(NULL != priv);

    dirhandle = PR_OpenDir(priv->bdb_home_directory);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name) {
                break;
            }
            PR_snprintf(filename, MAXPATHLEN, "%s/%s", priv->bdb_home_directory, direntry->name);
            return_value = PR_GetFileInfo64(filename, &info);
            if (PR_SUCCESS == return_value) {
                cumulative_size += (info.size / 1024);
                remainder += (info.size % 1024);
            } else {
                cumulative_size = (PRUint32)0;
                return_value = -1;
                break;
            }
        }
        PR_CloseDir(dirhandle);
    } else {
        return_value = -1;
    }

    *size = cumulative_size + (remainder / 1024);
    return return_value;
}


/*
 * Obtain a count of all the BDB files in the indicated directory.
 *
 * directory : The path to examine.
 * count     : Output parameter for the final count.
 * recurse   : 0/1, recursion is not complete, it only goes down one level.
 *
 * IMPORTANT: 'count' must be set to 0 by the caller before being passed.
 */
static int
bdb_count_dbfiles_in_dir(char *directory, int *count, int recurse)
{
    /* The new recurse argument was added to help with multiple backend
     * instances.  When recurse is true, this function will also look through
     * the directories in the given directory for .db3 files. */
    int return_value = 0;
    PRDir *dirhandle = NULL;

    dirhandle = PR_OpenDir(directory);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        char *direntry_name;
        PRFileInfo64 info;

        while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name) {
                break;
            }
            direntry_name = PR_smprintf("%s/%s", directory, direntry->name);
            if ((PR_GetFileInfo64(direntry_name, &info) == PR_SUCCESS) &&
                (PR_FILE_DIRECTORY == info.type) && recurse) {
                /* Recurse into this directory but not any further.  This is
                 * because each instance gets its own directory, but in those
                 * directories there should be only .db3 files.  There should
                 * not be any more directories in an instance directory. */
                bdb_count_dbfiles_in_dir(direntry_name, count, 0 /* don't recurse */);
            }
            if (direntry_name) {
                PR_smprintf_free(direntry_name);
            }
            /* PL_strcmp takes NULL arg */
            if (PL_strcmp(LDBM_FILENAME_SUFFIX, strrchr(direntry->name, '.')) == 0) {
                (*count)++;
            }
        }
        PR_CloseDir(dirhandle);
    } else {
        return_value = -1;
    }

    return return_value;
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
bdb_copyfile(char *source, char *destination, int overwrite __attribute__((unused)), int mode)
{
#ifdef DB_USE_64LFS
#define OPEN_FUNCTION bdb_open_large
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
        slapi_log_err(SLAPI_LOG_ERR, "bdb_copyfile", "Failed to open source file %s by \"%s\"\n",
                      source, strerror(errno));
        goto error;
    }
    /* Open destination file */
    dest_fd = OPEN_FUNCTION(destination, O_CREAT | O_WRONLY, mode);
    if (-1 == dest_fd) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_copyfile", "Failed to open dest file %s by \"%s\"\n",
                      destination, strerror(errno));
        goto error;
    }
    slapi_log_err(SLAPI_LOG_INFO,
                  "bdb_copyfile", "Copying %s to %s\n", source, destination);
    /* Loop round reading data and writing it */
    while (1) {
        int i;
        char *ptr = NULL;
        return_value = read(source_fd, buffer, 64 * 1024);
        if (return_value <= 0) {
            /* means error or EOF */
            if (return_value < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_copyfile", "Failed to read by \"%s\": rval = %d\n",
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
                slapi_log_err(SLAPI_LOG_ERR, "bdb_copyfile", "Failed to write by \"%s\"; real: %d bytes, exp: %d bytes\n",
                              strerror(errno), return_value, bytes_to_write);
                if (return_value > 0) {
                    bytes_to_write -= return_value;
                    ptr += return_value;
                    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_copyfile", "Retrying to write %d bytes\n", bytes_to_write);
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

/*
 * Copies all the .db# files in instance_dir to a directory with the same name
 * in destination_dir.  Both instance_dir and destination_dir are absolute
 * paths.
 * (#604921: added indexonly flag for the use in convindices
 *           -- backup/restore indices)
 *
 * If the argument restore is true,
 *        logging messages will be about "Restoring" files.
 * If the argument restore is false,
 *        logging messages will be about "Backing up" files.
 * The argument cnt is used to count the number of files that were copied.
 *
 * This function is used during db2bak and bak2db.
 */
int
bdb_copy_directory(struct ldbminfo *li,
                       Slapi_Task *task,
                       char *src_dir,
                       char *dest_dir,
                       int restore,
                       int *cnt,
                       int indexonly,
                       int is_changelog)
{
    dblayer_private *priv = NULL;
    char *new_src_dir = NULL;
    char *new_dest_dir = NULL;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char *compare_piece = NULL;
    char *filename1;
    char *filename2;
    int return_value = -1;
    char *relative_instance_name = NULL;
    char *inst_dirp = NULL;
    char inst_dir[MAXPATHLEN];
    char sep;
    int src_is_fullpath = 0;
    ldbm_instance *inst = NULL;

    if (!src_dir || '\0' == *src_dir) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_copy_directory", "src_dir is empty\n");
        return return_value;
    }
    if (!dest_dir || '\0' == *dest_dir) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_copy_directory", "dest_dir is empty\n");
        return return_value;
    }

    priv = li->li_dblayer_private;

    /* get the backend instance name */
    sep = get_sep(src_dir);
    if ((relative_instance_name = strrchr(src_dir, sep)) == NULL)
        relative_instance_name = src_dir;
    else
        relative_instance_name++;

    if (is_fullpath(src_dir)) {
        src_is_fullpath = 1;
    }
    if (is_changelog) {
        if (!src_is_fullpath) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_copy_directory", "Changelogdir \"%s\" is not full path; "
                                                                   "Skipping it.\n",
                          src_dir);
            return 0;
        }
    } else {
        inst = ldbm_instance_find_by_name(li, relative_instance_name);
        if (NULL == inst) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_copy_directory", "Backend instance \"%s\" does not exist; "
                                                                   "Instance path %s could be invalid.\n",
                          relative_instance_name, src_dir);
            return return_value;
        }
    }

    if (src_is_fullpath) {
        new_src_dir = src_dir;
    } else {
        int len;

        inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                              inst_dir, MAXPATHLEN);
        if (!inst_dirp || !*inst_dirp) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_copy_directory", "Instance dir is NULL.\n");
            if (inst_dirp != inst_dir) {
                slapi_ch_free_string(&inst_dirp);
            }
            return return_value;
        }
        len = strlen(inst_dirp);
        sep = get_sep(inst_dirp);
        if (*(inst_dirp + len - 1) == sep)
            sep = '\0';
        new_src_dir = inst_dirp;
    }

    dirhandle = PR_OpenDir(new_src_dir);
    if (NULL == dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_copy_directory", "Failed to open dir %s\n",
                      new_src_dir);

        return return_value;
    }

    while (NULL != (direntry =
                        PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name) {
            /* NSPR doesn't behave like the docs say it should */
            break;
        }
        if (indexonly &&
            0 == strcmp(direntry->name, ID2ENTRY LDBM_FILENAME_SUFFIX)) {
            continue;
        }

        compare_piece = PL_strrchr((char *)direntry->name, '.');
        if (NULL == compare_piece) {
            compare_piece = (char *)direntry->name;
        }
        /* rename .db3 -> .db4 or .db4 -> .db */
        if (0 == strcmp(compare_piece, LDBM_FILENAME_SUFFIX) ||
            0 == strcmp(compare_piece, LDBM_SUFFIX_OLD) ||
            0 == strcmp(direntry->name, DBVERSION_FILENAME)) {
            /* Found a database file.  Copy it. */

            if (NULL == new_dest_dir) {
                /* Need to create the new directory where the files will be
                 * copied to. */
                PRFileInfo64 info;
                char *prefix = "";
                char mysep = 0;

                if (!is_fullpath(dest_dir)) {
                    prefix = bdb_get_home_dir(li, NULL);
                    if (!prefix || !*prefix) {
                        continue;
                    }
                    mysep = get_sep(prefix);
                }

                if (mysep)
                    new_dest_dir = slapi_ch_smprintf("%s%c%s%c%s",
                                                     prefix, mysep, dest_dir, mysep, relative_instance_name);
                else
                    new_dest_dir = slapi_ch_smprintf("%s/%s",
                                                     dest_dir, relative_instance_name);
                /* } */
                if (PR_SUCCESS == PR_GetFileInfo64(new_dest_dir, &info)) {
                    ldbm_delete_dirs(new_dest_dir);
                }
                if (mkdir_p(new_dest_dir, 0700) != PR_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_ERR, "bdb_copy_directory", "Can't create new directory %s, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                                  new_dest_dir, PR_GetError(),
                                  slapd_pr_strerror(PR_GetError()));
                    goto out;
                }
            }

            filename1 = slapi_ch_smprintf("%s/%s", new_src_dir, direntry->name);
            filename2 = slapi_ch_smprintf("%s/%s", new_dest_dir, direntry->name);

            if (restore) {
                slapi_log_err(SLAPI_LOG_INFO, "bdb_copy_directory", "Restoring file %d (%s)\n",
                              *cnt, filename2);
                if (task) {
                    slapi_task_log_notice(task,
                                          "Restoring file %d (%s)", *cnt, filename2);
                    slapi_task_log_status(task,
                                          "Restoring file %d (%s)", *cnt, filename2);
                }
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "bdb_copy_directory", "Backing up file %d (%s)\n",
                              *cnt, filename2);
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backing up file %d (%s)", *cnt, filename2);
                    slapi_task_log_status(task,
                                          "Backing up file %d (%s)", *cnt, filename2);
                }
            }

            /* copy filename1 to filename2 */
            /* PL_strcmp takes NULL arg */
            return_value = bdb_copyfile(filename1, filename2,
                                                0, priv->dblayer_file_mode);
            if (return_value < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_copy_directory", "Failed to copy file %s to %s\n",
                              filename1, filename2);
                slapi_ch_free((void **)&filename1);
                slapi_ch_free((void **)&filename2);
                break;
            }
            slapi_ch_free((void **)&filename1);
            slapi_ch_free((void **)&filename2);

            (*cnt)++;
        }
    }
out:
    PR_CloseDir(dirhandle);
    slapi_ch_free_string(&new_dest_dir);
    if ((new_src_dir != src_dir) && (new_src_dir != inst_dir)) {
        slapi_ch_free_string(&new_src_dir);
    }
    return return_value;
}

/* Destination Directory is an absolute pathname */
int
bdb_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task)
{
    dblayer_private *priv = NULL;
    bdb_config *conf = NULL;
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
    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    db_dir = bdb_get_db_dir(li);

    home_dir = bdb_get_home_dir(li, NULL);
    if (NULL == home_dir || '\0' == *home_dir) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_backup", "Missing db home directory info\n");
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
     * around DB problem. If we don't do it this way around DB
     * thinks all old transaction logs are required for recovery
     * when the DB environment has been newly created (such as
     * after an import).
     */

    /* do a quick checkpoint */
    bdb_force_checkpoint(li);
    dblayer_txn_init(li, &txn);
    return_value = dblayer_txn_begin_all(li, NULL, &txn);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_backup", "Transaction error\n");
        return return_value;
    }

    if (g_get_shutdown() || c_get_shutdown()) {
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_backup", "Server shutting down, backup aborted\n");
        return_value = -1;
        goto bail;
    }

    /* repeat this until the logfile sets match... */
    do {
        /* get the list of logfiles currently existing */
        if (conf->bdb_enable_transactions) {
            return_value = LOG_ARCHIVE(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV,
                                       &listA, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listA == NULL)) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_backup", "Log archive error\n");
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
            slapi_log_err(SLAPI_LOG_ERR, "bdb_backup", "Server shutting down, backup aborted\n");
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
                              "bdb_backup", "Instance dir is empty\n");
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
            return_value = bdb_copy_directory(li, task, inst_dirp,
                                                  dest_dir, 0 /* backup */,
                                                  &cnt, 0, 0);
            if (return_value) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_backup", "Error in copying directory "
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
        if (conf->bdb_enable_transactions) {
            /* now, get the list of logfiles that still exist */
            return_value = LOG_ARCHIVE(((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV,
                                       &listB, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listB == NULL)) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_backup", "Can't get list of logs\n");
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
                                  "bdb_backup", "Log %s has been swiped "
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
                slapi_log_err(SLAPI_LOG_ERR, "bdb_backup", "Server shutting down, backup aborted\n");
                return_value = -1;
                goto bail;
            }

            if (ok) {
                size_t p1len, p2len;
                char **listptr;

                prefix = NULL;
                if ((NULL != conf->bdb_log_directory) &&
                    (0 != strlen(conf->bdb_log_directory))) {
                    prefix = conf->bdb_log_directory;
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
                    slapi_log_err(SLAPI_LOG_INFO, "bdb_backup", "Backing up file %d (%s)\n",
                                  cnt, pathname2);
                    if (task) {
                        slapi_task_log_notice(task,
                                              "Backing up file %d (%s)", cnt, pathname2);
                        slapi_task_log_status(task,
                                              "Backing up file %d (%s)", cnt, pathname2);
                    }
                    return_value = bdb_copyfile(pathname1, pathname2,
                                                    0, priv->dblayer_file_mode);
                    if (0 > return_value) {
                        slapi_log_err(SLAPI_LOG_ERR, "bdb_backup", "Error in copying file '%s' (err=%d)\n",
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
                        slapi_log_err(SLAPI_LOG_ERR, "bdb_backup", "Server shutting down, backup aborted\n");
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
    slapi_log_err(SLAPI_LOG_INFO, "bdb_backup", "Backing up file %d (%s)\n", cnt, pathname2);
    if (task) {
        slapi_task_log_notice(task, "Backing up file %d (%s)", cnt, pathname2);
        slapi_task_log_status(task, "Backing up file %d (%s)", cnt, pathname2);
    }
    return_value = bdb_copyfile(pathname1, pathname2, 0, priv->dblayer_file_mode);
    if (0 > return_value) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_backup", "Error in copying version file "
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
        return_value = bdb_dse_conf_backup(li, dest_dir);

    /* Backup the config files */
    if (ldbm_archive_config(dest_dir, task) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_backup",
                "Backup of config files failed or is incomplete\n");
    }

bail:
    slapi_ch_free((void **)&listA);
    slapi_ch_free((void **)&listB);
    dblayer_txn_abort_all(li, &txn);
    slapi_ch_free_string(&changelogdir);
    return return_value;
}


/*
 * Restore is pretty easy.
 * We delete the current database.
 * We then copy all the files over from the backup point.
 * We then leave them there for the slapd process to pick up and do the recovery
 * (which it will do as it sees no guard file).
 */

/* Helper function first */

static int
bdb_is_logfilename(const char *path)
{
    int ret = 0;
    /* Is the filename at least 4 characters long ? */
    if (strlen(path) < 4) {
        return 0; /* Not a log file then */
    }
    /* Are the first 4 characters "log." ? */
    ret = strncmp(path, "log.", 4);
    if (0 == ret) {
        /* Now, are the last 4 characters _not_ .db# ? */
        const char *piece = path + (strlen(path) - 4);
        ret = strcmp(piece, LDBM_FILENAME_SUFFIX);
        if (0 != ret) {
            /* Is */
            return 1;
        }
    }
    return 0; /* Is not */
}

/* remove log.xxx from log directory*/
static int
bdb_delete_transaction_logs(const char *log_dir)
{
    int rc = 0;
    char filename1[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    dirhandle = PR_OpenDir(log_dir);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        int is_a_logfile = 0;
        int pre = 0;
        PRFileInfo64 info;

        while (NULL != (direntry =
                            PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name) {
                /* NSPR doesn't behave like the docs say it should */
                slapi_log_err(SLAPI_LOG_ERR, "bdb_delete_transaction_logs", "PR_ReadDir failed (%d): %s\n",
                              PR_GetError(), slapd_pr_strerror(PR_GetError()));
                break;
            }
            PR_snprintf(filename1, MAXPATHLEN, "%s/%s", log_dir, direntry->name);
            pre = PR_GetFileInfo64(filename1, &info);
            if (pre == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) {
                continue;
            }
            is_a_logfile = bdb_is_logfilename(direntry->name);
            if (is_a_logfile && (NULL != log_dir) && (0 != strlen(log_dir))) {
                slapi_log_err(SLAPI_LOG_INFO, "bdb_delete_transaction_logs", "Deleting log file: (%s)\n",
                              filename1);
                unlink(filename1);
            }
        }
        PR_CloseDir(dirhandle);
    } else if (PR_FILE_NOT_FOUND_ERROR != PR_GetError()) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_delete_transaction_logs", "PR_OpenDir(%s) failed (%d): %s\n",
                      log_dir, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        rc = 1;
    }
    return rc;
}

const char *bdb_skip_list[] =
    {
        ".ldif",
        NULL};

static int
bdb_doskip(const char *filename)
{
    const char **p;
    int len = strlen(filename);

    for (p = bdb_skip_list; p && *p; p++) {
        int n = strlen(*p);
        if (0 == strncmp(filename + len - n, *p, n))
            return 1;
    }
    return 0;
}

static int
bdb_bak_config(const char *dir_name)
{
    const char *p = BACKUP_CONFIG_DIR;
    int len = strlen(dir_name);
    int n = strlen(p);

    if (0 == strncmp(dir_name + len - n, p, n)) {
        return 1;
    } else {
        return 0;
    }
}

int
bdb_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task)
{
    bdb_config *conf = NULL;
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
    char *restore_dir = NULL;
    char *prefix = NULL;
    int cnt = 1;

    PR_ASSERT(NULL != li);
    conf = (bdb_config *)li->li_dblayer_config;
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* DBDB this is a hack, take out later */
    PR_Lock(li->li_config_mutex);
    /* bdb_home_directory is freed in bdb_post_close.
     * li_directory needs to live beyond dblayer. */
    slapi_ch_free_string(&conf->bdb_home_directory);
    conf->bdb_home_directory = slapi_ch_strdup(li->li_directory);
    conf->bdb_cachesize = li->li_dbcachesize;
    conf->bdb_lock_config = li->li_dblock;
    conf->bdb_ncache = li->li_dbncache;
    priv->dblayer_file_mode = li->li_mode;
    PR_Unlock(li->li_config_mutex);

    home_dir = bdb_get_home_dir(li, NULL);

    if (NULL == home_dir || '\0' == *home_dir) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_restore",
                      "Missing db home directory info\n");
        return -1;
    }

    /* We find out if slapd is running */
    /* If it is, we fail */
    /* We check on the source staging area, no point in going further if it
     * isn't there */
    if (stat(src_dir, &sbuf) < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_restore", "Backup directory %s does not "
                                                        "exist.\n",
                      src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s does not exist.",
                                  src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    } else if (!S_ISDIR(sbuf.st_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_restore", "Backup directory %s is not "
                                                        "a directory.\n",
                      src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s is not a directory.",
                                  src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (!bdb_version_exists(li, src_dir)) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_restore", "Backup directory %s does not "
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
                    if (bdb_bak_config((char *)direntry->name)) {
                        /* Ignore config dir */
                        continue;
                    }
                    inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
                    if (inst == NULL) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "bdb_restore", "Target server has no backend (%s) configured\n",
                                      direntry->name);
                        if (task) {
                            slapi_task_log_notice(task,
                                                  "bdb_restore - Target server has no backend (%s) configured",
                                                  direntry->name);
                            slapi_task_cancel(task, LDAP_UNWILLING_TO_PERFORM);
                        }
                        PR_CloseDir(dirhandle);
                        return_value = LDAP_UNWILLING_TO_PERFORM;
                        goto error_out;
                    }

                    if (slapd_comp_path(src_dir, inst->inst_parent_dir_name) == 0) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "bdb_restore", "Backup dir %s and target dir %s are identical\n",
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

    /* Otherwise use the src_dir from the caller */
    real_src_dir = src_dir;

    /* We copy the files over from the staging area */
    /* We want to treat the logfiles specially: if there's
     * a log file directory configured, copy the logfiles there
     * rather than to the db dirctory */
    dirhandle = PR_OpenDir(real_src_dir);
    if (NULL == dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_restore", "Failed to open the directory \"%s\"\n", real_src_dir);
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
            inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
            if (inst == NULL || bdb_bak_config((char *)direntry->name)) {
                continue;
            }

            restore_dir = inst->inst_parent_dir_name;
            /* If we're doing a partial restore, we need to reset the LSNs on the data files */
            if (bdb_copy_directory(li, task, filename1,
                                       restore_dir, 1 /* restore */, &cnt, 0, 0) == 0)
                continue;
            else {
                slapi_log_err(SLAPI_LOG_ERR,
                              "bdb_restore", "Failed to copy directory %s\n",
                              filename1);
                if (task) {
                    slapi_task_log_notice(task,
                                          "bdb_restore - Failed to copy directory %s", filename1);
                }
                goto error_out;
            }
        }

        if (bdb_doskip(direntry->name))
            continue;

        /* Is this a log file ? */
        /* Log files have names of the form "log.xxxxx" */
        /* We detect these by looking for the prefix "log." and
         * the lack of the ".db#" suffix */
        is_a_logfile = bdb_is_logfilename(direntry->name);
        if (is_a_logfile) {
            seen_logfiles = 1;
        }
        if (is_a_logfile && (NULL != BDB_CONFIG(li)->bdb_log_directory) &&
            (0 != strlen(BDB_CONFIG(li)->bdb_log_directory))) {
            prefix = BDB_CONFIG(li)->bdb_log_directory;
        } else {
            prefix = home_dir;
        }
        mkdir_p(prefix, 0700);
        PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                    real_src_dir, direntry->name);
        PR_snprintf(filename2, sizeof(filename2), "%s/%s",
                    prefix, direntry->name);
        slapi_log_err(SLAPI_LOG_INFO, "bdb_restore", "Restoring file %d (%s)\n",
                      cnt, filename2);
        if (task) {
            slapi_task_log_notice(task, "Restoring file %d (%s)",
                                  cnt, filename2);
            slapi_task_log_status(task, "Restoring file %d (%s)",
                                  cnt, filename2);
        }
        return_value = bdb_copyfile(filename1, filename2, 0,
                                        priv->dblayer_file_mode);
        if (0 > return_value) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_restore", "Failed to copy file %s\n", filename1);
            goto error_out;
        }
        cnt++;
    }
    PR_CloseDir(dirhandle);

    /* We're done ! */

    /* [605024] check the DBVERSION and reset idl-switch if needed */
    if (bdb_version_exists(li, home_dir)) {
        char *ldbmversion = NULL;
        char *dataversion = NULL;

        if (bdb_version_read(li, home_dir, &ldbmversion, &dataversion) != 0) {
            slapi_log_err(SLAPI_LOG_WARNING, "bdb_restore", "Unable to read dbversion file in %s\n",
                          home_dir);
        } else {
            bdb_adjust_idl_switch(ldbmversion, li);
            slapi_ch_free_string(&ldbmversion);
            slapi_ch_free_string(&dataversion);
        }
    }

    return_value = bdb_check_db_version(li, &action);
    if (action &
        (DBVERSION_UPGRADE_3_4 | DBVERSION_UPGRADE_4_4 | DBVERSION_UPGRADE_4_5)) {
        dbmode = DBLAYER_CLEAN_RECOVER_MODE; /* upgrade: remove logs & recover */
    } else if (seen_logfiles) {
        dbmode = DBLAYER_RESTORE_MODE;
    } else if (action & DBVERSION_NEED_DN2RDN) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_restore", "%s is on, while the instance %s is in the DN format. "
                                         "Please run dn2rdn to convert the database format.\n",
                      CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    } else if (action & DBVERSION_NEED_RDN2DN) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_restore", "%s is off, while the instance %s is in the RDN format. "
                                         "Please change the value to on in dse.ldif.\n",
                      CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    } else {
        dbmode = DBLAYER_RESTORE_NO_RECOVERY_MODE;
    }

    /* now start the database code up, to prevent recovery next time the
     * server starts;
     * bdb_dse_conf_verify may need to have db started, as well. */
    /* If no logfiles were stored, then fatal recovery isn't required */

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* command line mode; no need to run db threads */
        dbmode |= DBLAYER_NO_DBTHREADS_MODE;
    } else /* on-line mode */
    {
        allinstance_set_not_busy(li);
    }

    tmp_rval = bdb_start(li, dbmode);
    if (0 != tmp_rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_restore", "Failed to init database\n");
        if (task) {
            slapi_task_log_notice(task, "bdb_restore - Failed to init database");
        }
        return_value = tmp_rval;
        goto error_out;
    }

    if (0 == return_value) { /* only when the copyfile succeeded */
        /* check the DSE_* files, if any */
        tmp_rval = bdb_dse_conf_verify(li, real_src_dir);
        if (0 != tmp_rval)
            slapi_log_err(SLAPI_LOG_WARNING,
                          "bdb_restore", "Unable to verify the index configuration\n");
    }

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* command line: close the database down again */
        tmp_rval = dblayer_close(li, dbmode);
        if (0 != tmp_rval) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_restore", "Failed to close database\n");
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

    return return_value;
}

static char *
bdb_import_file_name(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *fname = slapi_ch_smprintf("%s/.import_%s", li->li_directory, inst->inst_name);
    slapi_log_err(SLAPI_LOG_DEBUG, "bdb_import_file_name", "DBG: fname=%s\n", fname);
    return fname;
}

static char *
bdb_restore_file_name(struct ldbminfo *li)
{
    char *fname = slapi_ch_smprintf("%s/../.restore", li->li_directory);

    return fname;
}

static int
bdb_file_open(char *fname, int flags, int mode, PRFileDesc **prfd)
{
    int rc = 0;
    *prfd = PR_Open(fname, flags, mode);

    if (NULL == *prfd)
        rc = PR_GetError();
    if (rc && rc != PR_FILE_NOT_FOUND_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_file_open", "Failed to open file: %s, error: (%d) %s\n",
                      fname, rc, slapd_pr_strerror(rc));
    }
    return rc;
}

int
bdb_import_file_init(ldbm_instance *inst)
{
    int rc = -1;
    PRFileDesc *prfd = NULL;
    char *fname = bdb_import_file_name(inst);
    rc = bdb_file_open(fname, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, inst->inst_li->li_mode, &prfd);
    if (prfd) {
        PR_Close(prfd);
        rc = 0;
    }
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_import_file_init", "Failed to open file: %s, error: (%d) %s\n",
                      fname, rc, slapd_pr_strerror(rc));
    }
    slapi_ch_free_string(&fname);
    return rc;
}

int
bdb_restore_file_init(struct ldbminfo *li)
{
    int rc = -1;
    PRFileDesc *prfd;
    char *fname = bdb_restore_file_name(li);
    rc = bdb_file_open(fname, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, li->li_mode, &prfd);
    if (prfd) {
        PR_Close(prfd);
        rc = 0;
    }
    slapi_ch_free_string(&fname);
    return rc;
}
void
bdb_import_file_update(ldbm_instance *inst)
{
    PRFileDesc *prfd;
    char *fname = bdb_import_file_name(inst);
    bdb_file_open(fname, PR_RDWR, inst->inst_li->li_mode, &prfd);

    if (prfd) {
        char *line = slapi_ch_smprintf("import of %s succeeded", inst->inst_dir_name);
        slapi_write_buffer(prfd, line, strlen(line));
        slapi_ch_free_string(&line);
        PR_Close(prfd);
    }
    slapi_ch_free_string(&fname);
}

int
bdb_file_check(char *fname, int mode)
{
    int rc = 0;
    int err;
    PRFileDesc *prfd;
    err = bdb_file_open(fname, PR_RDWR, mode, &prfd);

    if (prfd) {
        /* file exists, additional check on size */
        PRFileInfo64 prfinfo;
        rc = 1;
        /* read it */
        err = PR_GetOpenFileInfo64(prfd, &prfinfo);
        if (err == PR_SUCCESS && 0 == prfinfo.size) {
            /* it is empty restore or import has failed */
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_file_check", "Previous import or restore failed, file: %s is empty\n", fname);
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
bdb_import_file_check(ldbm_instance *inst)
{
    int rc;
    char *fname = bdb_import_file_name(inst);
    rc = bdb_file_check(fname, inst->inst_li->li_mode);
    slapi_ch_free_string(&fname);
    return rc;
}

static int
bdb_restore_file_check(struct ldbminfo *li)
{
    int rc;
    char *fname = bdb_restore_file_name(li);
    rc = bdb_file_check(fname, li->li_mode);
    slapi_ch_free_string(&fname);
    return rc;
}

void
bdb_restore_file_update(struct ldbminfo *li, const char *directory)
{
    PRFileDesc *prfd;
    char *fname = bdb_restore_file_name(li);
    bdb_file_open(fname, PR_RDWR, li->li_mode, &prfd);
    slapi_ch_free_string(&fname);
    if (prfd) {
        char *line = slapi_ch_smprintf("restore of %s succeeded", directory);
        slapi_write_buffer(prfd, line, strlen(line));
        slapi_ch_free_string(&line);
        PR_Close(prfd);
    }
}


/*
 * to change the db extention (e.g., .db3 -> .db4)
 */
int
bdb_update_db_ext(ldbm_instance *inst, char *oldext, char *newext)
{
    struct attrinfo *a = NULL;
    struct ldbminfo *li = NULL;
    dblayer_private *priv = NULL;
    DB *thisdb = NULL;
    int rval = 0;
    char *ofile = NULL;
    char *nfile = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp;

    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_update_db_ext", "Null instance is passed\n");
        return -1; /* non zero */
    }
    li = inst->inst_li;
    priv = li->li_dblayer_private;
    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (NULL == inst_dirp || '\0' == *inst_dirp) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_update_db_ext", "Instance dir is NULL\n");
        if (inst_dirp != inst_dir) {
            slapi_ch_free_string(&inst_dirp);
        }
        return -1; /* non zero */
    }
    for (a = (struct attrinfo *)avl_getfirst(inst->inst_attrs);
         NULL != a;
         a = (struct attrinfo *)avl_getnext()) {
        PRFileInfo64 info;
        ofile = slapi_ch_smprintf("%s/%s%s", inst_dirp, a->ai_type, oldext);

        if (PR_GetFileInfo64(ofile, &info) != PR_SUCCESS) {
            slapi_ch_free_string(&ofile);
            continue;
        }

        /* db->rename disable DB in it; we need to create for each */
        rval = db_create(&thisdb, ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV, 0);
        if (0 != rval) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_update_db_ext", "db_create returned %d (%s)\n",
                          rval, dblayer_strerror(rval));
            goto done;
        }
        nfile = slapi_ch_smprintf("%s/%s%s", inst_dirp, a->ai_type, newext);
        slapi_log_err(SLAPI_LOG_TRACE, "bdb_update_db_ext", "Rename %s -> %s\n",
                      ofile, nfile);

        rval = thisdb->rename(thisdb, (const char *)ofile, NULL /* subdb */,
                              (const char *)nfile, 0);
        if (0 != rval) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_update_db_ext", "Rename returned %d (%s)\n",
                          rval, dblayer_strerror(rval));
            slapi_log_err(SLAPI_LOG_ERR,
                          "bdb_update_db_ext", "Index (%s) Failed to update index %s -> %s\n",
                          inst->inst_name, ofile, nfile);
            goto done;
        }
        slapi_ch_free_string(&ofile);
        slapi_ch_free_string(&nfile);
    }

    rval = db_create(&thisdb, ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV, 0);
    if (0 != rval) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_update_db_ext", "db_create returned %d (%s)\n",
                      rval, dblayer_strerror(rval));
        goto done;
    }
    ofile = slapi_ch_smprintf("%s/%s%s", inst_dirp, ID2ENTRY, oldext);
    nfile = slapi_ch_smprintf("%s/%s%s", inst_dirp, ID2ENTRY, newext);
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_update_db_ext", "Rename %s -> %s\n",
                  ofile, nfile);
    rval = thisdb->rename(thisdb, (const char *)ofile, NULL /* subdb */,
                          (const char *)nfile, 0);
    if (0 != rval) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_update_db_ext", "Rename returned %d (%s)\n",
                      rval, dblayer_strerror(rval));
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_update_db_ext", "Index (%s) Failed to update index %s -> %s\n",
                      inst->inst_name, ofile, nfile);
    }
done:
    slapi_ch_free_string(&ofile);
    slapi_ch_free_string(&nfile);
    if (inst_dirp != inst_dir) {
        slapi_ch_free_string(&inst_dirp);
    }

    return rval;
}

/*
 * delete the index files belonging to the instance
 */
int
bdb_delete_indices(ldbm_instance *inst)
{
    int rval = -1;
    struct attrinfo *a = NULL;
    int i;

    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_delete_indices", "NULL instance is passed\n");
        return rval;
    }
    rval = 0;
    for (a = (struct attrinfo *)avl_getfirst(inst->inst_attrs), i = 0;
         NULL != a;
         a = (struct attrinfo *)avl_getnext(), i++) {
        rval += bdb_rm_db_file(inst->inst_be, a, PR_TRUE, i /* chkpt; 1st time only */);
    }
    return rval;
}

void
bdb_set_recovery_required(struct ldbminfo *li)
{
    if (NULL == li || NULL == li->li_dblayer_config) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_set_recovery_required", "No dblayer info\n");
        return;
    }
    BDB_CONFIG(li)->bdb_recovery_required = 1;
}

int
bdb_get_info(Slapi_Backend *be, int cmd, void **info)
{
    int rc = -1;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *prv = NULL;
    bdb_db_env *penv = NULL;

    if ( !info) {
        return rc;
    }

    if (li) {
        prv = li->li_dblayer_private;
        if (prv) {
            penv = (bdb_db_env *)prv->dblayer_env;
        }
    }

    switch (cmd) {
    case BACK_INFO_DBENV: {
            if (penv && penv->bdb_DB_ENV) {
                *(DB_ENV **)info = penv->bdb_DB_ENV;
                rc = 0;
            }
        break;
    }
    case BACK_INFO_DBENV_OPENFLAGS: {
            if (penv) {
                *(int *)info = penv->bdb_openflags;
                rc = 0;
            }
        break;
    }
    case BACK_INFO_DB_PAGESIZE: {
            if (li && BDB_CONFIG(li)->bdb_page_size) {
                *(uint32_t *)info = BDB_CONFIG(li)->bdb_page_size;
            } else {
                *(uint32_t *)info = DBLAYER_PAGESIZE;
            }
            rc = 0;
        break;
    }
    case BACK_INFO_INDEXPAGESIZE: {
            if (li && BDB_CONFIG(li)->bdb_index_page_size) {
                *(uint32_t *)info = BDB_CONFIG(li)->bdb_index_page_size;
            } else {
                *(uint32_t *)info = DBLAYER_INDEX_PAGESIZE;
            }
            rc = 0;
        break;
    }
    case BACK_INFO_DIRECTORY: {
        if (li) {
            *(char **)info = li->li_directory;
            rc = 0;
        }
        break;
    }
    case BACK_INFO_DB_DIRECTORY: {
        if (li) {
            *(char **)info = BDB_CONFIG(li)->bdb_home_directory;
            rc = 0;
        }
        break;
    }
    case BACK_INFO_DBHOME_DIRECTORY: {
        if (li) {
            if (BDB_CONFIG(li)->bdb_dbhome_directory &&
                BDB_CONFIG(li)->bdb_dbhome_directory[0] != '\0') {
                *(char **)info = BDB_CONFIG(li)->bdb_dbhome_directory;
            } else {
                *(char **)info = BDB_CONFIG(li)->bdb_home_directory;
            }
            rc = 0;
        }
        break;
    }
    case BACK_INFO_INSTANCE_DIR: {
        if (li) {
            ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
            *(char **)info = dblayer_get_full_inst_dir(li, inst, NULL, 0);
            rc = 0;
        }
        break;
    }
    case BACK_INFO_LOG_DIRECTORY: {
        if (li) {
            *(char **)info = bdb_config_db_logdirectory_get_ext((void *)li);
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
            DB *db;
            rc = dblayer_get_changelog(be, (dbi_db_t **)&db, DB_CREATE);
        }
        if (rc == 0) {
            *(DB **)info = inst->inst_changelog;
        } else {
            *(DB **)info = NULL;
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
bdb_set_info(Slapi_Backend *be, int cmd, void **info)
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
bdb_back_ctrl(Slapi_Backend *be, int cmd, void *info)
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
        DB *db = (DB *)info;
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
        if (li) {
            dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
            if (priv && priv->dblayer_env) {
                char *instancedir;
                slapi_back_get_info(be, BACK_INFO_INSTANCE_DIR, (void **)&instancedir);
                char *path = slapi_ch_smprintf("%s/%s", instancedir, BDB_CL_FILENAME);
                db->close(db, 0);
                rc = bdb_db_remove_ex((bdb_db_env *)priv->dblayer_env, path, NULL, PR_TRUE);
                inst->inst_changelog = NULL;
                slapi_ch_free_string(&instancedir);
            }
        }
        break;
    }
    case BACK_INFO_DBENV_CLDB_UPGRADE: {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        char *oldFile = (char *)info;

        if (li) {
            dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
            if (priv && priv->dblayer_env) {
                DB_ENV *pEnv = ((bdb_db_env *)priv->dblayer_env)->bdb_DB_ENV;
                if (pEnv) {
                    char *instancedir;
                    char *newFile;

                    slapi_back_get_info(be, BACK_INFO_INSTANCE_DIR, (void **)&instancedir);
                    newFile = slapi_ch_smprintf("%s/%s", instancedir, BDB_CL_FILENAME);
                    rc = pEnv->dbrename(pEnv, 0, oldFile, 0, newFile, 0);
                    slapi_ch_free_string(&instancedir);
                    slapi_ch_free_string(&newFile);
                    bdb_force_logrenewal(li);
                }
            }
        }
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

dbi_error_t bdb_map_error(const char *funcname, int err)
{
    char *msg = NULL;

    switch (err) {
        case 0:
            return DBI_RC_SUCCESS;
        case DB_KEYEXIST:
            return DBI_RC_KEYEXIST;
        case DB_BUFFER_SMALL:
            return DBI_RC_BUFFER_SMALL;
        case DB_NOTFOUND:
            return DBI_RC_NOTFOUND;
        case DB_RUNRECOVERY:
            return DBI_RC_RUNRECOVERY;
        case DB_LOCK_DEADLOCK:
            return DBI_RC_RETRY;
        default:
            msg = db_strerror(err);
            if (!msg) {
                msg = "";
            }
            slapi_log_err(SLAPI_LOG_ERR, "bdb_map_error",
                "%s failed with db error %d : %s\n", funcname, err, msg);
            return DBI_RC_OTHER;
    }
}

/* Conversion a dbi_val_t* into a DBT* */
void bdb_dbival2dbt(dbi_val_t *dbi, DBT *dbt, PRBool isresponse)
{
/*
 * isresponse is true means that bdb_dbt2dbival(dbt, dbi, PR_FALSE)
 *  is called a few lines before bdb_dbival2dbt call
 * This means that if data pointer differs then the buffer has been
 * re alloced ==> should beware not to free it twice
 */
    if (!dbi || !dbt) {
        return;
    }
    dbt->data = dbi->data;
    dbt->size = dbi->size;
    dbt->ulen = dbi->ulen;

    if (dbi->flags & DBI_VF_DONTGROW) {
        /* Should not change the buffer */
        dbt->flags = DB_DBT_USERMEM;
    } else if (dbi->flags & DBI_VF_PROTECTED) {
        /* Should not free the buffer */
        dbt->flags = DB_DBT_MALLOC;
    } else {
        /* By default lets try to reuse buffer */
        dbt->flags = DB_DBT_REALLOC;
    }
}

/* Conversion a DBT* into a dbi_val_t* */
void bdb_dbt2dbival(DBT *dbt, dbi_val_t *dbi, PRBool isresponse)
{
/*
 * isresponse is true means that bdb_dbival2dbt(dbt, dbi, PR_FALSE)
 *  is called a few lines before bdb_dbt2dbival call
 * This means that if data pointer differs then the buffer has been
 * re alloced ==> should beware not to free it twice
 */
    if (!dbi || !dbt) {
        return;
    }
    /*
     * if dbi is read only
     *  return NOMEM
     * if dbt and dbi do not have same data address
     *     if dbt is not growable return NOMEM
     *     free dbi
     *     set dbi to dbt values
     *     if dbt is MALLOC or REALLOC set its value to NULL.
     * update the size
     * and the flags
     */

    if (dbi->flags & DBI_VF_READONLY) {
        /* trying to modify read only data */
        PR_ASSERT(0);
        dblayer_value_set_buffer(bdb_be(), dbi, (void*)(-1), -1);
        return;
    }
    /*
     * Note: as dblayer_value_set/dblayer_value_set_buffer is used
     * typical usage:
     *    bdb_dbival2dbt(dbikey, &dbtkey, PR_FALSE);
     *    some bdb operation(...,&dbtkey,...);
     *    bdb_dbt2dbival(&dbtkey, dbikey, PR_TRUE);
     * does free the original value if its address changes
     * So at backend level the dbi needs to be freed once before
     *  exiting the function (no more need to free the
     *  value if its address change as it is the case with
     *  when using DB_DBT_MALLOC )
     */
    if (dbt->data != dbi->data) {
        if (dbi->flags & DBI_VF_DONTGROW) {
            /* trying to realloc a user buffer */
            PR_ASSERT(0);
            dblayer_value_set_buffer(bdb_be(), dbi, (void*)(-1), -1);
            return;
        }
        if (dbt->flags & (DB_DBT_MALLOC | DB_DBT_REALLOC)) {
            if (isresponse) {
                dbi->data = NULL; /* Value is already freed by dbt realloc */
            }
            dblayer_value_set(bdb_be(), dbi, dbt->data, dbt->size);
            dbt->data = NULL;  /* Insure that value will not be freed through dbt */
            dbt->size = 0;
        } else if (dbt->flags & DB_DBT_USERMEM) {
            dblayer_value_set_buffer(bdb_be(), dbi, dbt->data, dbt->size);
            dbi->ulen = dbt->ulen;
        } else {
            /* trying to use uninitialized DBT */
            PR_ASSERT(0);
            dblayer_value_set_buffer(bdb_be(), dbi, (void*)(-1), -1);
            return;
        }
        dbi->ulen = dbt->ulen;
    } else {
        /* data buffer has not changed ==> update the size */
        dbi->size = dbt->size;
        dbi->ulen = dbt->ulen;
    }
}

/**********************/
/* dbimpl.c callbacks */
/**********************/

char *bdb_public_get_db_filename(dbi_db_t *db)
{
    return ((DB*)db)->fname;
}

int bdb_public_bulk_free(dbi_bulk_t *bulkdata)
{
    /* No specific action required for berkeley db handling */
    return DBI_RC_SUCCESS;
}

int bdb_public_bulk_nextdata(dbi_bulk_t *bulkdata, dbi_val_t *data)
{
    DBT bulk;
    void *retdata = NULL;
    u_int32_t retdlen = 0;;
    bdb_dbival2dbt(&bulkdata->v, &bulk, PR_FALSE);
    if (bulkdata->v.flags & DBI_VF_BULK_DATA) {
        DB_MULTIPLE_NEXT(bulkdata->it, &bulk, retdata, retdlen);
        dblayer_value_set_buffer(bulkdata->be, data, retdata, retdlen);
    } else {
        /* Coding error - bulkdata is not initialized or wrong type */
        PR_ASSERT(0);
        return DBI_RC_INVALID;
    }
    if (retdata == NULL || bulkdata->be == NULL) {
        return DBI_RC_NOTFOUND;
    }
    return DBI_RC_SUCCESS;
}

int bdb_public_bulk_nextrecord(dbi_bulk_t *bulkdata, dbi_val_t *key, dbi_val_t *data)
{
    DBT bulk;
    void *retkey = NULL;
    void *retdata = NULL;
    u_int32_t retklen = 0;;
    u_int32_t retdlen = 0;;
    bdb_dbival2dbt(&bulkdata->v, &bulk, PR_FALSE);
    if (bulkdata->v.flags & DBI_VF_BULK_RECORD) {
        DB_MULTIPLE_KEY_NEXT(bulkdata->it, &bulk, retkey, retklen, retdata, retdlen);
        dblayer_value_set_buffer(bulkdata->be, data, retdata, retdlen);
        dblayer_value_set_buffer(bulkdata->be, key, retkey, retklen);
    } else {
        /* Coding error - bulkdata is not initialized or wrong type */
        PR_ASSERT(0);
        return DBI_RC_INVALID;
    }
    if (retdata == NULL || bulkdata->be == NULL) {
        return DBI_RC_NOTFOUND;
    }
    return DBI_RC_SUCCESS;
}

int bdb_public_bulk_init(dbi_bulk_t *bulkdata)
{
    /* No specific action required for berkeley db handling */
    return DBI_RC_SUCCESS;
}

int bdb_public_bulk_start(dbi_bulk_t *bulkdata)
{
    DBT bulk;
    bdb_dbival2dbt(&bulkdata->v, &bulk, PR_FALSE);
    DB_MULTIPLE_INIT(bulkdata->it, &bulk);
    return DBI_RC_SUCCESS;
}

int bdb_public_cursor_bulkop(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_bulk_t *bulkdata)
{
    int mflag = (bulkdata->v.flags & DBI_VF_BULK_RECORD) ? DB_MULTIPLE_KEY : DB_MULTIPLE;
    DBC *bdb_cur = (DBC*)cursor->cur;
    DBT bdb_key = {0};
    DBT bdb_data = {0};
    int rc = 0;

    if (bdb_cur == NULL)
        return DBI_RC_INVALID;

    if (bulkdata->v.size < bdb_cur->dbp->pgsize) {
        /* Make sure that size is 1024 aligned and >= db page size */
        long size = (bdb_cur->dbp->pgsize + 1023L) & ~1023L;
        int flags = bulkdata->v.flags & (DBI_VF_BULK_DATA|DBI_VF_BULK_RECORD);
        dblayer_bulk_set_buffer(bulkdata->be, bulkdata, slapi_ch_malloc(size), size, flags);
    }

    bdb_dbival2dbt(key, &bdb_key, PR_FALSE);
    bdb_dbival2dbt(&bulkdata->v, &bdb_data, PR_FALSE);
    switch (op)
    {
        case DBI_OP_MOVE_TO_FIRST:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_FIRST | DB_MULTIPLE);
            break;
        case DBI_OP_MOVE_TO_KEY:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_SET | mflag);
            break;
        case DBI_OP_NEXT_KEY:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT_NODUP | mflag);
            break;
        case DBI_OP_NEXT:
            PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_RECORD);
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT | mflag);
            break;
        case DBI_OP_NEXT_DATA:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT_DUP | mflag);
            break;
        default:
            /* Unknown bulk operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    bdb_dbt2dbival(&bdb_key, key, PR_TRUE);
    bdb_dbt2dbival(&bdb_data, &bulkdata->v, PR_TRUE);
    return bdb_map_error(__FUNCTION__, rc);
}

int bdb_public_cursor_op(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    DBC *bdb_cur = (DBC*)cursor->cur;
    DBT bdb_key = {0};
    DBT bdb_data = {0};
    int rc = 0;

    if (bdb_cur == NULL) {
        return (op == DBI_OP_CLOSE) ? DBI_RC_SUCCESS : DBI_RC_INVALID;
    }

    bdb_dbival2dbt(key, &bdb_key, PR_FALSE);
    bdb_dbival2dbt(data, &bdb_data, PR_FALSE);
    switch (op)
    {
        case DBI_OP_MOVE_TO_KEY:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_SET);
            break;
        case DBI_OP_MOVE_NEAR_KEY:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_SET_RANGE);
            break;
        case DBI_OP_MOVE_TO_DATA:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_GET_BOTH);
            break;
        case DBI_OP_MOVE_NEAR_DATA:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_GET_BOTH_RANGE);
            break;
        case DBI_OP_MOVE_TO_RECNO:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_SET_RECNO);
            break;
        case DBI_OP_MOVE_TO_FIRST:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_FIRST);
            break;
        case DBI_OP_MOVE_TO_LAST:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_LAST);
            break;
        case DBI_OP_GET:
            /* not a bdb_cur operation (db operation) */
            PR_ASSERT(op != DBI_OP_GET);
            rc = DBI_RC_UNSUPPORTED;
            break;
        case DBI_OP_GET_RECNO:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_GET_RECNO);
            break;
        case DBI_OP_NEXT:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT);
            break;
        case DBI_OP_NEXT_DATA:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT_DUP);
            break;
        case DBI_OP_NEXT_KEY:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT_NODUP);
            break;
        case DBI_OP_PREV:
            rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_PREV);
            break;
        case DBI_OP_PUT:
            /* not a bdb_cur operation (db operation) */
            PR_ASSERT(op != DBI_OP_PUT);
            rc = DBI_RC_UNSUPPORTED;
            break;
        case DBI_OP_REPLACE:
            rc = bdb_cur->c_put(bdb_cur, &bdb_key, &bdb_data, DB_CURRENT);
            break;
        case DBI_OP_ADD:
            rc = bdb_cur->c_put(bdb_cur, &bdb_key, &bdb_data, DB_NODUPDATA);
            break;
        case DBI_OP_DEL:
            rc = bdb_cur->c_del(bdb_cur, 0);
            break;
        case DBI_OP_CLOSE:
            rc = bdb_cur->c_close(bdb_cur);
            break;
        default:
            /* Unknown operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    bdb_dbt2dbival(&bdb_key, key, PR_TRUE);
    bdb_dbt2dbival(&bdb_data, data, PR_TRUE);
    return bdb_map_error(__FUNCTION__, rc);
}

int bdb_public_db_op(dbi_db_t *db,  dbi_txn_t *txn, dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    DB_TXN *bdb_txn = (DB_TXN*)txn;
    DB *bdb_db = (DB*)db;
    DBT bdb_key = {0};
    DBT bdb_data = {0};
    int rc = 0;

    bdb_dbival2dbt(key, &bdb_key, PR_FALSE);
    bdb_dbival2dbt(data, &bdb_data, PR_FALSE);
    switch (op)
    {
        case DBI_OP_GET:
            rc = bdb_db->get(bdb_db, bdb_txn, &bdb_key, &bdb_data, 0);
            break;
        case DBI_OP_PUT:
            rc = bdb_db->put(bdb_db, bdb_txn, &bdb_key, &bdb_data, 0);
            break;
        case DBI_OP_ADD:
            rc = bdb_db->put(bdb_db, bdb_txn, &bdb_key, &bdb_data, DB_NODUPDATA);
            break;
        case DBI_OP_DEL:
            rc = bdb_db->del(bdb_db, bdb_txn, &bdb_key, 0);
            break;
        case DBI_OP_CLOSE:
            rc = bdb_db->close(bdb_db, 0);
            break;
        default:
            /* Unknown db operation */
            PR_ASSERT(op != op);
            rc = DBI_RC_UNSUPPORTED;
            break;
    }
    bdb_dbt2dbival(&bdb_key, key, PR_TRUE);
    bdb_dbt2dbival(&bdb_data, data, PR_TRUE);
    return bdb_map_error(__FUNCTION__, rc);
}

int bdb_public_new_cursor(dbi_db_t *db,  dbi_cursor_t *cursor)
{
    DB *bdb_db = (DB*)db;
    return bdb_map_error(__FUNCTION__, bdb_db->cursor(bdb_db, (DB_TXN*)cursor->txn, (DBC**)&cursor->cur, 0));
}

int bdb_public_value_free(dbi_val_t *data)
{
    /* No specific action required for berkeley db handling */
    return DBI_RC_SUCCESS;
}

int bdb_public_value_init(dbi_val_t *data)
{
    /* No specific action required for berkeley db handling */
    return DBI_RC_SUCCESS;
}

static int
bdb_db_uses_feature(DB_ENV *db_env, u_int32_t flags)
{
    u_int32_t openflags = 0;
    PR_ASSERT(db_env);
    db_env->get_open_flags(db_env, &openflags);

    return (flags & openflags);
}

int
bdb_uses_locking(DB_ENV *db_env)
{
    return bdb_db_uses_feature(db_env, DB_INIT_LOCK);
}

int
bdb_uses_transactions(DB_ENV *db_env)
{
    return bdb_db_uses_feature(db_env, DB_INIT_TXN);
}

int
bdb_uses_mpool(DB_ENV *db_env)
{
    return bdb_db_uses_feature(db_env, DB_INIT_MPOOL);
}

int
bdb_uses_logging(DB_ENV *db_env)
{
    return bdb_db_uses_feature(db_env, DB_INIT_LOG);
}

/*
 * Rules:
 * NULL comes before anything else.
 * Otherwise, strcmp(elem_a->rdn_elem_nrdn_rdn - elem_b->rdn_elem_nrdn_rdn) is
 * returned.
 */
int
bdb_entryrdn_compare_dups(DB *db __attribute__((unused)), const DBT *a, const DBT *b)
{
    if (NULL == a) {
        if (NULL == b) {
            return 0;
        } else {
            return -1;
        }
    } else if (NULL == b) {
        return 1;
    }
    return entryrdn_compare_rdn_elem(a->data, b->data);
}

int
bdb_public_set_dup_cmp_fn(struct attrinfo *a, dbi_dup_cmp_t idx)
{
    switch (idx)
    {
        case DBI_DUP_CMP_NONE:
            a->ai_dup_cmp_fn = NULL;
            break;
        case DBI_DUP_CMP_ENTRYRDN:
            a->ai_dup_cmp_fn = bdb_entryrdn_compare_dups;
            break;
        default:
            PR_ASSERT(0);
            return DBI_RC_UNSUPPORTED;
    }
    return DBI_RC_SUCCESS;
}

int
bdb_dbi_txn_begin(dbi_env_t *env, PRBool readonly, dbi_txn_t *parent_txn, dbi_txn_t **txn)
{
    return TXN_BEGIN(env, parent_txn, txn, 0);
}

int
bdb_dbi_txn_commit(dbi_txn_t *txn)
{
    return TXN_COMMIT(txn, 0);
}

int
bdb_dbi_txn_abort(dbi_txn_t *txn)
{
    return TXN_ABORT(txn);
}

int
bdb_get_entries_count(dbi_db_t *db, dbi_txn_t *txn, int *count)
{
    DB_BTREE_STAT *stats = NULL;
    int rc;

    rc = ((DB*)db)->stat(db, (DB_TXN*)txn, (void *)&stats, 0);
    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_get_entries_count",
                      "Failed to get bd statistics: db error - %d %s\n",
                      rc, db_strerror(rc));
        rc = DBI_RC_OTHER;
    }
    *count = rc ? 0 : stats->bt_ndata;
    slapi_ch_free((void **)&stats);
    return rc;
}

int
bdb_public_cursor_get_count(dbi_cursor_t *cursor, dbi_recno_t *count)
{
    DBC *cur = cursor->cur;
    int rc = cur->c_count(cur, count, 0);
    return bdb_map_error(__FUNCTION__, rc);
}

static void
getdir(char *path, char **eod)
{
    char *pt = strrchr(path, '/');
    if (pt) {
        if (eod) {
            *eod = pt;
        }
        *pt = 0;
    } else if (path[0]) {
        strcpy(path, ".");
    }
}


int
bdb_public_private_open(backend *be, const char *db_filename, int rw, dbi_env_t **env, dbi_db_t **db)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *priv = li->li_dblayer_private;
    bdb_config *conf = (bdb_config *)li->li_dblayer_config;
    bdb_db_env **ppEnv = (bdb_db_env**)&priv->dblayer_env;
    char dbhome[MAXPATHLEN];
    DB_ENV *bdb_env = NULL;
    DB *bdb_db = NULL;
    struct stat st = {0};
    int flags;
    int rc;

    /* Either filename is an existing regular file
     *  or the "home" directory where txn logs are
     */

    PL_strncpyz(dbhome, db_filename, MAXPATHLEN);
    if (stat(dbhome, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            li->li_directory = slapi_ch_strdup(dbhome);
        } else if (S_ISREG(st.st_mode)) {
            getdir(dbhome, NULL);
            li->li_directory = slapi_ch_strdup(db_filename);
            getdir(dbhome, NULL);
        } else {
            fprintf(stderr, "bdb_public_private_open: Unable to determine dbhome from %s\n", db_filename);
            return EINVAL;
        }
    } else {
        getdir(dbhome, NULL);
        li->li_directory = slapi_ch_strdup(dbhome);
        getdir(dbhome, NULL);
        if (stat(dbhome, &st) || ((st.st_mode & S_IFMT) != S_IFDIR)) {
            fprintf(stderr, "bdb_public_private_open: Unable to determine dbhome from %s\n", db_filename);
            return EINVAL;
        }
    }
    li->li_config_mutex = PR_NewLock();
    conf->bdb_dbhome_directory = slapi_ch_strdup(dbhome);
    if (rw) {
        /* Setup a fully transacted environment */
        priv->dblayer_env = NULL;
        conf->bdb_enable_transactions = 0;
        conf->bdb_tx_max = 50;
        rc = bdb_start(li, DBLAYER_NORMAL_MODE);
    } else {
        /* Setup minimal environment */
        rc = db_env_create(&bdb_env, 0);
        if (rc == 0) {
            flags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE;
            rc = bdb_env->open(bdb_env, NULL, flags, 0);
        }
    }

    if (rc == 0) {
        rc = dbbdb_create_db_for_open(be, "bdb_public_private_open", DB_CREATE | DB_THREAD, (DB**)db, bdb_env);
        bdb_db = *db;
    }
    if (rc == 0) {
        if (rw) {
            DB_OPEN((*ppEnv)->bdb_openflags,
                    (DB*)*db, NULL /* txnid */, db_filename, NULL /* subname */, DB_BTREE,
                    DB_CREATE | DB_THREAD, priv->dblayer_file_mode, rc);
        } else {
            rc = bdb_db->open(bdb_db, NULL, db_filename, NULL, DB_UNKNOWN, DB_RDONLY, 0);
        }
    }

    *env = bdb_env;
    *db = bdb_db;

    return bdb_map_error(__FUNCTION__, rc);
}

int
bdb_public_private_close(dbi_env_t **env, dbi_db_t **db)
{
    DB_ENV *bdb_env = *env;
    DB *bdb_db = *db;
    int rc = 0;

    if (bdb_db) {
        rc = bdb_db->close(bdb_db, 0);
    }
    if (bdb_env) {
        rc = bdb_env->close(bdb_env, 0);
    }
    *db = NULL;
    *env = NULL;
    return bdb_map_error(__FUNCTION__, rc);
}

/*
 * Walk all database files of a backend (for dbscan)
 *
 * directory: The path to examine.
 * subdir: subdirname(should be NULL when calling the function except for the recursion case)
 * cb: context struct holding the data
 * cbctx: cb user context
 */
static
int bdb_walk_dbfiles (const char *directory, const char *subdir,
                     void (*cb)(const char *dbname, void *cbctx), void *cbctx)
{
    int return_value = 0;
    PRDir *dirhandle = NULL;
    int len = strlen (DB_REGION_PREFIX);

    dirhandle = PR_OpenDir (directory);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        char *direntry_name, *pt;
        PRFileInfo64 info;

        while (NULL != (direntry = PR_ReadDir (dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name) {
                break;
            }
            direntry_name = PR_smprintf ("%s/%s", directory, direntry->name);
            if (PR_GetFileInfo64 (direntry_name, &info) != PR_SUCCESS) {
                PR_smprintf_free (direntry_name);
                continue;
            }
            if (PR_FILE_DIRECTORY == info.type) {
                /* Recurse into this directory but not any further. This is
                 * because each instance gets its own directory, but in those
                 * directories there should be only .db3 files. There should
                 * not be anymore directories in an instance directory.
                 */
                if (!subdir) {
                    bdb_walk_dbfiles (direntry_name, direntry->name, cb, cbctx);
                }
                PR_smprintf_free (direntry_name);
                continue;
            }
            PR_smprintf_free (direntry_name);
            if (strncmp(DB_REGION_PREFIX, direntry->name, len) == 0) {
                continue;
            }
            pt = strrchr (direntry->name, *LDBM_FILENAME_SUFFIX);
            if (!pt || strcmp (pt, LDBM_FILENAME_SUFFIX)) {
                continue;
            }
            if (subdir) {
                pt = PR_smprintf ("%s/%s", subdir, direntry->name);
                cb (pt, cbctx);
                PR_smprintf_free (pt);
            } else {
                cb (direntry->name, cbctx);
            }
        }
        PR_CloseDir (dirhandle);
    } else {
        return_value = -1;
    }
    return return_value;
}

static void
dbslist_count_space_to_reserve(const char * dbname, void *cbctx)
{
    dbi_dbslist_ctx_t *ctx = cbctx;
    ctx->maxdbs++;
    ctx->nbdbs++;
}

static void
dbslist_store_a_db(const char * dbname, void *cbctx)
{
    dbi_dbslist_ctx_t *ctx = cbctx;
    if (ctx->nbdbs < ctx->maxdbs) {
        PL_strncpyz (ctx->list[ctx->nbdbs++].filename, dbname, MAXPATHLEN);
    }
}

dbi_dbslist_t *
bdb_list_dbs (const char * dbhome)
{
    dbi_dbslist_ctx_t cbctx = { 0 };
    int rc = bdb_walk_dbfiles (dbhome, NULL, dbslist_count_space_to_reserve, &cbctx);
    if (rc) {
        return NULL;
    }
    cbctx.nbdbs++;              /* Reserve space for empty filename that marks end of list */
    cbctx.list = (dbi_dbslist_t *) slapi_ch_calloc (cbctx.nbdbs, sizeof (dbi_dbslist_t));
    cbctx.nbdbs = 0;
    bdb_walk_dbfiles (dbhome, NULL, dbslist_store_a_db, &cbctx);
    return cbctx.list;
}

/* [605974] check adb region file's existence to know whether import is executed by other process or not */
int
bdb_public_in_import (ldbm_instance * inst)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int rval = 0;

    inst_dirp = dblayer_get_full_inst_dir (inst->inst_li, inst, inst_dir, MAXPATHLEN);
    if (!inst_dirp || !*inst_dirp) {
        rval = -1;
       goto done;
    }
    dirhandle = PR_OpenDir (inst_dirp);
    if (NULL == dirhandle)
        goto done;

    while (NULL != (direntry = PR_ReadDir (dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name) {
            break;
        }
        if (0 == strncmp (direntry->name, DB_REGION_PREFIX, 5)) {
            rval = 1;
            break;
        }
    }
    PR_CloseDir (dirhandle);
  done:
    if (inst_dirp != inst_dir) {
        slapi_ch_free_string (&inst_dirp);
    }
    return rval;
}

const char *
bdb_public_get_db_suffix(void)
{
    return LDBM_FILENAME_SUFFIX;
}

int
bdb_public_dblayer_compact(Slapi_Backend *be, PRBool just_changelog)
{
    struct ldbminfo *li = NULL;
    int rc = -1;

    li = (struct ldbminfo *)be->be_database->plg_private;
    bdb_force_checkpoint(li);
    rc = bdb_do_compact(li, just_changelog);
    bdb_force_checkpoint(li);
    return rc;
}

int
bdb_do_compact(struct ldbminfo *li, PRBool just_changelog)
{
    Object *inst_obj;
    ldbm_instance *inst;
    DB *db = NULL;
    int rc = 0;

    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_do_compact",
                  "Compacting databases ...\n");
    for (inst_obj = objset_first_obj(li->li_instance_set);
        inst_obj;
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (!just_changelog) {
            rc = dblayer_get_id2entry(inst->inst_be, (dbi_db_t **)&db);
            if (!db || rc) {
                continue;
            }
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_do_compact",
                    "Compacting DB: %s\n", inst->inst_name);
            rc = bdb_db_compact_one_db(db, inst);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_do_compact",
                        "failed to compact id2entry for %s; db error - %d %s\n",
                        inst->inst_name, rc, db_strerror(rc));
                break;
            }
        }

        /* Compact changelog db */
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_do_compact",
                "Compacting Replication Changelog: %s\n", inst->inst_name);
        dblayer_get_changelog(inst->inst_be, (dbi_db_t **)&db, 0);
        if (db) {
            rc = bdb_db_compact_one_db(db, inst);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "bdb_do_compact",
                        "failed to compact changelog for %s; db error - %d %s\n",
                        inst->inst_name, rc, db_strerror(rc));
                break;
            }
        }
    }

    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_do_compact", "Compacting databases finished.\n");

    return rc;
}

int
bdb_public_delete_db(backend *be, dbi_db_t *db)
{
    /* Used in dbscan context */
    char dbName[MAXPATHLEN];

    PL_strncpyz(dbName, bdb_public_get_db_filename(db), MAXPATHLEN);
    bdb_close_file((DB**)&db);
    return unlink(dbName);
}

int
bdb_dblayer_cursor_iterate(dbi_cursor_t *cursor, dbi_iterate_cb_t *action_cb,
                           const dbi_val_t *startingkey, void *ctx)
{
    DBC *bdb_cur = (DBC*)cursor->cur;
    DBT bdb_key = {0};
    DBT bdb_data = {0};
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    int rc = 0;
    
    if (bdb_cur == NULL) {
        return  DBI_RC_INVALID;
    }

    bdb_key.flags = DB_DBT_REALLOC;
    bdb_data.flags = DB_DBT_REALLOC;
    if (startingkey && startingkey->data && startingkey->size) {
        bdb_key.data = slapi_ch_malloc(startingkey->size);
        memcpy(bdb_key.data, startingkey->data, startingkey->size);
        bdb_key.size = bdb_key.ulen = startingkey->size;
        rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_SET_RANGE);
    } else {
        rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_FIRST);
    }
    while (rc == 0) {
        key.data = bdb_key.data;
        key.size = bdb_key.size;
        data.data = bdb_data.data;
        data.size = bdb_data.size;
        rc = action_cb(&key, &data, ctx);
        if (rc == DBI_RC_NOTFOUND) {
            rc = DBI_RC_SUCCESS;
            break;
        }
        rc = bdb_cur->c_get(bdb_cur, &bdb_key, &bdb_data, DB_NEXT);
    }
    if (rc == DB_NOTFOUND) {
        rc = DBI_RC_NOTFOUND;
    } else if (rc != DBI_RC_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_dblayer_cursor_iterate",
                "Database error while iterating a cursor ; db error - %d %s\n",
                rc, db_strerror(rc));
        rc = bdb_map_error(__FUNCTION__, rc);
    }
    slapi_ch_free(&bdb_key.data);
    slapi_ch_free(&bdb_data.data);
    return rc;
}
