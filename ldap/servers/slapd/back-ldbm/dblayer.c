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
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
    Abstraction layer which sits between db2.0 and 
    higher layers in the directory server---typically
    the back-end.
    This module's purposes are 1) to hide messy stuff which
    db2.0 needs, and with which we don't want to pollute the back-end
    code. 2) Provide some degree of portability to other databases
    if that becomes a requirement. Note that it is NOT POSSIBLE
    to revert to db1.85 because the backend is now using features
    from db2.0 which db1.85 does not have.
    Also provides an emulation of the ldbm_ functions, for anyone
    who is still calling those. The use of these functions is
    deprecated. Only for backwards-compatibility.
    Blame: dboreham
*/

/* Return code conventions: 
    Unless otherwise advertised, all the functions in this module
    return an int which is zero if the operation was successful
    and non-zero if it wasn't. If the return'ed value was > 0,
    it can be interpreted as a system errno value. If it was < 0,
    its meaning is defined in dblayer.h
*/

/*
    Some information about how this stuff is to be used:
    
    Call dblayer_init() near the beginning of the application's life.
    This allocates some resources and allows the config line processing
    stuff to work.
    Call dblayer_start() when you're sure all config stuff has been seen.
    This needs to be called before you can do anything else.
    Call dblayer_close() when you're finished using the db and want to exit.
    This closes and flushes all files opened by your application since calling
    dblayer_start. If you do NOT call dblayer_close(), we assume that the
    application crashed, and initiate recover next time you call dblayer_start().
    Call dblayer_terminate() after close. This releases resources.

    DB* handles are retrieved from dblayer via these functions:

    dblayer_get_id2entry()
    dblayer_get_index_file()

    the caller must honour the protocol that these handles are released back
    to dblayer when you're done using them, use thse functions to do this:

    dblayer_release_id2entry()
    dblayer_release_index_file()


*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "back-ldbm.h"
#include "dblayer.h"
#include <prthread.h>
#include <prclist.h>
#ifndef XP_WIN32
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#endif

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4100
#define DB_OPEN(oflags, db, txnid, file, database, type, flags, mode, rval)    \
{                                                                              \
    if (((oflags) & DB_INIT_TXN) && ((oflags) & DB_INIT_LOG))                  \
    {                                                                          \
        (rval) = ((db)->open)((db), (txnid), (file), (database), (type), (flags)|DB_AUTO_COMMIT, (mode)); \
    }                                                                          \
    else                                                                       \
    {                                                                          \
        (rval) = ((db)->open)((db), (txnid), (file), (database), (type), (flags), (mode)); \
    }                                                                          \
}
/* 608145: db4.1 and newer does not require exclusive lock for checkpointing 
 * and transactions */
#define DB_CHECKPOINT_LOCK(use_lock, lock) ;
#define DB_CHECKPOINT_UNLOCK(use_lock, lock) ;
#else /* older then db 41 */
#define DB_OPEN(oflags, db, txnid, file, database, type, flags, mode, rval)    \
    (rval) = (db)->open((db), (file), (database), (type), (flags), (mode))
#define DB_CHECKPOINT_LOCK(use_lock, lock) if(use_lock) slapi_rwlock_wrlock(lock);
#define DB_CHECKPOINT_UNLOCK(use_lock, lock) if(use_lock) slapi_rwlock_unlock(lock);
#endif

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4000
#define DB_ENV_SET_REGION_INIT(env) (env)->set_flags((env), DB_REGION_INIT, 1)
#define TXN_BEGIN(env, parent_txn, tid, flags) \
    (env)->txn_begin((env), (parent_txn), (tid), (flags))
#define TXN_COMMIT(txn, flags) (txn)->commit((txn), (flags))
#define TXN_ABORT(txn) (txn)->abort(txn)
#define TXN_CHECKPOINT(env, kbyte, min, flags) \
    (env)->txn_checkpoint((env), (kbyte), (min), (flags))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
    (env)->memp_stat((env), (gsp), (fsp), (flags))
#define MEMP_TRICKLE(env, pct, nwrotep) \
    (env)->memp_trickle((env), (pct), (nwrotep))
#define LOG_ARCHIVE(env, listp, flags, malloc) \
    (env)->log_archive((env), (listp), (flags))
#define LOG_FLUSH(env, lsn) (env)->log_flush((env), (lsn))
#define LOCK_DETECT(env, flags, atype, aborted) \
    (env)->lock_detect((env), (flags), (atype), (aborted))
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4400 /* db4.4 or later */
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
    (env)->mutex_set_tas_spins((env), (tas_spins))
#else /* < 4.4 */
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
    (env)->set_tas_spins((env), (tas_spins))
#endif /* 4.4 or later */
#else    /* older than db 4.0 */
#define DB_ENV_SET_REGION_INIT(env) db_env_set_region_init(1)
#define DB_ENV_SET_TAS_SPINS(env, tas_spins) \
    db_env_set_tas_spins((tas_spins))
#define TXN_BEGIN(env, parent_txn, tid, flags) \
    txn_begin((env), (parent_txn), (tid), (flags))
#define TXN_COMMIT(txn, flags) txn_commit((txn), (flags))
#define TXN_ABORT(txn) txn_abort((txn))
#define TXN_CHECKPOINT(env, kbyte, min, flags) \
    txn_checkpoint((env), (kbyte), (min), (flags))
#define MEMP_TRICKLE(env, pct, nwrotep) memp_trickle((env), (pct), (nwrotep))
#define LOG_FLUSH(env, lsn) log_flush((env), (lsn))
#define LOCK_DETECT(env, flags, atype, aborted) \
    lock_detect((env), (flags), (atype), (aborted))

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
#define MEMP_STAT(env, gsp, fsp, flags, malloc) memp_stat((env), (gsp), (fsp))
#define LOG_ARCHIVE(env, listp, flags, malloc) \
    log_archive((env), (listp), (flags))

#else    /* older than db 3.3 */
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
    memp_stat((env), (gsp), (fsp), (malloc))
#define LOG_ARCHIVE(env, listp, flags, malloc) \
    log_archive((env), (listp), (flags), (malloc))
#endif
#endif

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
#define INCR_THREAD_COUNT(priv) \
    PR_Lock(priv->thread_count_lock); \
    ++priv->dblayer_thread_count; \
    PR_Unlock(priv->thread_count_lock)

#define DECR_THREAD_COUNT(priv) \
    PR_Lock(priv->thread_count_lock); \
    if (--priv->dblayer_thread_count == 0) { \
        PR_NotifyCondVar(priv->thread_count_cv); \
    } \
    PR_Unlock(priv->thread_count_lock)

#define NEWDIR_MODE 0755
#define DB_REGION_PREFIX "__db."

static int perf_threadmain(void *param);
static int checkpoint_threadmain(void *param);
static int trickle_threadmain(void *param);
static int deadlock_threadmain(void *param);
static int commit_good_database(dblayer_private *priv);
static int read_metadata(struct ldbminfo *li);
static int count_dbfiles_in_dir(char *directory, int *count, int recurse);
static int dblayer_override_libdb_functions(DB_ENV *pEnv, dblayer_private *priv);
static int dblayer_force_checkpoint(struct ldbminfo *li);
static int log_flush_threadmain(void *param);
static int dblayer_delete_transaction_logs(const char * log_dir);
static int dblayer_is_logfilename(const char* path);
static int dblayer_start_log_flush_thread(dblayer_private *priv);
static int dblayer_start_deadlock_thread(struct ldbminfo *li);
static int dblayer_start_checkpoint_thread(struct ldbminfo *li);
static int dblayer_start_trickle_thread(struct ldbminfo *li);
static int dblayer_start_perf_thread(struct ldbminfo *li);
static int dblayer_start_txn_test_thread(struct ldbminfo *li);
static int trans_batch_count=1;
static int trans_batch_limit=0;
static PRBool log_flush_thread=PR_FALSE;
static int dblayer_db_remove_ex(dblayer_private_env *env, char const path[], char const dbName[], PRBool use_lock);
static void dblayer_init_pvt_txn();
static void dblayer_push_pvt_txn(back_txn *txn);
static back_txn *dblayer_get_pvt_txn();
static void dblayer_pop_pvt_txn();

#define MEGABYTE (1024 * 1024)
#define GIGABYTE (1024 * MEGABYTE)

/* env. vars. you can set to stress txn handling */
#define TXN_TESTING "TXN_TESTING" /* enables the txn test thread */
#define TXN_TEST_HOLD_MSEC "TXN_TEST_HOLD_MSEC" /* time to hold open the db cursors */
#define TXN_TEST_LOOP_MSEC "TXN_TEST_LOOP_MSEC" /* time to wait before looping again */
#define TXN_TEST_USE_TXN "TXN_TEST_USE_TXN" /* use transactions or not */
#define TXN_TEST_USE_RMW "TXN_TEST_USE_RMW" /* use DB_RMW for c_get flags or not */
#define TXN_TEST_INDEXES "TXN_TEST_INDEXES" /* list of indexes to use - comma delimited - id2entry,entryrdn,etc. */
#define TXN_TEST_VERBOSE "TXN_TEST_VERBOSE" /* be wordy */

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
dblayer_bt_compare(DB *db, const DBT *dbt1, const DBT *dbt2)
{
    struct berval bv1, bv2;
    value_compare_fn_type syntax_cmp_fn = (value_compare_fn_type)db->app_private;

    if ((dbt1->data && (dbt1->size>1) && (*((char*)dbt1->data) == EQ_PREFIX)) &&
        (dbt2->data && (dbt2->size>1) && (*((char*)dbt2->data) == EQ_PREFIX))) {
        bv1.bv_val = (char *)dbt1->data+1; /* remove leading '=' */
        bv1.bv_len = (ber_len_t)dbt1->size-1;

        bv2.bv_val = (char *)dbt2->data+1; /* remove leading '=' */
        bv2.bv_len = (ber_len_t)dbt2->size-1;

        return syntax_cmp_fn(&bv1, &bv2);
    }

    /* else compare two "raw" index keys */
    bv1.bv_val = (char *)dbt1->data;
    bv1.bv_len = (ber_len_t)dbt1->size;

    bv2.bv_val = (char *)dbt2->data;
    bv2.bv_len = (ber_len_t)dbt2->size;

    return slapi_berval_cmp(&bv1, &bv2);
}

static int db_uses_feature(DB_ENV *db_env, u_int32_t flags)
{
    u_int32_t openflags = 0;
    PR_ASSERT(db_env);
    (*db_env->get_open_flags)(db_env, &openflags);

    return (flags & openflags);
}

int dblayer_db_uses_locking(DB_ENV *db_env) {
    return db_uses_feature(db_env, DB_INIT_LOCK);
}

int dblayer_db_uses_transactions(DB_ENV *db_env) {
    return db_uses_feature(db_env, DB_INIT_TXN);
}

int dblayer_db_uses_mpool(DB_ENV *db_env) {
    return db_uses_feature(db_env, DB_INIT_MPOOL);
}

int dblayer_db_uses_logging(DB_ENV *db_env) {
    return db_uses_feature(db_env, DB_INIT_LOG);
}

/* this flag use if user remotely turned batching off */

#define FLUSH_REMOTEOFF -1 
/* routine that allows batch value to be changed remotely:

    1. value = 0 turns batching off
    2. value = 1 makes behavior be like 5.0 but leaves batching on
    3. value > 1 changes batch value

    2 and 3 assume that nsslapd-db-transaction-batch-val is greater 0 at startup
*/

int
dblayer_set_batch_transactions(void *arg, void *value, char *errorbuf, int phase, int apply) {
    int val = (int)((uintptr_t)value);
    int retval = LDAP_SUCCESS;

    if (apply) {
        if(phase == CONFIG_PHASE_STARTUP) {
            trans_batch_limit=val;
        } else if(trans_batch_limit != FLUSH_REMOTEOFF ) { 
            if((val == 0) && (log_flush_thread)) { 
                log_flush_thread=PR_FALSE;
                trans_batch_limit = FLUSH_REMOTEOFF;
            } else if(val > 0) { 
                trans_batch_limit=val;
            }
        }
    }
    return retval;
}

void *
dblayer_get_batch_transactions(void *arg) {
    return (void *)((uintptr_t)trans_batch_limit);
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
dblayer_txn_checkpoint(struct ldbminfo *li, struct dblayer_private_env *env,
                       PRBool use_lock, PRBool busy_skip, PRBool db_force)
{
    int ret = 0;
    if (busy_skip && is_anyinstance_busy(li))
    {
        return ret;
    }
    DB_CHECKPOINT_LOCK(use_lock, env->dblayer_env_lock);
    ret = TXN_CHECKPOINT(env->dblayer_DB_ENV, 0, 0, db_force?DB_FORCE:0);
    DB_CHECKPOINT_UNLOCK(use_lock, env->dblayer_env_lock);
    return ret;
}

static int _dblayer_check_version(dblayer_private *priv)
{
    int major, minor = 0;
    char *string = 0;
    int ret = 0; 

    string = db_version(&major,&minor,NULL);
    if (major < DB_VERSION_MAJOR)
    {
        ret = -1;
    } 
    else
    {
        ret = 0;
    }
    /* DB3X: always POST 24 :) */
    priv->dblayer_lib_version = DBLAYER_LIB_VERSION_POST_24;
    LDAPDebug(LDAP_DEBUG_TRACE,"version check: %s (%d.%d)\n", string, major, minor);
    return ret;
}


/*
 * return nsslapd-db-home-directory (dblayer_dbhome_directory), if exists.
 * Otherwise, return nsslapd-directory (dblayer_home_directory).
 *
 * if dblayer_dbhome_directory exists, set 1 to dbhome.
 */
char *
dblayer_get_home_dir(struct ldbminfo *li, int *dbhome)
{
    dblayer_private *priv = (dblayer_private*)li->li_dblayer_private;
    char *home_dir = priv->dblayer_home_directory;
    if (dbhome)
        *dbhome = 0;

    if (priv->dblayer_dbhome_directory && *(priv->dblayer_dbhome_directory))
    {
        if (dbhome)
            *dbhome = 1;
        home_dir = priv->dblayer_dbhome_directory;
    }
    if (NULL == home_dir)
    {
        LDAPDebug(LDAP_DEBUG_ANY,"Db home directory is not set. "
            "Possibly %s (optionally %s) is missing in the config file.\n",
            CONFIG_DIRECTORY, CONFIG_DB_HOME_DIRECTORY, 0);
    }
    return home_dir;
}

/* Helper function which deletes the persistent state of the database library
 * IMHO this should be in inside libdb, but keith won't have it. 
 * Stop press---libdb now does delete these files on recovery, so we don't call this any more.
 */
static void dblayer_reset_env(struct ldbminfo *li)
{
    /* Remove the memory regions */
    dblayer_private *priv = (dblayer_private*)li->li_dblayer_private;
    DB_ENV *pEnv = priv->dblayer_env->dblayer_DB_ENV;
    char *home_dir = dblayer_get_home_dir(li, NULL);
    if (home_dir && *home_dir)
        pEnv->remove(pEnv, home_dir, DB_FORCE);
}

/* Callback function for libdb to spit error info into our log */
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4300
void dblayer_log_print(const DB_ENV *dbenv, const char* prefix,
                       const char *buffer)
#else
void dblayer_log_print(const char* prefix, char *buffer)
#endif
{
    /* We ignore the prefix since we know who we are anyway */
    LDAPDebug(LDAP_DEBUG_ANY,"libdb: %s\n", buffer, 0, 0);    
}

void dblayer_remember_disk_filled(struct ldbminfo *li)
{
    dblayer_private *priv = NULL;

    PR_ASSERT(NULL != li);
    priv = li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    priv->dblayer_bad_stuff_happened = 1;
    
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
static int dblayer_open_large(const char *path, int oflag, mode_t mode)
{
    int err;

    err = open64(path, oflag, mode);
    /* weird but necessary: */
    if (err >= 0) errno = 0;
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
int dblayer_open_huge_file(const char *path, int oflag, int mode)
{
    return dblayer_open_large(path, oflag, (mode_t)mode);
}


#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4300
/* Helper function for large seeks, db4.3 */
static int dblayer_seek43_large(int fd, off64_t offset, int whence)
{
    int ret = 0;

    ret = lseek64(fd, offset, whence);

    return (ret < 0) ? errno : 0;
}
#else
/* Helper function for large seeks, db2.4 */
static int dblayer_seek24_large(int fd, size_t pgsize, db_pgno_t pageno,
                u_long relative, int isrewind, int whence)
{
    off64_t offset = 0, ret;

    offset = (off64_t)pgsize * pageno + relative;
    if (isrewind) offset = -offset;
    ret = lseek64(fd, offset, whence);

    return (ret < 0) ? errno : 0;
}
#endif

/* helper function for large fstat -- this depends on 'struct stat64' having
 * the following members:
 *    off64_t        st_size;
 *      long        st_blksize;
 */
static int dblayer_ioinfo_large(const char *path, int fd, u_int32_t *mbytesp,
                u_int32_t *bytesp, u_int32_t *iosizep)
{
    struct stat64 sb;

    if (fstat64(fd, &sb) < 0)
    return (errno);

    /* Return the size of the file. */
    if (mbytesp)
    *mbytesp = (u_int32_t) (sb.st_size / (off64_t) MEGABYTE);
    if (bytesp)
    *bytesp = (u_int32_t) (sb.st_size % (off64_t) MEGABYTE);

    if (iosizep)
    *iosizep = (u_int32_t)(sb.st_blksize);
    return 0;
}
/* Helper function to tell if a file exists */
/* On Solaris, if you use stat() on a file >4Gbytes, it fails with EOVERFLOW, 
   causing us to think that the file does not exist when it in fact does */
static int dblayer_exists_large(char *path, int *isdirp)
{
    struct stat64 sb;

    if (stat64(path, &sb) != 0)
    return (errno);

    if (isdirp != NULL)
        *isdirp = S_ISDIR(sb.st_mode);

    return (0);
}

#else   /* DB_USE_64LFS */

int dblayer_open_huge_file(const char *path, int oflag, int mode)
{
    return open(path, oflag, mode);
}

#endif  /* DB_USE_64LFS */


static int dblayer_override_libdb_functions(DB_ENV *pEnv, dblayer_private *priv)
{
#ifdef DB_USE_64LFS
    int major = 0;
    int minor = 0;

    /* Find out whether we are talking to a 2.3 or 2.4+ libdb */
    db_version(&major, &minor, NULL);

#ifndef irix
    /* irix doesn't have open64() */
    db_env_set_func_open((int (*)(const char *, int, ...))dblayer_open_large);
#endif  /* !irix */
    db_env_set_func_ioinfo(dblayer_ioinfo_large);
    db_env_set_func_exists((int (*)())dblayer_exists_large);
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4300
    db_env_set_func_seek((int (*)())dblayer_seek43_large);
#else
    db_env_set_func_seek((int (*)())dblayer_seek24_large);
#endif

    LDAPDebug(LDAP_DEBUG_TRACE, "Enabled 64-bit files\n", 0, 0, 0);
#endif   /* DB_USE_64LFS */
    return 0;
}



/* This function is called in the initialization code, before the 
 * config file is read in, so we can't do much here
 */
int dblayer_init(struct ldbminfo *li)
{
    /* Allocate memory we need, create mutexes etc. */
    dblayer_private *priv = NULL;
    int ret = 0;

    PR_ASSERT(NULL != li);
    if (NULL != li->li_dblayer_private)
    {
        return -1;
    }
    
    priv = (dblayer_private*) slapi_ch_calloc(1,sizeof(dblayer_private));
    if (NULL == priv)
    {
        /* Memory allocation failed */
        return -1;
    }
    priv->thread_count_lock = PR_NewLock();
    priv->thread_count_cv = PR_NewCondVar(priv->thread_count_lock);
    li->li_dblayer_private = priv;

    /* For now, we call this to get debug printout */
    _dblayer_check_version(priv);

    /* moved db_env_create to dblayer_start */
    return ret;
}

int dblayer_terminate(struct ldbminfo *li)
{
    /* We assume that dblayer_close has been called already */
    dblayer_private *priv = (dblayer_private*)li->li_dblayer_private;
    Object *inst_obj;
    ldbm_instance *inst;
    int rval = 0;

    if (NULL == priv)    /* already terminated.  nothing to do */
        return rval;

    /* clean up mutexes */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (NULL != inst->inst_db_mutex) {
            PR_DestroyMonitor(inst->inst_db_mutex);
            inst->inst_db_mutex = NULL;
        }
        if (NULL != inst->inst_handle_list_mutex) {
            PR_DestroyLock(inst->inst_handle_list_mutex);
            inst->inst_handle_list_mutex = NULL;
        }
    }
    
    slapi_ch_free_string(&priv->dblayer_log_directory);
    PR_DestroyCondVar(priv->thread_count_cv);
    priv->thread_count_cv = NULL;
    PR_DestroyLock(priv->thread_count_lock);
    priv->thread_count_lock = NULL;
    slapi_ch_free((void**)&priv);
    li->li_dblayer_private = NULL;

    if (config_get_entryusn_global()) {
        slapi_counter_destroy(&li->li_global_usn_counter);
    }

    return 0;
}

static void dblayer_select_ncache(size_t cachesize, int *ncachep)
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
#if defined(__LP64__) || defined (_LP64)
	if ( (sizeof(cachesize) > 4) && (cachesize > (4L * GIGABYTE))) {
		*ncachep = (cachesize / (4L * GIGABYTE)) + 1;
		LDAPDebug(LDAP_DEBUG_ANY,"Setting ncache to: %d to keep each chunk below 4Gbytes\n",
			*ncachep, 0, 0);
	}
#endif
	/* On Windows, we know that it's hard to allocate more than some 
	 * maximum chunk. In that case
	 * we set ncache to a sensible value.
	 */
#if defined(_WIN32)
	{
		size_t max_windows_chunk = (300 * MEGABYTE); /* This number was determined empirically on Win2k */
		if (cachesize > max_windows_chunk) {
			*ncachep = (cachesize / max_windows_chunk) + 1;
			LDAPDebug(LDAP_DEBUG_ANY,"Setting ncache to: %d for Windows memory address space fragmentation\n",
                *ncachep, 0, 0);
		}
	}
#endif
}

/* This function is no longer called : 
   It attempts to find the maximum allocatable chunk size
   by performing the actual allocations. However as a result
   it allocates pretty much _all_ the available memory, 
   causing the actual cache allocation to fail later.
   If there turns out to be a good safe way to determine
   the maximum chunk size, then we should use that instead.
   For now we just guess in dblayer_pick_ncache().
 */
#if 0
static void dblayer_get_ncache(size_t cachesize, int *ncachep)
{
    int myncache;
    int mymaxncache;
    int found = 0;
    char **head;

    if (*ncachep <= 0)    /* negative ncache is not allowed */
        myncache = 1;
    else
        myncache = *ncachep;

    mymaxncache = myncache + 20;    /* should be reasonable */

    head = (char **)slapi_ch_malloc(mymaxncache * sizeof(char *));
    do {
        int i;
        int end;
        size_t sz;
        size_t firstsz;
        size_t rest;

        rest = cachesize % myncache;
        sz = cachesize / myncache;
        firstsz = sz + rest;
        end = myncache;
        for (i = 0; i < myncache; i++) {
            if (i == 0)
                head[i] = (char *)malloc(firstsz);
            else
                head[i] = (char *)malloc(sz);
            if (NULL == head[i]) {
                end = i;
                myncache++;
                goto cleanup;
            }
        }
        found = 1;
cleanup:
        for (i = 0; i < end; i++) {
            slapi_ch_free((void **)&head[i]);
        }
        if (myncache == mymaxncache) {
            LDAPDebug(LDAP_DEBUG_ANY,
                "WARNING: dbcachesize %lu too big\n", cachesize, 0, 0);
            myncache = 0;
            found = -1;
        }
    } while (0 == found);
    *ncachep = myncache;
    slapi_ch_free((void **)&head);
    return;
}
#endif

void dblayer_free(void *ptr)
{
    slapi_ch_free(&ptr);
}

static void dblayer_init_dbenv(DB_ENV *pEnv, dblayer_private *priv)
{
    size_t  mysize;
    int     myncache = 1;

    mysize = priv->dblayer_cachesize;
    myncache = priv->dblayer_ncache;
    dblayer_select_ncache(mysize, &myncache); 
    priv->dblayer_ncache = myncache;

    dblayer_set_env_debugging(pEnv,priv);

    pEnv->set_lg_max(pEnv, priv->dblayer_logfile_size);
    pEnv->set_cachesize(pEnv, mysize / GIGABYTE, mysize % GIGABYTE, myncache);
    pEnv->set_lk_max_locks(pEnv, priv->dblayer_lock_config);
    pEnv->set_lk_max_objects(pEnv, priv->dblayer_lock_config);
    pEnv->set_lk_max_lockers(pEnv, priv->dblayer_lock_config); 

    /* shm_key required for named_regions (DB_SYSTEM_MEM) */
    pEnv->set_shm_key(pEnv, priv->dblayer_shm_key);

    /* increase max number of active transactions */
    pEnv->set_tx_max(pEnv, priv->dblayer_tx_max);

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
    pEnv->set_alloc(pEnv, (void *)slapi_ch_malloc, (void *)slapi_ch_realloc, dblayer_free);

    /* 
     * The log region is used to store filenames and so needs to be
     * increased in size from the default for a large number of files. 
     */
    pEnv->set_lg_regionmax(pEnv, 1 * 1048576); /* 1 MB */
#endif
}

/* returns system pagesize (in bytes) and the number of pages of physical
 * RAM this machine has.
 * as a bonus, if 'procpages' is non-NULL, it will be filled in with the
 * approximate number of pages this process is using!
 * on platforms that we haven't figured out how to do this yet, both fields
 * are filled with zero and you're on your own.
 *
 * platforms supported so far:
 * Solaris, Linux, Windows
 */
#ifdef OS_solaris
#include <sys/procfs.h>
#endif
#ifdef LINUX
#include <linux/kernel.h>
#include <sys/sysinfo.h>    /* undocumented (?) */
#endif
#if defined ( hpux )
#include <sys/pstat.h>
#endif

#if !defined(_WIN32)
static size_t dblayer_getvirtualmemsize()
{
    struct rlimit rl;

    /* the maximum size of a process's total available memory, in bytes */
    getrlimit(RLIMIT_AS, &rl);
    return rl.rlim_cur;
}
#endif

/* pages = number of pages of physical ram on the machine (corrected for 32-bit build on 64-bit machine).
 * procpages = pages currently used by this process (or working set size, sometimes)
 * availpages = some notion of the number of pages 'free'. Typically this number is not useful.
 */
void dblayer_sys_pages(size_t *pagesize, size_t *pages, size_t *procpages, size_t *availpages)
{
    *pagesize = *pages = *availpages = 0;
    if (procpages)
        *procpages = 0;

#ifdef _WIN32
    {
        SYSTEM_INFO si;
        MEMORYSTATUS ms;

        GetSystemInfo(&si);
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatus(&ms);
        *pagesize = si.dwPageSize;
        *pages = ms.dwTotalPhys  / si.dwPageSize;
        *availpages = ms.dwAvailVirtual / *pagesize;
        if (procpages) {
            DWORD minwss = 0, maxwss = 0;

            GetProcessWorkingSetSize(GetCurrentProcess(), &minwss, &maxwss);
            *procpages = (int)(maxwss / si.dwPageSize);
        }
    }
#endif

#ifdef OS_solaris
    *pagesize = (int)sysconf(_SC_PAGESIZE);
    *pages = (int)sysconf(_SC_PHYS_PAGES);
    *availpages = dblayer_getvirtualmemsize() / *pagesize;
    /* solaris has THE most annoying way to get this info */
    if (procpages) {
        struct prpsinfo psi;
        char fn[40];
        int fd;

        sprintf(fn, "/proc/%d", getpid());
        fd = open(fn, O_RDONLY);
        if (fd >= 0) {
                memset(&psi, 0, sizeof(psi));
            if (ioctl(fd, PIOCPSINFO, (void *)&psi) == 0)
                *procpages = psi.pr_size;
            close(fd);
        }
    }
#endif

#ifdef LINUX
    {
        struct sysinfo si;
        size_t pages_per_mem_unit = 0;
        size_t mem_units_per_page = 0; /* We don't know if these units are really pages */

        sysinfo(&si);
        *pagesize = getpagesize();
        if (si.mem_unit > *pagesize) {
            pages_per_mem_unit = si.mem_unit / *pagesize;
            *pages = si.totalram * pages_per_mem_unit;
        } else {
            mem_units_per_page = *pagesize / si.mem_unit;
            *pages = si.totalram / mem_units_per_page;
        }
        *availpages = dblayer_getvirtualmemsize() / *pagesize;
        /* okay i take that back, linux's method is more retarded here.
         * hopefully linux doesn't have the FILE* problem that solaris does
         * (where you can't use FILE if you have more than 256 fd's open)
         */
        if (procpages) {
            FILE *f;
            char fn[40], s[80];

            sprintf(fn, "/proc/%d/status", getpid());
            f = fopen(fn, "r");
            if (!f)    /* fopen failed */
                return;
            while (! feof(f)) {
                fgets(s, 79, f);
                if (feof(f))
                    break;
                if (strncmp(s, "VmSize:", 7) == 0) {
                    sscanf(s+7, "%lu", procpages);
                    break;
                }
            }
            fclose(f);
            /* procpages is now in 1k chunks, not pages... */
            *procpages /= (*pagesize / 1024);
        }
    }
#endif

#if defined ( hpux )
    {
        struct pst_static pst;
        int rval = pstat_getstatic(&pst, sizeof(pst), (size_t)1, 0);
        if (rval < 0)    /* pstat_getstatic failed */
            return;
        *pagesize = pst.page_size;
        *pages = pst.physical_memory;
        *availpages = dblayer_getvirtualmemsize() / *pagesize;
        if (procpages)
        {
#define BURST (size_t)32        /* get BURST proc info at one time... */
            struct pst_status psts[BURST];
            int i, count;
            int idx = 0; /* index within the context */
            int mypid = getpid();

            *procpages = 0;
            /* loop until count == 0, will occur all have been returned */
            while ((count = pstat_getproc(psts, sizeof(psts[0]), BURST, idx)) > 0) {
                /* got count (max of BURST) this time.  process them */
                for (i = 0; i < count; i++) {
                    if (psts[i].pst_pid == mypid)
                    {
                        *procpages = (size_t)(psts[i].pst_dsize + psts[i].pst_tsize + psts[i].pst_ssize);
                        break;
                    }
                }
                if (i < count)
                    break;

                /*
                 * now go back and do it again, using the next index after
                 * the current 'burst'
                 */
                idx = psts[count-1].pst_idx + 1;
            }
        }
    }
#endif
    /* If this is a 32-bit build, it might be running on a 64-bit machine,
     * in which case, if the box has tons of ram, we can end up telling 
     * the auto cache code to use more memory than the process can address.
     * so we cap the number returned here.
     */
#if defined(__LP64__) || defined (_LP64)
#else
    {    
        size_t one_gig_pages = GIGABYTE / *pagesize;
        if (*pages > (2 * one_gig_pages) ) {
            LDAPDebug(LDAP_DEBUG_TRACE,"More than 2Gbytes physical memory detected. Since this is a 32-bit process, truncating memory size used for auto cache calculations to 2Gbytes\n",
                0, 0, 0);
            *pages = (2 * one_gig_pages);
        }
    }
#endif
}


int dblayer_is_cachesize_sane(size_t *cachesize)
{
    size_t pages = 0, pagesize = 0, procpages = 0, availpages = 0;
    int issane = 1;

    dblayer_sys_pages(&pagesize, &pages, &procpages, &availpages);
    if (!pagesize || !pages)
        return 1;    /* do nothing when we can't get the avail mem */
    /* If the requested cache size is larger than the remaining pysical memory
     * after the current working set size for this process has been subtracted,
     * then we say that's insane and try to correct.
     */
    issane = (int)(*cachesize / pagesize) <= (pages - procpages);
    if (!issane) {
        *cachesize = (size_t)((pages - procpages) * pagesize);
    }
    /* We now compensate for DB's own compensation for metadata size 
     * They increase the actual cache size by 25%, but only for sizes
     * less than 500Meg.
     */
    if (*cachesize < 500*MEGABYTE) {
        *cachesize = (size_t)((double)*cachesize * (double)0.8);
    }
    
    return issane;
}


static void dblayer_dump_config_tracing(dblayer_private *priv)
{
    if (priv->dblayer_home_directory) {
        LDAPDebug(LDAP_DEBUG_TRACE,"home_directory=%s\n",priv->dblayer_home_directory,0,0);
    }
    if (priv->dblayer_log_directory) {
        LDAPDebug(LDAP_DEBUG_TRACE,"log_directory=%s\n",priv->dblayer_log_directory,0,0);
    }
    if (priv->dblayer_dbhome_directory) {
        LDAPDebug(LDAP_DEBUG_TRACE,"dbhome_directory=%s\n",priv->dblayer_dbhome_directory,0,0);
    }
    LDAPDebug(LDAP_DEBUG_TRACE,"trickle_percentage=%d\n",priv->dblayer_trickle_percentage,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"page_size=%lu\n",priv->dblayer_page_size,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"index_page_size=%lu\n",priv->dblayer_index_page_size,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"cachesize=%lu\n",priv->dblayer_cachesize,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"previous_cachesize=%lu\n",priv->dblayer_previous_cachesize,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"ncache=%d\n",priv->dblayer_ncache,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"previous_ncache=%d\n",priv->dblayer_previous_ncache,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"recovery_required=%d\n",priv->dblayer_recovery_required,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"durable_transactions=%d\n",priv->dblayer_durable_transactions,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"checkpoint_interval=%d\n",priv->dblayer_checkpoint_interval,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"transaction_batch_val=%d\n",trans_batch_limit,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"circular_logging=%d\n",priv->dblayer_circular_logging,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"idl_divisor=%d\n",priv->dblayer_idl_divisor,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"logfile_size=%lu\n",priv->dblayer_logfile_size,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"logbuf_size=%lu\n",priv->dblayer_logbuf_size,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"file_mode=%d\n",priv->dblayer_file_mode,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"cache_config=%d\n",priv->dblayer_cache_config,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"lib_version=%d\n",priv->dblayer_lib_version,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"spin_count=%d\n",priv->dblayer_spin_count,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"named_regions=%d\n",priv->dblayer_named_regions,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"private mem=%d\n",priv->dblayer_private_mem,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"private import mem=%d\n",priv->dblayer_private_import_mem,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"shm_key=%ld\n",priv->dblayer_shm_key,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"lockdown=%d\n",priv->dblayer_lockdown,0,0);
    LDAPDebug(LDAP_DEBUG_TRACE,"tx_max=%d\n",priv->dblayer_tx_max,0,0);
}

/* Check a given filesystem directory for access we need */
#define DBLAYER_DIRECTORY_READ_ACCESS 1
#define DBLAYER_DIRECTORY_WRITE_ACCESS 2
#define DBLAYER_DIRECTORY_READWRITE_ACCESS 3
static int dblayer_grok_directory(char *directory, int flags)
{
    /* First try to open the directory using NSPR */
    /* If that fails, we can tell whether it's because it cannot be created or
     * we don't have any permission to access it */
    /* If that works, proceed to try to access files in the directory */
    char       filename[MAXPATHLEN];
    PRDir      *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRFileInfo info;

    dirhandle = PR_OpenDir(directory);
    if (NULL == dirhandle)
    {
        /* it does not exist or wrong file is there */
        /* try delete and mkdir */
        PR_Delete(directory);
        return mkdir_p(directory, 0700);
    }

    while (NULL !=
           (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name)
        {
            break;
        }
        PR_snprintf(filename, MAXPATHLEN, "%s/%s",directory,direntry->name);
        
        /* Right now this is set up to only look at files here. 
         * With multiple instances of the backend the are now other directories
         * in the db home directory.  This function wasn't ment to deal with
         * other directories, so we skip them. */
        if (PR_GetFileInfo(filename, &info) == PR_SUCCESS &&
            info.type == PR_FILE_DIRECTORY) {
            /* go into it (instance dir) */
            int retval = dblayer_grok_directory(filename, flags);
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
            prfd = PR_Open(filename,open_flags,0);
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
                LDAPDebug(LDAP_DEBUG_ANY,
                    "WARNING---no %s permission to file %s\n",
                    access_string,filename,0);
            } else {
                PR_Close(prfd);    /* okay */
            }
        }
    }
    PR_CloseDir(dirhandle);
    return 0;
}

static void
dblayer_set_data_dir(dblayer_private *priv, struct dblayer_private_env *pEnv,
                     char **data_directories)
{
    char **dirp;

    if (!(pEnv->dblayer_priv_flags & DBLAYER_PRIV_SET_DATA_DIR))
    {
        for (dirp = data_directories; dirp && *dirp; dirp++)
        {
            pEnv->dblayer_DB_ENV->set_data_dir(pEnv->dblayer_DB_ENV, *dirp);
        }
        pEnv->dblayer_priv_flags |= DBLAYER_PRIV_SET_DATA_DIR;
    }
}

static int
dblayer_inst_exists(ldbm_instance *inst, char *dbname)
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
dblayer_free_env(struct dblayer_private_env **env)
{
    if (NULL == env || NULL == *env) {
        return;
    }
    if ((*env)->dblayer_env_lock) {
        slapi_destroy_rwlock((*env)->dblayer_env_lock);
        (*env)->dblayer_env_lock = NULL;
    }
    slapi_ch_free((void **)env);
    return;
}

/*
 * create a new DB_ENV and fill it with the goodies from dblayer_private
 */
static int 
dblayer_make_env(struct dblayer_private_env **env, struct ldbminfo *li)
{
    dblayer_private *priv = (dblayer_private*)li->li_dblayer_private;
    struct dblayer_private_env *pEnv;
    char *home_dir = NULL;
    int ret;
    Object *inst_obj;
    ldbm_instance *inst = NULL;

    pEnv = (struct dblayer_private_env *)slapi_ch_calloc(1,
                                                   sizeof(dblayer_private_env));

    if ((ret = db_env_create(&pEnv->dblayer_DB_ENV, 0)) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "ERROR -- Failed to create DB_ENV (returned: %d).\n",
                  ret, 0, 0);
    }

    DB_ENV_SET_REGION_INIT(pEnv->dblayer_DB_ENV);

    /* Here we overide various system functions called by libdb */
    ret = dblayer_override_libdb_functions(pEnv->dblayer_DB_ENV, priv);
    if (ret != 0) {
        goto fail;
    }

    if (priv->dblayer_spin_count != 0) {
        DB_ENV_SET_TAS_SPINS(pEnv->dblayer_DB_ENV, priv->dblayer_spin_count);
    }

    dblayer_dump_config_tracing(priv);

    /* set data dir to avoid having absolute paths in the transaction log */
    for (inst_obj = objset_first_obj(li->li_instance_set);
         inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (inst->inst_parent_dir_name)
        {
            if (!charray_utf8_inlist(priv->dblayer_data_directories,
                                     inst->inst_parent_dir_name))
            {
                charray_add(&(priv->dblayer_data_directories),
                            slapi_ch_strdup(inst->inst_parent_dir_name));
            }
        }
    }
    home_dir = dblayer_get_home_dir(li, NULL);
    /* user specified db home */
    if (home_dir && *home_dir &&
        !charray_utf8_inlist(priv->dblayer_data_directories, home_dir))
    {
        charray_add(&(priv->dblayer_data_directories), home_dir);
    }

    /* user specified log dir */
    if (priv->dblayer_log_directory && *(priv->dblayer_log_directory)) {
        pEnv->dblayer_DB_ENV->set_lg_dir(pEnv->dblayer_DB_ENV,
                                         priv->dblayer_log_directory);
    }

    /* set up cache sizes */
    dblayer_init_dbenv(pEnv->dblayer_DB_ENV, priv);

    pEnv->dblayer_env_lock = slapi_new_rwlock();

    if (pEnv->dblayer_env_lock) {
        *env = pEnv;
        pEnv = NULL; /* do not free below */
    } else {
        LDAPDebug(LDAP_DEBUG_ANY,
            "ERROR -- Failed to create RWLock (returned: %d).\n", 
            ret, 0, 0);
    }

fail:
    if (pEnv) {
        slapi_ch_array_free(priv->dblayer_data_directories);
        priv->dblayer_data_directories = NULL;
        if (pEnv->dblayer_DB_ENV) {
            pEnv->dblayer_DB_ENV->close(pEnv->dblayer_DB_ENV, 0);
        }
        dblayer_free_env(&pEnv); /* pEnv is now garbage */
    }
    return ret;
}

/* generate an absolute path if the given instance dir is not.  */
char *
dblayer_get_full_inst_dir(struct ldbminfo *li, ldbm_instance *inst,
                          char *buf, int buflen)
{
    char *parent_dir = NULL;
    int mylen = 0;

    if (!inst)
        return NULL;

    if (inst->inst_parent_dir_name) /* e.g., /var/lib/dirsrv/slapd-ID/db */
    {
        parent_dir = inst->inst_parent_dir_name;
        mylen = strlen(parent_dir) + 1;
    }
    else
    {
        parent_dir = dblayer_get_home_dir(li, NULL);
        if (!parent_dir || !*parent_dir) {
            buf = NULL;
            return buf;
        }
        mylen = strlen(parent_dir);
        inst->inst_parent_dir_name = slapi_ch_strdup(parent_dir);
    }


    if (inst->inst_dir_name) /* e.g., userRoot */
    {
        mylen += strlen(inst->inst_dir_name) + 2;
        if (!buf || mylen > buflen)
            buf = slapi_ch_malloc(mylen);
        sprintf(buf, "%s%c%s",
                parent_dir, get_sep(parent_dir), inst->inst_dir_name);
    }
    else if (inst->inst_name)
    {
        inst->inst_dir_name = slapi_ch_strdup(inst->inst_name);
        mylen += strlen(inst->inst_dir_name) + 2;
        if (!buf || mylen > buflen)
            buf = slapi_ch_malloc(mylen);
        sprintf(buf, "%s%c%s",
                parent_dir, get_sep(parent_dir), inst->inst_dir_name);
    }
    else
    {
        mylen += 1;
        if (!buf || mylen > buflen)
            buf = slapi_ch_malloc(mylen);
        sprintf(buf, "%s", parent_dir);
    }
    return buf;
}

/*
 *  Get the total size of all the __db files
 */
static PRUint64
dblayer_get_region_size(const char *dir)
{
    PRFileInfo info;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRUint64 region_size = 0;

    dirhandle = PR_OpenDir(dir);
    if (NULL == dirhandle){
        return region_size;
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))){
        if (NULL == direntry->name){
            continue;
        }
        if (0 == strncmp(direntry->name, DB_REGION_PREFIX, 5)){
            char filename[MAXPATHLEN];

            PR_snprintf(filename, MAXPATHLEN, "%s/%s", dir, direntry->name);
            if (PR_GetFileInfo(filename, &info) != PR_FAILURE){
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
no_diskspace(struct ldbminfo *li, int dbenv_flags)
{
    struct statvfs dbhome_buf;
    struct statvfs db_buf;
    int using_region_files = !(dbenv_flags & ( DB_PRIVATE | DB_SYSTEM_MEM));
    PRUint64 expected_siz = li->li_dbcachesize + li->li_dbcachesize/2; /* dbcache + region files */
    PRUint64 fsiz;
    char *region_dir;

    if (statvfs(li->li_directory, &db_buf) < 0){
        LDAPDebug(LDAP_DEBUG_ANY,
            "Cannot get file system info for (%s); file system corrupted?\n",
            li->li_directory, 0, 0);
        return 1;
    } else {
        /*
         *  If db_home_directory is set, and it's not the same as the db_directory,
         *  then check the disk space.
         */
        if(li->li_dblayer_private->dblayer_dbhome_directory &&
           strcmp(li->li_dblayer_private->dblayer_dbhome_directory,"") &&
           strcmp(li->li_directory, li->li_dblayer_private->dblayer_dbhome_directory))
        {
            /* Calculate the available space as long as we are not using shared memory */
            if(using_region_files){
                if(statvfs(li->li_dblayer_private->dblayer_dbhome_directory, &dbhome_buf) < 0){
                    LDAPDebug(LDAP_DEBUG_ANY,
                        "Cannot get file system info for (%s); file system corrupted?\n",
                        li->li_dblayer_private->dblayer_dbhome_directory, 0, 0);
                    return 1;
                }
                fsiz = ((PRUint64)dbhome_buf.f_bavail) * ((PRUint64)dbhome_buf.f_bsize);
                region_dir = li->li_dblayer_private->dblayer_dbhome_directory;
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
        fsiz += dblayer_get_region_size(region_dir);

        /* Check if we have enough space */
        if (fsiz < expected_siz){
            LDAPDebug(LDAP_DEBUG_ANY,
                "No enough space left on device (%s) (%lu bytes); "
                "at least %lu bytes space is needed for db region files\n",
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
#define DBCONFLEN    3
#define CATASTROPHIC (struct dblayer_private_env *)-1

int
dblayer_start(struct ldbminfo *li, int dbmode)
{
    /* 
     * So, here we open our DB_ENV session. We store it away for future use.
     * We also check to see if we exited cleanly last time. If we didn't,
     * we try to recover. If recovery fails, we're hosed.
     * We also create the thread which handles checkpointing and logfile
     * truncation here.
     */
    int return_value = -1;
    dblayer_private *priv = NULL;
    struct dblayer_private_env *pEnv = NULL;
    char *region_dir = NULL; /* directory to place region files */
    char *log_dir = NULL;    /* directory to place txn log files */
    int open_flags = 0;

    PR_ASSERT(NULL != li);

    priv = (dblayer_private*)li->li_dblayer_private;

    if (NULL == priv) {
        /* you didn't call init successfully */
        return -1;
    }

    if (NULL != priv->dblayer_env) {
        if (CATASTROPHIC == priv->dblayer_env) {
            LDAPDebug(LDAP_DEBUG_ANY,
                "Error: DB previously failed to start.\n", 0, 0, 0);
            return -1;
        } else {
            LDAPDebug(LDAP_DEBUG_ANY,
                "Warning: DB already started.\n", 0, 0, 0);
            return 0;
        }
    }

    /* DBDB we should pick these up in our config routine, and do away with
     *  the li_ one */
    if (NULL == li->li_directory || '\0' == *li->li_directory) {
        LDAPDebug(LDAP_DEBUG_ANY,
            "Error: DB directory is not specified.\n", 0, 0, 0);
        return -1;
    }
    PR_Lock(li->li_config_mutex);
    /* li->li_directory comes from nsslapd-directory */
    /* dblayer_home_directory is freed in dblayer_post_close.
     * li_directory needs to live beyond dblayer. */
    priv->dblayer_home_directory = slapi_ch_strdup(li->li_directory); 
    priv->dblayer_cachesize = li->li_dbcachesize;
    priv->dblayer_file_mode = li->li_mode;
    priv->dblayer_ncache = li->li_dbncache;
    PR_Unlock(li->li_config_mutex);

    /* use nsslapd-db-home-directory (dblayer_dbhome_directory), if set */
    /* Otherwise, nsslapd-directory (dblayer_home_directory). */
    region_dir = dblayer_get_home_dir(li, NULL);
    if (!region_dir || !(*region_dir)) {
        return -1;
    }

    /* Check here that the database directory both exists, and that we have
     * the appropriate access to it */
    return_value = dblayer_grok_directory(region_dir,
                                          DBLAYER_DIRECTORY_READWRITE_ACCESS);
    if (0 != return_value) {
        LDAPDebug(LDAP_DEBUG_ANY,"Can't start because the database "
                  "directory \"%s\" either doesn't exist, or is not "
                  "accessible\n", region_dir, 0, 0);
        return return_value;
    }

    log_dir = priv->dblayer_log_directory; /* nsslapd-db-logdirectory */
    if (log_dir && *log_dir) {
        /* checking the user defined log dir's accessability */
        return_value = dblayer_grok_directory(log_dir,
                           DBLAYER_DIRECTORY_READWRITE_ACCESS);
        if (0 != return_value) {
            LDAPDebug(LDAP_DEBUG_ANY,"Can't start because the log "
                      "directory \"%s\" either doesn't exist, or is not "
                      "accessible\n", log_dir, 0, 0);
            return return_value;
        }
    }

    /* Sanity check on cache size on platforms which allow us to figure out
     * the available phys mem */
    if (!dblayer_is_cachesize_sane(&(priv->dblayer_cachesize))) {
        /* Oops---looks like the admin misconfigured, let's warn them */
        LDAPDebug(LDAP_DEBUG_ANY,"WARNING---Likely CONFIGURATION ERROR---"
                  "dbcachesize is configured to use more than the available "
                  "physical memory, decreased to the largest available size (%lu bytes).\n",
                  priv->dblayer_cachesize, 0, 0);
        li->li_dbcachesize = priv->dblayer_cachesize;
    }

    /* fill in DB_ENV stuff from the common configuration */
    return_value = dblayer_make_env(&pEnv, li);
    if (return_value != 0)
        return return_value;

    if ((DBLAYER_NORMAL_MODE|DBLAYER_CLEAN_RECOVER_MODE) & dbmode)
    {
        /* Now, we read our metadata */
        return_value = read_metadata(li);
        if (0 != return_value) {
            /* The error message was output by read_metadata() */
            return -1;
        }
    }

    dblayer_free_env(&priv->dblayer_env);
    priv->dblayer_env = pEnv;

    open_flags = DB_CREATE | DB_INIT_MPOOL | DB_THREAD;

    if (priv->dblayer_enable_transactions) {
        open_flags |= (DB_INIT_TXN | DB_INIT_LOG | DB_INIT_LOCK);
        if (priv->dblayer_recovery_required) {
            open_flags |= DB_RECOVER;
            if (DBLAYER_RESTORE_MODE & dbmode) {
                LDAPDebug(LDAP_DEBUG_ANY, "Recovering database after restore "
                          "from archive.\n", 0, 0, 0);
            } else if (DBLAYER_CLEAN_RECOVER_MODE & dbmode) {
                LDAPDebug(LDAP_DEBUG_ANY, "Clean up db environment and start "
                          "from archive.\n", 0, 0, 0);
            } else {
                LDAPDebug(LDAP_DEBUG_ANY, "Detected Disorderly Shutdown last "
                          "time Directory Server was running, recovering "
                          "database.\n", 0, 0, 0);
            }
        }
        switch  (dbmode&DBLAYER_RESTORE_MASK) {
        case DBLAYER_RESTORE_MODE:
            open_flags |= DB_RECOVER_FATAL;
            open_flags &= ~DB_RECOVER;    /* shouldn't set both */
            if (!(dbmode & DBLAYER_NO_DBTHREADS_MODE))
                dbmode = DBLAYER_NORMAL_MODE; /* to restart helper threads */
            break;
        case DBLAYER_RESTORE_NO_RECOVERY_MODE:
            open_flags &= ~(DB_RECOVER | DB_RECOVER_FATAL);
            if (!(dbmode & DBLAYER_NO_DBTHREADS_MODE))
                dbmode = DBLAYER_NORMAL_MODE; /* to restart helper threads */
        }
    }

    if (priv->dblayer_private_mem) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "WARNING: Server is running with nsslapd-db-private-mem on; "
                  "No other process is allowed to access the database\n",
                  0, 0, 0);
        open_flags |= DB_PRIVATE;
    }

    if (priv->dblayer_named_regions) {
        open_flags |= DB_SYSTEM_MEM;
    }

    if (priv->dblayer_lockdown) {
        open_flags |= DB_LOCKDOWN;
    }
    

    /* Is the cache being re-sized ? (If we're just doing an archive or export,
     * we don't care if the cache is being re-sized) */
    if ( (priv->dblayer_previous_cachesize || priv->dblayer_previous_ncache) &&
         ((priv->dblayer_cachesize != priv->dblayer_previous_cachesize) ||
          (priv->dblayer_ncache != priv->dblayer_previous_ncache)) &&
         !(dbmode & (DBLAYER_ARCHIVE_MODE|DBLAYER_EXPORT_MODE)) ) {
         LDAPDebug(LDAP_DEBUG_ANY,
                      "I'm resizing my cache now...cache was %lu and is now %lu\n",
                      priv->dblayer_previous_cachesize, priv->dblayer_cachesize, 0);
        dblayer_reset_env(li);
        /*
         * Once pEnv->remove (via dblayer_reset_env) has been called,
         * the DB_ENV (pEnv) needs to be created again.
         */
        if ((return_value = dblayer_make_env(&pEnv, li)) != 0) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ERROR -- Failed to create DBENV (returned: %d).\n",
                      return_value, 0, 0);
        }
        dblayer_free_env(&priv->dblayer_env);
        priv->dblayer_env = pEnv;
    }

    /* transactions enabled and logbuf size greater than sleepycat's default */
    if(priv->dblayer_enable_transactions && (priv->dblayer_logbuf_size > 0)) {
        if(priv->dblayer_logbuf_size >= 32768) {
            pEnv->dblayer_DB_ENV->set_lg_bsize(pEnv->dblayer_DB_ENV,priv->dblayer_logbuf_size);
        } else {
            LDAPDebug(LDAP_DEBUG_ANY, "using default value for log bufsize because configured value (%lu) is too small.\n", 
            priv->dblayer_logbuf_size, 0, 0);
        }
    }

    /* check if there's enough disk space to start */
    if (no_diskspace(li, open_flags))
    {
        return ENOSPC;
    }

    dblayer_set_data_dir(priv, pEnv, priv->dblayer_data_directories);
    /* If we're doing recovery, we MUST open the env single-threaded ! */
    if ( (open_flags & DB_RECOVER) || (open_flags & DB_RECOVER_FATAL) ) {
        /* Recover, then close, then open again */
        int recover_flags = open_flags & ~DB_THREAD;

        if (DBLAYER_CLEAN_RECOVER_MODE & dbmode) /* upgrade case */
        {
            DB_ENV *thisenv = pEnv->dblayer_DB_ENV;
            return_value = thisenv->remove(thisenv, region_dir, DB_FORCE);
            if (0 != return_value)
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "dblayer_start: failed to remove old db env "
                          "in %s: %s\n", region_dir, 
                          dblayer_strerror(return_value), 0);
                return return_value;
            }
            dbmode = DBLAYER_NORMAL_MODE;

            if ((return_value = dblayer_make_env(&pEnv, li)) != 0)
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ERROR -- Failed to create DBENV (returned: %d).\n",
                          return_value, 0, 0);
                return return_value;
            }
        }

        return_value = (pEnv->dblayer_DB_ENV->open)(
                        pEnv->dblayer_DB_ENV,
                        region_dir,
                        recover_flags,
                        priv->dblayer_file_mode
                        );
        if (0 != return_value) {
            if (return_value == ENOMEM) {
                /* 
                 * https://blackflag.mcom.com/show_bug.cgi?id=557319
                 * Crash ns-slapd while running scalab01 after restart slapd
                 */
                LDAPDebug(LDAP_DEBUG_ANY,
                          "mmap in opening database environment (recovery mode) "
                          "failed trying to allocate %lu bytes. (OS err %d - %s)\n",
                          li->li_dbcachesize, return_value, dblayer_strerror(return_value));
                dblayer_free_env(&priv->dblayer_env);
                priv->dblayer_env = CATASTROPHIC;
            } else {
                LDAPDebug(LDAP_DEBUG_ANY, "Database Recovery Process FAILED. "
                          "The database is not recoverable. err=%d: %s\n",
                          return_value, dblayer_strerror(return_value), 0);
                LDAPDebug(LDAP_DEBUG_ANY, 
                          "Please make sure there is enough disk space for "
                          "dbcache (%lu bytes) and db region files\n",
                          li->li_dbcachesize, 0, 0);
            }
            return return_value;
        } else {
            open_flags &= ~(DB_RECOVER | DB_RECOVER_FATAL);
            pEnv->dblayer_DB_ENV->close(pEnv->dblayer_DB_ENV, 0);
            if ((return_value = dblayer_make_env(&pEnv, li)) != 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ERROR -- Failed to create DBENV (returned: %d).\n",
                          return_value, 0, 0);
                return return_value;
            }
            dblayer_free_env(&priv->dblayer_env);
            priv->dblayer_env = pEnv;
            dblayer_set_data_dir(priv, pEnv, priv->dblayer_data_directories);
        }
    }

    if ((!priv->dblayer_durable_transactions) || 
        ((priv->dblayer_enable_transactions) && (trans_batch_limit > 0))){
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3200
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4100 /* db4.1 and newer */
      pEnv->dblayer_DB_ENV->set_flags(pEnv->dblayer_DB_ENV, DB_TXN_WRITE_NOSYNC, 1);
#else /* db3.3 */
      pEnv->dblayer_DB_ENV->set_flags(pEnv->dblayer_DB_ENV, DB_TXN_NOSYNC, 1);
#endif
#else /* older */
      open_flags |= DB_TXN_NOSYNC;
#endif
    }
    /* ldbm2index uses transactions but sets the transaction flag to off - we
       need to dblayer_init_pvt_txn in that case */
    dblayer_init_pvt_txn();
    if (!((DBLAYER_IMPORT_MODE|DBLAYER_INDEX_MODE) & dbmode))
    {
        pEnv->dblayer_openflags = open_flags;
        return_value = (pEnv->dblayer_DB_ENV->open)(
            pEnv->dblayer_DB_ENV,
            region_dir,
            open_flags,
            priv->dblayer_file_mode
            );


        /* Now attempt to start up the checkpoint and deadlock threads */
        /* note: need to be '==', not '&' to omit DBLAYER_NO_DBTHREADS_MODE */
        if ( (DBLAYER_NORMAL_MODE == dbmode ) && 
             (0 == return_value)) {
            /* update the dbversion file */
            dbversion_write(li, region_dir, NULL, DBVERSION_ALL);

            /* if dblayer_close then dblayer_start is called,
               this flag is set */
            priv->dblayer_stop_threads = 0; 
            if (0 != (return_value = dblayer_start_deadlock_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = dblayer_start_checkpoint_thread(li))) {
                return return_value;
            }
            
            if (0 != (return_value = dblayer_start_log_flush_thread(priv))) {
                return return_value;
            }

            if (0 != (return_value = dblayer_start_trickle_thread(li))) {
                return return_value;
            }

            if (0 != (return_value = dblayer_start_perf_thread(li))) {
                return return_value;
            }

            /* Now open the performance counters stuff */
            perfctrs_init(li,&(priv->perf_private));
            if (getenv(TXN_TESTING)) {
                dblayer_start_txn_test_thread(li);
            }
        }
        if (return_value != 0) {
            if (return_value == ENOMEM) {
                /* 
                 * https://blackflag.mcom.com/show_bug.cgi?id=557319
                 * Crash ns-slapd while running scalab01 after restart slapd
                 */
                LDAPDebug(LDAP_DEBUG_ANY,
                      "mmap in opening database environment "
                      "failed trying to allocate %d bytes. (OS err %lu - %s)\n",
                      li->li_dbcachesize, return_value, dblayer_strerror(return_value));
                dblayer_free_env(&priv->dblayer_env);
                priv->dblayer_env = CATASTROPHIC;
            } else {
                LDAPDebug(LDAP_DEBUG_ANY,
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
 * If the size is less than the hard limit, it issues an error and quit.
 * If the size is greater than the hard limit and less than the soft limit,
 * it issues a warning, but continues the import task.
 *
 * Note: this function is called only if the import is executed as a stand
 * alone command line (ldif2db).
 */
int
check_and_set_import_cache(struct ldbminfo *li)
{
    size_t import_pages = 0;
    size_t pagesize, pages, procpages, availpages;
    size_t soft_limit = 0;
    size_t hard_limit = 0;
    size_t page_delta = 0;
    char s[64];   /* big enough to hold %ld */

    dblayer_sys_pages(&pagesize, &pages, &procpages, &availpages);
    if (0 == pagesize || 0 == pages) {
        LDAPDebug2Args(LDAP_DEBUG_ANY, "check_and_set_import_cache: "
                       "Failed to get pagesize: %ld or pages: %ld\n",
                       pagesize, pages);
        return ENOENT;
    }
    LDAPDebug(LDAP_DEBUG_ANY, "check_and_set_import_cache: "
                  "pagesize: %ld, pages: %ld, procpages: %ld\n",
                  pagesize, pages, procpages);

    /* Soft limit: pages equivalent to 1GB (defined in dblayer.h) */
    soft_limit = (DBLAYER_IMPORTCACHESIZE_SL*1024) / (pagesize/1024);
    /* Hard limit: pages equivalent to 100MB (defined in dblayer.h) */
    hard_limit = (DBLAYER_IMPORTCACHESIZE_HL*1024) / (pagesize/1024);
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
        LDAPDebug0Args(LDAP_DEBUG_ANY,
            "check_and_set_import_cache: "
            "import cache autosizing value "
            "(nsslapd-import-cache-autosize) should not be "
            "greater than or equal to 100(%). Reset to 50(%).\n");
        li->li_import_cache_autosize = 50;
    }

    if (li->li_import_cache_autosize == 0) {
        /* user specified importCache */
        import_pages = li->li_import_cachesize / pagesize;

    } else {
        /* autosizing importCache */
        /* ./125 instead of ./100 is for adjusting the BDB overhead. */
        import_pages = (li->li_import_cache_autosize * pages) / 125;
    }

    page_delta = pages - import_pages;
    if (page_delta < hard_limit) {
        LDAPDebug(LDAP_DEBUG_ANY, 
            "After allocating import cache %ldKB, "
            "the available memory is %ldKB, "
            "which is less than the hard limit %ldKB. "
            "Please decrease the import cache size and rerun import.\n",
            import_pages*(pagesize/1024), page_delta*(pagesize/1024),
            hard_limit*(pagesize/1024));
        return ENOMEM;
    }
    if (page_delta < soft_limit) {
        LDAPDebug(LDAP_DEBUG_ANY, 
            "WARNING: After allocating import cache %ldKB, "
            "the available memory is %ldKB, "
            "which is less than the soft limit %ldKB. "
            "You may want to decrease the import cache size and "
            "rerun import.\n",
            import_pages*(pagesize/1024), page_delta*(pagesize/1024),
            soft_limit*(pagesize/1024));
    }

    LDAPDebug1Arg(LDAP_DEBUG_ANY, "Import allocates %ldKB import cache.\n", 
                  import_pages*(pagesize/1024));
    if (li->li_import_cache_autosize > 0) { /* import cache autosizing */
        /* set the calculated import cache size to the config */
        sprintf(s, "%lu", (unsigned long)(import_pages * pagesize));
        ldbm_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, s);
    }
    return 0;
}

size_t
dblayer_get_id2entry_size(ldbm_instance *inst)
{
    struct ldbminfo *li = NULL;
    char *id2entry_file = NULL;
    PRFileInfo info;
    int rc;
    char inst_dir[MAXPATHLEN], *inst_dirp;

    if (NULL == inst) {
        return 0;
    }
    li = inst->inst_li;
    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    id2entry_file = slapi_ch_smprintf("%s/%s", inst_dirp,
                                      ID2ENTRY LDBM_FILENAME_SUFFIX);
    rc = PR_GetFileInfo(id2entry_file, &info);
    slapi_ch_free_string(&id2entry_file);
    if (rc) {
        return 0;
    }
    return info.size;
}

/* mode is one of
 * DBLAYER_NORMAL_MODE,
 * DBLAYER_INDEX_MODE,
 * DBLAYER_IMPORT_MODE,
 * DBLAYER_EXPORT_MODE
 */
int dblayer_instance_start(backend *be, int mode)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    dblayer_private *priv;
    struct dblayer_private_env *pEnv;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int return_value = -1;

    priv = (dblayer_private*)li->li_dblayer_private;
    pEnv = priv->dblayer_env;
    if (CATASTROPHIC == pEnv || NULL == pEnv) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "instance %s: dbenv is not available (0x%x).\n", 
                  inst?inst->inst_name:"unknown", pEnv, 0);
        return return_value;
    }

    if (NULL != inst->inst_id2entry) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Warning: DB instance \"%s\" already started.\n",
                  inst->inst_name, 0, 0);
        return 0;
    }

    if (attrcrypt_init(inst)) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Error: unable to initialize attrcrypt system for %s\n",
                  inst->inst_name, 0, 0);
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
        return_value = dblayer_grok_directory(inst_dirp,
                                          DBLAYER_DIRECTORY_READWRITE_ACCESS);
    } else {
        LDAPDebug(LDAP_DEBUG_ANY,"Can't start because the database instance "
                      "directory is NULL\n", 0, 0, 0);
        goto errout;
    }
    if (0 != return_value) {
        LDAPDebug(LDAP_DEBUG_ANY,"Can't start because the database instance "
                      "directory \"%s\" either doesn't exist, "
                      "or the db files are not accessible\n",
                      inst_dirp, 0, 0);
        goto errout;
    }

    if (mode & DBLAYER_NORMAL_MODE) {
        /* In normal mode (not db2ldif, ldif2db, etc.) we need to deal with 
         * the dbversion file here. */

        /* Read the dbversion file if there is one, and create it
         * if it doesn't exist. */
        if (dbversion_exists(li, inst_dirp)) {
            char *ldbmversion = NULL;
            char *dataversion = NULL;

            if (dbversion_read(li, inst_dirp, &ldbmversion, &dataversion) != 0) {
                LDAPDebug(LDAP_DEBUG_ANY, "Warning: Unable to read dbversion "
                          "file in %s\n", inst->inst_dir_name, 0, 0);
            } else {
                int rval = 0;
                /* check the DBVERSION and reset idl-switch if needed (DS6.2) */
                /* from the next major rel, we won't do this and just upgrade */
                if (!(li->li_flags & LI_FORCE_MOD_CONFIG)) {
                    adjust_idl_switch(ldbmversion, li);
                }
                slapi_ch_free_string(&ldbmversion);

                /* check to make sure these instance was made with the correct
                 * version. */
                rval = check_db_inst_version(inst);
                if (rval & DBVERSION_NOT_SUPPORTED)
                {
                    LDAPDebug(LDAP_DEBUG_ANY, "Instance %s does not have the "
                              "expected version\n", inst->inst_name, 0, 0);
                    PR_ASSERT(0);
                    return_value = -1;
                    goto errout;
                }
                else if (rval & DBVERSION_NEED_DN2RDN)
                {
                    LDAPDebug2Args(LDAP_DEBUG_ANY, 
                        "%s is on, while the instance %s is in the DN format. "
                        "Please run dn2rdn to convert the database format.\n",
                        CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
                    return_value = -1;
                    goto errout;
                }
                else if (rval & DBVERSION_NEED_RDN2DN)
                {
                    LDAPDebug2Args(LDAP_DEBUG_ANY, 
                        "%s is off, while the instance %s is in the RDN "
                        "format. Please change the value to on in dse.ldif.\n",
                        CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
                    return_value = -1;
                    goto errout;
                }

                /* record the dataversion */
                if (dataversion != NULL && *dataversion != '\0') {
                    inst->inst_dataversion = dataversion;
                } else {
                    slapi_ch_free_string(&dataversion);
                }

                rval = ldbm_upgrade(inst, rval);
                if (0 != rval)
                {
                    LDAPDebug(LDAP_DEBUG_ANY, "Upgrading instance %s failed\n",
                              inst->inst_name, 0, 0);
                    PR_ASSERT(0);
                    return_value = -1;
                    goto errout;
                }
                /**
                if (rval & DBVERSION_NEED_IDL_OLD2NEW)
                {
                    LDAPDebug(LDAP_DEBUG_ANY, 
                        "Instance %s: idl-switch is new while db idl format is "
                        "old, modify nsslapd-idl-switch in dse.ldif to old\n",
                        inst->inst_name, 0, 0);
                    return_value = -1;
                    goto errout;
                }
                else if (rval & DBVERSION_NEED_IDL_NEW2OLD)
                {
                    LDAPDebug(LDAP_DEBUG_ANY, 
                        "Instance %s: idl-switch is old while db idl format is "
                        "new, modify nsslapd-idl-switch in dse.ldif to new\n",
                        inst->inst_name, 0, 0);
                    return_value = -1;
                    goto errout;
                }
                **/
            }
        } else {
            /* The dbversion file didn't exist, so we'll create one. */
            dbversion_write(li, inst_dirp, NULL, DBVERSION_ALL);
        }
    } /* on import we don't mess with the dbversion file except to write it
       * when done with the import. */

    /* Now attempt to open id2entry */
    {
        char *id2entry_file;
        int open_flags = 0;
        DB *dbp;
        char *subname;
        struct dblayer_private_env *mypEnv;

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
        if (mode & (DBLAYER_IMPORT_MODE|DBLAYER_INDEX_MODE)) {
            size_t cachesize;
            char *data_directories[2] = {0, 0};
            /* [605974] delete DB_PRIVATE: 
             * to make import visible to the other process */
            int oflags = DB_CREATE | DB_INIT_MPOOL | DB_THREAD;
            /*
             * but nsslapd-db-private-import-mem should work with import,
             * as well */
            if (priv->dblayer_private_import_mem) {
                LDAPDebug(LDAP_DEBUG_ANY,
                  "WARNING: Import is running with "
                  "nsslapd-db-private-import-mem on; "
                  "No other process is allowed to access the database\n",
                  0, 0, 0);
                oflags |= DB_PRIVATE;
            }
            PR_Lock(li->li_config_mutex);
            /* import cache checking and autosizing is available only
             * for the command line */
            if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
                return_value = check_and_set_import_cache(li);
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
            priv->dblayer_cachesize = cachesize;
            /* We always auto-calculate ncache for the import region */
            priv->dblayer_ncache = 0;

            /* use our own env */
            return_value = dblayer_make_env(&mypEnv, li);
            if (return_value != 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "Unable to create new DB_ENV for import/export! %d\n",
                          return_value, 0, 0);
                goto out;
            }
            /* do not assume import cache size is under 1G */
            mypEnv->dblayer_DB_ENV->set_cachesize(mypEnv->dblayer_DB_ENV,
                                                  cachesize/GIGABYTE,
                                                  cachesize%GIGABYTE,
                                                  priv->dblayer_ncache);
            /* probably want to change this -- but for now, create the 
             * mpool files in the instance directory.
             */
            mypEnv->dblayer_openflags = oflags;
            data_directories[0] = inst->inst_parent_dir_name;
            dblayer_set_data_dir(priv, mypEnv, data_directories);
            return_value = (mypEnv->dblayer_DB_ENV->open)(mypEnv->dblayer_DB_ENV,
                                                       inst_dirp,
                                                       oflags,
                                                       priv->dblayer_file_mode);
            if (return_value != 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "Unable to open new DB_ENV for import/export! %d\n",
                          return_value, 0, 0);
                goto out;
            }
            inst->import_env = mypEnv;
        } else {
            mypEnv = pEnv;
        }

        inst->inst_id2entry = NULL;
        return_value = db_create(&inst->inst_id2entry, mypEnv->dblayer_DB_ENV, 0);
        if (0 != return_value) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "Unable to create id2entry db file! %d\n",
                      return_value, 0, 0);
            goto out;
        }
        dbp = inst->inst_id2entry;

        return_value = dbp->set_pagesize(dbp,
            (priv->dblayer_page_size == 0) ? DBLAYER_PAGESIZE :
            priv->dblayer_page_size);
        if (0 != return_value) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "dbp->set_pagesize(%lu or %lu) failed %d\n",
                      priv->dblayer_page_size, DBLAYER_PAGESIZE,
                      return_value);
            goto out;
        }

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 3300
        return_value = dbp->set_malloc(dbp, (void *)slapi_ch_malloc);
        if (0 != return_value) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY, "dbp->set_malloc failed %d\n",
                          return_value);

            goto out;
        }
#endif
        if ((charray_get_index(priv->dblayer_data_directories,
                               inst->inst_parent_dir_name) != 0) &&
            !dblayer_inst_exists(inst, NULL))
        {
            char *abs_id2entry_file = NULL;
            /* create a file with abs path, then try again */

            abs_id2entry_file = slapi_ch_smprintf( "%s%c%s", inst_dirp, 
                    get_sep(inst_dirp), ID2ENTRY LDBM_FILENAME_SUFFIX);
            DB_OPEN(mypEnv->dblayer_openflags,
                dbp, NULL/* txnid */, abs_id2entry_file, subname, DB_BTREE,
                open_flags, priv->dblayer_file_mode, return_value);
            dbp->close(dbp, 0);
            return_value = db_create(&inst->inst_id2entry,
                                     mypEnv->dblayer_DB_ENV, 0);
            if (0 != return_value)
                goto out;
            dbp = inst->inst_id2entry;
            return_value = dbp->set_pagesize(dbp,
                (priv->dblayer_page_size == 0) ? DBLAYER_PAGESIZE :
                priv->dblayer_page_size);
            if (0 != return_value) {
                LDAPDebug(LDAP_DEBUG_ANY,
                      "dbp->set_pagesize(%lu or %lu) failed %d\n",
                      priv->dblayer_page_size, DBLAYER_PAGESIZE,
                      return_value);
                goto out;
            }

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 3300
            return_value = dbp->set_malloc(dbp, (void *)slapi_ch_malloc);
            if (0 != return_value) {
                LDAPDebug1Arg(LDAP_DEBUG_ANY, "dbp->set_malloc failed %d\n",
                              return_value);

                goto out;
            }
#endif
            slapi_ch_free_string(&abs_id2entry_file);
        }
        DB_OPEN(mypEnv->dblayer_openflags,
                dbp, NULL/* txnid */, id2entry_file, subname, DB_BTREE,
                open_flags, priv->dblayer_file_mode, return_value);
        if (0 != return_value) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "dbp->open(\"%s\") failed: %s (%d)\n",
                      id2entry_file, dblayer_strerror(return_value),
                      return_value);
            /* if it's a newly created backend instance,
             * need to check the inst_parent_dir already exists and
             * set as a data dir */
            if (strstr(dblayer_strerror(return_value),
                       "No such file or directory"))
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                      "Instance %s is not registered as a db data directory. "
                      "Please restart the server to create it.\n",
                      inst?inst->inst_name:"unknown", pEnv, 0);
            }
            else if (strstr(dblayer_strerror(return_value),
                            "Permission denied"))
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "Instance directory %s may not be writable\n",
                          inst_dirp, 0, 0);
            }

            goto out;
        }
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR == 4100
        /* lower the buffer cache priority to avoid sleep in memp_alloc */
        /* W/ DB_PRIORITY_LOW, the db buffer page priority is calculated as:
         *     priority = lru_count + pages / (-1)
         * (by default, priority = lru_count)
         * When upgraded to db4.2, this setting may not needed, hopefully.
         * ask sleepycat [#8301]; blackflag #619964
         */
        dbp->set_cache_priority(dbp, DB_PRIORITY_LOW);
#endif
out:
        slapi_ch_free_string(&id2entry_file);
    }

    if (0 == return_value) {
        /* get nextid from disk now */
        get_ids_from_disk(be);
    }

    if (mode & DBLAYER_NORMAL_MODE) {
        dbversion_write(li, inst_dirp, NULL, DBVERSION_ALL);
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
        LDAPDebug(LDAP_DEBUG_ANY, "dblayer_instance_start fail: backend '%s' "
            "has no IDs left. DATABASE MUST BE REBUILT.\n",
            be->be_name, 0, 0);
        return 1;
    }

    if (return_value != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "dblayer_instance_start fail: %s (%d)\n",
            dblayer_strerror(return_value), return_value, 0);   
    }
errout:
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return return_value;
}


/* This returns a DB* for the primary index.
 * If the database library is non-reentrant, we lock it.
 * the caller MUST call to unlock the db library once they're
 * finished with the handle. Luckily, the back-end already has
 * these semantics for the older dbcache stuff.
 */
/* Things have changed since the above comment was
 * written.  The database library is reentrant. */
int dblayer_get_id2entry(backend *be, DB **ppDB)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);
    
    inst = (ldbm_instance *) be->be_instance_info;

    *ppDB = inst->inst_id2entry;
    return 0;
}

int dblayer_release_id2entry(backend *be, DB *pDB)
{
    return 0;
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
dblayer_get_aux_id2entry(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path)
{
    return dblayer_get_aux_id2entry_ext(be, ppDB, ppEnv, path, 0);
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
dblayer_get_aux_id2entry_ext(backend *be, DB **ppDB, DB_ENV **ppEnv, 
                             char **path, int flags)
{
    ldbm_instance *inst;
    struct dblayer_private_env *mypEnv = NULL;
    DB *dbp = NULL;
    int rval = 1;
    struct ldbminfo *li = NULL;
    dblayer_private *opriv = NULL;
    dblayer_private *priv = NULL;
    char *subname = NULL;
    int envflags = 0;
    int dbflags = 0;
    size_t cachesize;
    PRFileInfo prfinfo;
    PRStatus prst;
    char *id2entry_file = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    char *data_directories[2] = {0, 0};

    PR_ASSERT(NULL != be);
    
    if ((NULL == ppEnv) || (NULL == ppDB)) {
        LDAPDebug0Args(LDAP_DEBUG_ANY, "No memory for DB_ENV or DB handle\n");
        goto done;
    }
    *ppDB = NULL;
    inst = (ldbm_instance *) be->be_instance_info;
    if (NULL == inst)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "No instance/env: persistent id2entry is not available\n", 0, 0, 0);
        goto done;
    }

    li = inst->inst_li;
    if (NULL == li)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "No ldbm info: persistent id2entry is not available\n", 0, 0, 0);
        goto done;
    }

    opriv = li->li_dblayer_private;
    if (NULL == opriv)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "No dblayer info: persistent id2entry is not available\n", 0, 0, 0);
        goto done;
    }
    priv = (dblayer_private *)slapi_ch_malloc(sizeof(dblayer_private));
    memcpy(priv, opriv, sizeof(dblayer_private));
    priv->dblayer_spin_count = 0;

    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp)
    {
        priv->dblayer_home_directory = slapi_ch_smprintf("%s/dbenv", inst_dirp);
    }
    else
    {
        LDAPDebug(LDAP_DEBUG_ANY,
          "Instance dir is NULL: persistent id2entry is not available\n",
          0, 0, 0);
        goto done;
    }
    priv->dblayer_log_directory = slapi_ch_strdup(priv->dblayer_home_directory);

    prst = PR_GetFileInfo(inst_dirp, &prfinfo);
    if (PR_FAILURE == prst || PR_FILE_DIRECTORY != prfinfo.type)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "No inst dir: persistent id2entry is not available\n", 0, 0, 0);
        goto done;
    }

    prst = PR_GetFileInfo(priv->dblayer_home_directory, &prfinfo);
    if (PR_SUCCESS == prst)
    {
        ldbm_delete_dirs(priv->dblayer_home_directory);
    }
    rval = mkdir_p(priv->dblayer_home_directory, 0700);
    if (rval)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
               "can't create env dir: persistent id2entry is not available\n");
        goto done;
    }

    /* use our own env if not passed */
    if (!*ppEnv) {
        rval = dblayer_make_env(&mypEnv, li);
        if (rval) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                  "Unable to create new DB_ENV for import/export! %d\n", rval);
            goto err;
        }
    }

    envflags = DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE;
    cachesize = 10485760; /* 10M */

    if (!*ppEnv) {
        mypEnv->dblayer_DB_ENV->set_cachesize(mypEnv->dblayer_DB_ENV,
                0, cachesize, priv->dblayer_ncache);

        /* probably want to change this -- but for now, create the 
         * mpool files in the instance directory.
         */
        mypEnv->dblayer_openflags = envflags;
        data_directories[0] = inst->inst_parent_dir_name;
        dblayer_set_data_dir(priv, mypEnv, data_directories);
        rval = (mypEnv->dblayer_DB_ENV->open)(mypEnv->dblayer_DB_ENV,
            priv->dblayer_home_directory, envflags, priv->dblayer_file_mode);
        if (rval) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                  "Unable to open new DB_ENV for upgradedb/reindex %d\n", rval);
            goto err;
        }
        *ppEnv = mypEnv->dblayer_DB_ENV;
    }
    rval = db_create(&dbp, *ppEnv, 0);
    if (rval) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "Unable to create id2entry db handler! %d\n", rval);
        goto err;
    }

    rval = dbp->set_pagesize(dbp, (priv->dblayer_page_size == 0) ?
                        DBLAYER_PAGESIZE : priv->dblayer_page_size);
    if (rval) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "dbp->set_pagesize(%lu or %lu) failed %d\n",
                  priv->dblayer_page_size, DBLAYER_PAGESIZE, rval);
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

    PR_ASSERT(dblayer_inst_exists(inst, NULL));
    DB_OPEN(envflags, dbp, NULL/* txnid */, id2entry_file, subname, DB_BTREE,
            dbflags, priv->dblayer_file_mode, rval);
    if (rval) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "dbp->open(\"%s\") failed: %s (%d)\n",
                  id2entry_file, dblayer_strerror(rval), rval);
        if (strstr(dblayer_strerror(rval), "Permission denied")) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                  "Instance directory %s may not be writable\n", inst_dirp);
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
    if (priv->dblayer_home_directory) {
        ldbm_delete_dirs(priv->dblayer_home_directory);
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
        slapi_ch_free_string(&priv->dblayer_home_directory);
        slapi_ch_free_string(&priv->dblayer_log_directory);
    }
    /* Don't free priv->dblayer_data_directories since priv doesn't own the memory */
    slapi_ch_free((void **)&priv);
    slapi_ch_free((void **)&mypEnv);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

int dblayer_release_aux_id2entry(backend *be, DB *pDB, DB_ENV *pEnv)
{
    ldbm_instance *inst;
    char *envdir = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;

    inst = (ldbm_instance *) be->be_instance_info;
    if (NULL == inst)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "No instance/env: persistent id2entry is not available\n", 0, 0, 0);
        goto done;
    }

    inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                          inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp)
    {
        envdir = slapi_ch_smprintf("%s/dbenv", inst_dirp);
    }

done:
    if (pDB)
       pDB->close(pDB, 0);
    if (pEnv)
       pEnv->close(pEnv, 0);
    if (envdir) {
        ldbm_delete_dirs(envdir);
        slapi_ch_free_string(&envdir);
    }
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return 0;
}

int dblayer_close_indexes(backend *be)
{
    ldbm_instance *inst;
    DB *pDB = NULL;
    dblayer_handle *handle = NULL;
    dblayer_handle *next = NULL;
    int return_value = 0;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *) be->be_instance_info;
    PR_ASSERT(NULL != inst);

    for (handle = inst->inst_handle_head; handle != NULL; handle = next) {
        /* Close it, and remove from the list */
        pDB = handle->dblayer_dbp;
        return_value |= pDB->close(pDB,0);
        next = handle->dblayer_handle_next;
        *(handle->dblayer_handle_ai_backpointer) = NULL;
        slapi_ch_free((void**)&handle);
    }

    /* reset the list to make sure we don't use it again */
    inst->inst_handle_tail = NULL;
    inst->inst_handle_head = NULL;

    return return_value;
}

int dblayer_instance_close(backend *be)
{
    DB *pDB = NULL;
    int return_value = 0;
    DB_ENV * env = 0;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (NULL == inst)
        return -1;

    if (!inst->import_env) {
        be->be_state = BE_STATE_STOPPING;
    }
    if (getenv("USE_VALGRIND") || slapi_is_loglevel_set(SLAPI_LOG_CACHE)) {
        /* 
         * if any string is set to an environment variable USE_VALGRIND,
         * when running a memory leak checking tool (e.g., valgrind),
         * it reduces the noise by enabling this code.
         */
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "%s: Cleaning up entry cache\n",
                                      inst->inst_name);
        cache_clear(&inst->inst_cache, CACHE_TYPE_ENTRY);
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "%s: Cleaning up dn cache\n",
                                      inst->inst_name);
        cache_clear(&inst->inst_dncache, CACHE_TYPE_DN);
    }

    if (attrcrypt_cleanup_private(inst)) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Error: failed to clean up attrcrypt system for %s\n",
                  inst->inst_name, 0, 0);
    }

    return_value = dblayer_close_indexes(be);

    /* Now close id2entry if it's open */
    pDB = inst->inst_id2entry;
    if (NULL != pDB) {
        return_value |= pDB->close(pDB,0);
    }
    inst->inst_id2entry = NULL;

    if (inst->import_env) {
       /* ignore the value of env, close, because at this point, 
        * work is done with import env by calling env.close, 
        * env and all the associated db handles will be closed, ignore,
        * if sleepycat complains, that db handles are open at env close time */
       return_value |= inst->import_env->dblayer_DB_ENV->close(inst->import_env->dblayer_DB_ENV, 0);
       return_value = db_env_create(&env, 0);
       if (return_value == 0) {
         char inst_dir[MAXPATHLEN];
         char *inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                     inst_dir, MAXPATHLEN);
         if (inst_dirp && *inst_dir) {
           return_value = env->remove(env, inst_dirp, 0);
         }
         else
         {
           return_value = -1;
         }
         if (return_value == EBUSY) {
           return_value = 0; /* something else is using the env so ignore */
         }
         if (inst_dirp != inst_dir)
           slapi_ch_free_string(&inst_dirp);
       }
       slapi_destroy_rwlock(inst->import_env->dblayer_env_lock);
       slapi_ch_free((void **)&inst->import_env);
    } else {
       be->be_state = BE_STATE_STOPPED;
    }

    return return_value;
}

void dblayer_pre_close(struct ldbminfo *li)
{
    dblayer_private *priv = 0;
    PRInt32 threadcount = 0;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private*)li->li_dblayer_private;

    if (priv->dblayer_stop_threads)    /* already stopped.  do nothing... */
        return;

    /* first, see if there are any housekeeping threads running */
    PR_Lock(priv->thread_count_lock);
    threadcount = priv->dblayer_thread_count;
    PR_Unlock(priv->thread_count_lock);

    if (threadcount) {
        PRIntervalTime cvwaittime = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL * 100);
        int timedout = 0;
        /* Print handy-dandy log message */
        LDAPDebug(LDAP_DEBUG_ANY,"Waiting for %d database threads to stop\n",
                  threadcount, 0,0);
        PR_Lock(priv->thread_count_lock);
        /* Tell them to stop - we wait until the last possible moment to invoke
           this.  If we do this much sooner than this, we could find ourselves
           in a situation where the threads see the stop_threads and exit before
           we can issue the WaitCondVar below, which means the last thread to
           exit will do a NotifyCondVar that has nothing waiting.  If we do this
           inside the lock, we will ensure that the threads will block until we
           issue the WaitCondVar below */
        priv->dblayer_stop_threads = 1;
        /* Wait for them to exit */
        while (priv->dblayer_thread_count > 0) {
            PRIntervalTime before = PR_IntervalNow();
            /* There are 3 ways to wake up from this WaitCondVar:
               1) The last database thread exits and calls NotifyCondVar - thread_count
               should be 0 in this case
               2) Timeout - in this case, thread_count will be > 0 - bad
               3) A bad error occurs - bad - will be reported as a timeout
            */
            PR_WaitCondVar(priv->thread_count_cv, cvwaittime);
            if (priv->dblayer_thread_count > 0) {
                /* still at least 1 thread running - see if this is a timeout */
                if ((PR_IntervalNow() - before) >= cvwaittime) {
                    threadcount = priv->dblayer_thread_count;
                    timedout = 1;
                    break;
                }
                /* else just a spurious interrupt */
            }
        }
        PR_Unlock(priv->thread_count_lock); 
        if (timedout) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "Timeout after [%d] milliseconds; leave %d database thread(s)...\n",
                      (DBLAYER_SLEEP_INTERVAL * 100), threadcount,0);
            priv->dblayer_bad_stuff_happened = 1;
            goto timeout_escape;
        }
    }
    LDAPDebug(LDAP_DEBUG_ANY,"All database threads now stopped\n",0,0,0);
timeout_escape:
    return;
}

int dblayer_post_close(struct ldbminfo *li, int dbmode)
{
    dblayer_private *priv = 0;
    int return_value = 0;
    dblayer_private_env *pEnv;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private*)li->li_dblayer_private;

    /* We close all the files we ever opened, and call pEnv->close. */
    if (NULL == priv->dblayer_env) /* db env is already closed. do nothing. */
        return return_value;
    /* Shutdown the performance counter stuff */
    if (DBLAYER_NORMAL_MODE & dbmode) {
        if (priv->perf_private) {
            perfctrs_terminate(&priv->perf_private, priv->dblayer_env->dblayer_DB_ENV);
        }
    }

    /* Now release the db environment */
    pEnv = priv->dblayer_env;
    return_value = pEnv->dblayer_DB_ENV->close(pEnv->dblayer_DB_ENV, 0);
    dblayer_free_env(&priv->dblayer_env); /* pEnv is now garbage */

    if (0 == return_value
        && !((DBLAYER_ARCHIVE_MODE|DBLAYER_EXPORT_MODE) & dbmode)
        && !priv->dblayer_bad_stuff_happened) {
        commit_good_database(priv);
    }
    if (priv->dblayer_data_directories) {
        /* dblayer_data_directories are set in dblayer_make_env via
         * dblayer_start, which is paired with dblayer_close. */
        /* no need to release dblayer_home_directory,
         * which is one of dblayer_data_directories */
        charray_free(priv->dblayer_data_directories);
        priv->dblayer_data_directories = NULL;
    }
    return return_value;
}

/* 
 * This function is called when the server is shutting down.
 * This is not safe to call while other threads are calling into the open
 * databases !!!   So: DON'T !
 */
int dblayer_close(struct ldbminfo *li, int dbmode)
{
    backend *be = NULL;
    ldbm_instance *inst;
    Object *inst_obj;
    int return_value = 0;

    dblayer_pre_close(li);

    /* 
     * dblayer_close_indexes and pDB->close used to be located above loop:
     *   while(priv->dblayer_thread_count > 0) in pre_close.
     * This order fixes a bug: shutdown under the stress makes txn_checkpoint
     * (checkpoint_thread) fail b/c the mpool might have been already closed.
     */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        be = inst->inst_be;
        if (NULL != be->be_instance_info) {
            return_value |= dblayer_instance_close(be);
        }
    }

    if (return_value != 0) {
        /* force recovery next startup if any close failed */
        dblayer_private *priv;
        PR_ASSERT(NULL != li);
        priv = (dblayer_private*)li->li_dblayer_private;
        PR_ASSERT(NULL != priv);
        priv->dblayer_bad_stuff_happened = 1;
    }

    return_value |= dblayer_post_close(li, dbmode);

    return return_value;
}

/* 
 * Called to tell us to flush any data not on disk to the disk
 * for the transacted database, we interpret this as an instruction
 * to write a checkpoint.
 */
int
dblayer_flush(struct ldbminfo *li)
{
    return 0;
}

/* API to remove the environment */
int
dblayer_remove_env(struct ldbminfo *li)
{
    DB_ENV *env = NULL;
    char *home_dir = NULL;
    int rc = db_env_create(&env, 0);
    if (rc) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "ERROR -- Failed to create DB_ENV (returned: %d)\n", rc);
        return rc;
    }
    if (NULL == li) {
        LDAPDebug0Args(LDAP_DEBUG_ANY, "ERROR -- No ldbm info is given\n");
        return -1;
    }

    home_dir = dblayer_get_home_dir(li, NULL);
    if (home_dir) {
        rc = env->remove(env, home_dir, 0);
        if (rc) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "ERROR -- Failed to remove DB environment files. "
                          "Please remove %s/__db.00# (# is 1 through 6)\n",
                          home_dir);
        }
    }
    return rc;
}

#if !defined(DB_DUPSORT)
#define DB_DUPSORT 0
#endif

static int
_dblayer_set_db_callbacks(dblayer_private *priv, DB *dbp, struct attrinfo *ai)
{
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
                        (priv->dblayer_index_page_size == 0) ?
                        DBLAYER_INDEX_PAGESIZE : priv->dblayer_index_page_size);
    } else {
        rc = dbp->set_pagesize(
                        dbp,
                        (priv->dblayer_page_size == 0) ?
                        DBLAYER_PAGESIZE : priv->dblayer_page_size);
    }
    if (rc)
        return rc;

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 3300
    rc = dbp->set_malloc(dbp, (void *)slapi_ch_malloc);
    if (rc)
        return rc;
#endif

    if (idl_get_idl_new() && !(ai->ai_indexmask & INDEX_VLV)) {
        rc = dbp->set_flags(dbp, DB_DUP | DB_DUPSORT);
        if (rc)
            return rc;

        if (ai->ai_dup_cmp_fn) {
            /* If set, use the special dup compare callback */
            rc = dbp->set_dup_compare(dbp, ai->ai_dup_cmp_fn);
        } else {
            rc = dbp->set_dup_compare(dbp, idl_new_compare_dups);
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
        dbp->set_bt_compare(dbp, dblayer_bt_compare);
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
dblayer_open_file(backend *be, char* indexname, int open_flag, 
                  struct attrinfo *ai, DB **ppDB)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    int open_flags = 0;
    char *file_name = NULL;
    char *rel_path = NULL;
    dblayer_private_env *pENV = 0;
    dblayer_private *priv = NULL;
    int return_value = 0;
    DB *dbp = NULL;
    char *subname = NULL;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    if (NULL == inst->inst_dir_name)
    {
        if (dblayer_get_instance_data_dir(be) != 0)
            return -1;
    }

    if (NULL != inst->inst_parent_dir_name)
    {
        if (!charray_utf8_inlist(priv->dblayer_data_directories,
                                 inst->inst_parent_dir_name) &&
            !is_fullpath(inst->inst_dir_name))

        {
            LDAPDebug(LDAP_DEBUG_ANY, 
                "The instance path %s is not registered for db_data_dir, "
                "although %s is a relative path.\n",
                inst->inst_parent_dir_name, inst->inst_dir_name, 0);
            return -1;
        }
    }

    pENV = priv->dblayer_env;
    if (inst->import_env)
        pENV = inst->import_env;

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
    return_value = db_create(ppDB, pENV->dblayer_DB_ENV, 0);
    if (0 != return_value)
        goto out;

    dbp = *ppDB;
    return_value = _dblayer_set_db_callbacks(priv, dbp, ai);
    if (return_value)
        goto out;

    /* The subname argument allows applications to have
     * subdatabases, i.e., multiple databases inside of a single
     * physical file. This is useful when the logical databases
     * are both numerous and reasonably small, in order to
     * avoid creating a large number of underlying files.
     */
    /* If inst_parent_dir_name is not the primary DB dir &&
     * the index file does not exist */
    if ((charray_get_index(priv->dblayer_data_directories,
                           inst->inst_parent_dir_name) > 0) &&
        !dblayer_inst_exists(inst, file_name))
    {
        char inst_dir[MAXPATHLEN];
        char *inst_dirp = NULL;
        char *abs_file_name = NULL;
        /* create a file with abs path, then try again */

        inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
        if (!inst_dirp || !*inst_dirp)
        {
            return_value = -1;
            goto out;
        }
        abs_file_name = slapi_ch_smprintf("%s%c%s",
                inst_dirp, get_sep(inst_dirp), file_name);
        DB_OPEN(pENV->dblayer_openflags,
                dbp, NULL/* txnid */, abs_file_name, subname, DB_BTREE,
                open_flags, priv->dblayer_file_mode, return_value);
        dbp->close(dbp, 0);
        return_value = db_create(ppDB, pENV->dblayer_DB_ENV, 0);
        if (0 != return_value)
        {
            goto out;
        }
        dbp = *ppDB;
        return_value = _dblayer_set_db_callbacks(priv, dbp, ai);
        if (return_value)
            goto out;

        slapi_ch_free_string(&abs_file_name);
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
    }
    DB_OPEN(pENV->dblayer_openflags,
            dbp, NULL, /* txnid */ rel_path, subname, DB_BTREE,
            open_flags, priv->dblayer_file_mode, return_value);
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR == 4100
    /* lower the buffer cache priority to avoid sleep in memp_alloc */
    /* W/ DB_PRIORITY_LOW, the db buffer page priority is calculated as:
     *     priority = lru_count + pages / (-1)
     * (by default, priority = lru_count)
     * When upgraded to db4.2, this setting may not needed, hopefully.
     * ask sleepycat [#8301]; blackflag #619964
     */
    dbp->set_cache_priority(dbp, DB_PRIORITY_LOW);
#endif
out:
    slapi_ch_free((void**)&file_name);
    slapi_ch_free((void**)&rel_path);
    /* close the database handle to avoid handle leak */
    if (dbp && (return_value != 0)) {
        dblayer_close_file(&dbp);
    }
    return return_value;
}

int 
dblayer_close_file(DB **db)
{
    if (db) {
        DB *dbp = *db;
        *db = NULL; /* To avoid to leave stale DB, set NULL before closing. */
        return dbp->close(dbp, 0);
    }
    return 1;
}

/*
 * OK, this is a tricky one. We store open DB* handles within an AVL
 * structure used in other parts of the back-end. This is nasty, because
 * that code has no idea we're doing this, and we don't have much control
 * over what it does. But, the reason is that we want to get fast lookup
 * of the index file pertaining to each particular attribute. Putting the
 * DB* handles in the attribute info structures is an easy way to achieve this
 * because we already lookup this structure as part of an index lookup.
 */

/* 
 * This function takes an attrinfo structure and returns a valid
 * DB* handle for the index file corresponding to this attribute.
 */

/*
 * If the db library is non-reentrant, we lock the mutex here,
 * see comments above for id2entry for the details on this.
 */

/*
 * The create flag determines if the index file should be created if it 
 * does not already exist.
 */

int dblayer_get_index_file(backend *be, struct attrinfo *a, DB** ppDB, int open_flags)
{
  /* 
   * We either already have a DB* handle in the attrinfo structure.
   * in which case we simply return it to the caller, OR:
   * we need to make one. We do this as follows: 
   * 1a) acquire the mutex that protects the handle list.
   * 1b) check that the DB* is still null.
   * 2) get the filename, and call libdb to open it
   * 3) if successful, store the result in the attrinfo stucture
   * 4) store the DB* in our own list so we can close it later.
   * 5) release the mutex.
   */
  ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
  int return_value = -1;
  DB *pDB = NULL;
  char *attribute_name = a->ai_type;
  
  *ppDB = NULL;
  
  /* it's like a semaphore -- when count > 0, any file handle that's in
   * the attrinfo will remain valid from here on.
   */
  PR_AtomicIncrement(&a->ai_dblayer_count);
  
  if (a->ai_dblayer && ((dblayer_handle*)(a->ai_dblayer))->dblayer_dbp) {
    /* This means that the pointer is valid, so we should return it. */
    *ppDB = ((dblayer_handle*)(a->ai_dblayer))->dblayer_dbp;
    return 0;
  }
  
  /* attrinfo handle is NULL, at least for now -- grab the mutex and try
   * again.
   */
  PR_Lock(inst->inst_handle_list_mutex);
  if (a->ai_dblayer && ((dblayer_handle*)(a->ai_dblayer))->dblayer_dbp) {
    /* another thread set the handle while we were waiting on the lock */
    *ppDB = ((dblayer_handle*)(a->ai_dblayer))->dblayer_dbp;
    PR_Unlock(inst->inst_handle_list_mutex);
    return 0;
  }
  
  /* attrinfo handle is still blank, and we have the mutex: open the
   * index file and stuff it in the attrinfo.
   */
  return_value = dblayer_open_file(be, attribute_name, open_flags,
                                   a, &pDB);
  if (0 == return_value) {
      /* Opened it OK */
      dblayer_handle *handle = (dblayer_handle *) 
        slapi_ch_calloc(1, sizeof(dblayer_handle));
      
      if (NULL == handle) {
        /* Memory allocation failed */
        return_value = -1;
      } else {
        dblayer_handle *prev_handle = inst->inst_handle_tail;
        
        PR_ASSERT(NULL != pDB);
        /* Store the returned DB* in our own private list of
         * open files */
        if (NULL == prev_handle) {
          /* List was empty */
          inst->inst_handle_tail = handle;
          inst->inst_handle_head = handle;
        } else {
          /* Chain the handle onto the last structure in the
           * list */
          inst->inst_handle_tail = handle;
          prev_handle->dblayer_handle_next = handle;
        }
        /* Stash a pointer to our wrapper structure in the
         * attrinfo structure */
        handle->dblayer_dbp = pDB;
        /* And, most importantly, return something to the caller!*/
        *ppDB = pDB;
        /* and save the hande in the attrinfo structure for
         * next time */
        a->ai_dblayer = handle;
        /* don't need to update count -- we incr'd it already */
        handle->dblayer_handle_ai_backpointer = &(a->ai_dblayer);
      }
  } else {
    /* Did not open it OK ! */
    /* Do nothing, because return value and fact that we didn't
     * store a DB* in the attrinfo is enough 
     */
  }
  PR_Unlock(inst->inst_handle_list_mutex);
  
  if (return_value != 0) {
    /* some sort of error -- we didn't open a handle at all.
     * decrement the refcount back to where it was.
     */
    PR_AtomicDecrement(&a->ai_dblayer_count);
  }
  
  return return_value;
}

/*
 * Unlock the db lib mutex here if we need to.
 */
int dblayer_release_index_file(backend *be,struct attrinfo *a, DB* pDB)
{
    PR_AtomicDecrement(&a->ai_dblayer_count);
    return 0;
}

/*
  dblayer_db_remove assumptions:
  
  No environment has the given database open.
  
*/

static int
dblayer_db_remove_ex(dblayer_private_env *env, char const path[], char const dbName[], PRBool use_lock) {
  DB_ENV * db_env = 0;
  int rc;
  DB *db;
  
  if (env) {
    if(use_lock) slapi_rwlock_wrlock(env->dblayer_env_lock); /* We will be causing logging activity */
    db_env = env->dblayer_DB_ENV;
  }
  
  rc = db_create(&db, db_env, 0); /* must use new handle to database */
  if (0 != rc) {
    LDAPDebug(LDAP_DEBUG_ANY, "db_remove: Failed to create db (%d) %s\n",
                  rc, dblayer_strerror(rc), 0);
    goto done;
  }
  rc = db->remove(db, path, dbName, 0); /* kiss the db goodbye! */

done:
  if (env) {
    if(use_lock) slapi_rwlock_unlock(env->dblayer_env_lock);
  }
  
  return rc;
}


int
dblayer_db_remove(dblayer_private_env * env, char const path[], char const dbName[]) {
    return(dblayer_db_remove_ex(env,path,dbName,PR_TRUE));
}

#define DBLAYER_CACHE_DELAY     PR_MillisecondsToInterval(250)
int dblayer_erase_index_file_ex(backend *be, struct attrinfo *a,
                                PRBool use_lock, int no_force_checkpoint)
{
  struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
  dblayer_private *priv = (dblayer_private*) li->li_dblayer_private;
  struct dblayer_private_env *pEnv = priv->dblayer_env;
  ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
  dblayer_handle *handle = NULL;
  char dbName[MAXPATHLEN];
  char *dbNamep;
  char *p;
  int dbbasenamelen, dbnamelen;
  int rc = 0;
  DB *db = 0;

  if (NULL == pEnv)        /* db does not exist */
    return rc;
 
  /* Added for bug 600401. Somehow the checkpoint thread deadlocked on
     index file with this function, index file couldn't be removed on win2k.
     Force a checkpoint here to break deadlock.
  */
  if (0 == no_force_checkpoint) {
    dblayer_force_checkpoint(li);
  }

  if (0 == dblayer_get_index_file(be, a, &db, 0 /* Don't create an index file
                                                   if it does not exist. */)) {
    if(use_lock) slapi_rwlock_wrlock(pEnv->dblayer_env_lock); /* We will be causing logging activity */
    /* first, remove the file handle for this index, if we have it open */
    PR_Lock(inst->inst_handle_list_mutex);
    if (a->ai_dblayer) {
      /* there is a handle */
      handle = (dblayer_handle *)a->ai_dblayer;
      
      /* when we successfully called dblayer_get_index_file we bumped up
         the reference count of how many threads are using the index. So we
         must manually back off the count by one here.... rwagner */
      
      dblayer_release_index_file(be, a, db);
      
      while (a->ai_dblayer_count > 0) {
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
      dblayer_close_file(&(handle->dblayer_dbp));
      
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
        if (dbnamelen > MAXPATHLEN)
        {
          dbNamep = (char *)slapi_ch_realloc(dbNamep, dbnamelen);
        }
        p = dbNamep + dbbasenamelen;
        sprintf(p, "%c%s%s",
                   get_sep(dbNamep), a->ai_type, LDBM_FILENAME_SUFFIX);
        rc = dblayer_db_remove_ex(pEnv, dbNamep, 0, 0);
        a->ai_dblayer = NULL;
        if (dbNamep != dbName)
          slapi_ch_free_string(&dbNamep);
      }
      else
      {
        rc = -1;
      }
      slapi_ch_free((void **)&handle);
    } else {
      /* no handle to close */
    }
    PR_Unlock(inst->inst_handle_list_mutex);
    if(use_lock) slapi_rwlock_unlock(pEnv->dblayer_env_lock);

  }
  
  return rc;
}

int dblayer_erase_index_file_nolock(backend *be, struct attrinfo *a, int no_force_chkpt) {
    return dblayer_erase_index_file_ex(be,a,PR_FALSE,no_force_chkpt);
}

int dblayer_erase_index_file(backend *be, struct attrinfo *a, int no_force_chkpt) {
    return dblayer_erase_index_file_ex(be,a,PR_TRUE,no_force_chkpt);
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
int dblayer_txn_init(struct ldbminfo *li, back_txn *txn)
{
    back_txn *cur_txn = dblayer_get_pvt_txn();
    PR_ASSERT(NULL != txn);

    if (cur_txn && txn) {
        txn->back_txn_txn = cur_txn->back_txn_txn;
    } else if (txn) {
        txn->back_txn_txn = NULL;
    }
    return 0;
}


int
dblayer_txn_begin_ext(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dblayer_private *priv = NULL;
    back_txn new_txn = {NULL};
    PR_ASSERT(NULL != li);
    /*
     * When server is shutting down, some components need to
     * flush some data (e.g. replication to write ruv).
     * So don't check shutdown signal unless we can't write.
     */
    if ( g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL ) {
        return return_value;
    }

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    if (txn) {
        txn->back_txn_txn = NULL;
    }

    if (priv->dblayer_enable_transactions)
    {
        dblayer_private_env *pEnv = priv->dblayer_env;
        if(use_lock) slapi_rwlock_rdlock(pEnv->dblayer_env_lock);
        if (!parent_txn)
        {
            /* see if we have a stored parent txn */
            back_txn *par_txn_txn = dblayer_get_pvt_txn();
            if (par_txn_txn) {
                parent_txn = par_txn_txn->back_txn_txn;
            }
        }
        return_value = TXN_BEGIN(pEnv->dblayer_DB_ENV,
                                 (DB_TXN*)parent_txn,
                                 &new_txn.back_txn_txn,
                                 DB_TXN_NOWAIT);
        if (0 != return_value) 
        {
            if(use_lock) slapi_rwlock_unlock(priv->dblayer_env->dblayer_env_lock);
        }
        else
        {
            /* this txn is now our current transaction for current operations
               and new parent for any nested transactions created */
            dblayer_push_pvt_txn(&new_txn);
            if (txn) {
                txn->back_txn_txn = new_txn.back_txn_txn;
            }
        }
    } else
    {
        return_value = 0;
    }
    if (0 != return_value) 
    {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Serious Error---Failed in dblayer_txn_begin, err=%d (%s)\n",
                  return_value, dblayer_strerror(return_value), 0);
    }
    return return_value;
}

int
dblayer_read_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return (dblayer_txn_begin_ext(li,parent_txn,txn,PR_FALSE));
}

int
dblayer_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc = 0;
    if (SERIALLOCK(li)) {
        dblayer_lock_backend(be);
    }
    rc = dblayer_txn_begin_ext(li,parent_txn,txn,PR_TRUE);
    if (rc && SERIALLOCK(li)) {
        dblayer_unlock_backend(be);
    }
    return rc;
}


int dblayer_txn_commit_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dblayer_private *priv = NULL;
    DB_TXN *db_txn = NULL;
    back_txn *cur_txn = NULL;

    PR_ASSERT(NULL != li);

    priv = (dblayer_private*)li->li_dblayer_private;
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
        1 != priv->dblayer_stop_threads &&
        priv->dblayer_env &&
        priv->dblayer_enable_transactions)
    {
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
        if ((priv->dblayer_durable_transactions) && use_lock ) {
            if(trans_batch_limit > 0) {        
                if(trans_batch_count % trans_batch_limit) {
                    trans_batch_count++;    
                } else {
                    LOG_FLUSH(priv->dblayer_env->dblayer_DB_ENV,0);
                    trans_batch_count=1;
                }        
            } else if(trans_batch_limit == FLUSH_REMOTEOFF) { /* user remotely turned batching off */
                LOG_FLUSH(priv->dblayer_env->dblayer_DB_ENV,0);
            }
        }
        if(use_lock) slapi_rwlock_unlock(priv->dblayer_env->dblayer_env_lock);
    } else
    {
        return_value = 0;
    }

    if (0 != return_value) 
    {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Serious Error---Failed in dblayer_txn_commit, err=%d (%s)\n",
                  return_value, dblayer_strerror(return_value), 0);
        if (LDBM_OS_ERR_IS_DISKFULL(return_value)) {
            operation_out_of_disk_space();
        }
    }
    return return_value;
}

int
dblayer_read_txn_commit(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return(dblayer_txn_commit_ext(li,txn,PR_FALSE));
}

int
dblayer_txn_commit(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc = dblayer_txn_commit_ext(li,txn,PR_TRUE);
    if (SERIALLOCK(li)) {
        dblayer_unlock_backend(be);
    }
    return rc;
}

int dblayer_txn_abort_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    int return_value = -1;
    dblayer_private *priv = NULL;
    DB_TXN *db_txn = NULL;
    back_txn *cur_txn = NULL;

    PR_ASSERT(NULL != li);

    priv = (dblayer_private*)li->li_dblayer_private;
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
        priv->dblayer_enable_transactions)
    {
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
        if(use_lock) slapi_rwlock_unlock(priv->dblayer_env->dblayer_env_lock);
    } else
    {
        return_value = 0;
    }    

    if (0 != return_value) 
    {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Serious Error---Failed in dblayer_txn_abort, err=%d (%s)\n",
                  return_value, dblayer_strerror(return_value), 0);
        if (LDBM_OS_ERR_IS_DISKFULL(return_value)) {
            operation_out_of_disk_space();
        }
    }
    return return_value;                                         
}

int
dblayer_read_txn_abort(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return(dblayer_txn_abort_ext(li, txn, PR_FALSE));
}

int
dblayer_txn_abort(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc = dblayer_txn_abort_ext(li, txn, PR_TRUE);
    if (SERIALLOCK(li)) {
        dblayer_unlock_backend(be);
    }
    return rc;
}

int
dblayer_txn_begin_all(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn)
{
    return (dblayer_txn_begin_ext(li,parent_txn,txn,PR_TRUE));
}

int
dblayer_txn_commit_all(struct ldbminfo *li, back_txn *txn)
{
    return(dblayer_txn_commit_ext(li, txn, PR_TRUE));
}

int
dblayer_txn_abort_all(struct ldbminfo *li, back_txn *txn)
{
    return(dblayer_txn_abort_ext(li, txn, PR_TRUE));
}

size_t dblayer_get_optimal_block_size(struct ldbminfo *li)
{
    dblayer_private *priv = NULL;
    size_t page_size = 0;

    PR_ASSERT(NULL != li);
    
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    page_size = (priv->dblayer_page_size == 0) ? DBLAYER_PAGESIZE : priv->dblayer_page_size; 
    if (priv->dblayer_idl_divisor == 0) 
    {
        return page_size - DB_EXTN_PAGE_HEADER_SIZE;
    } else 
    {
        return page_size / priv->dblayer_idl_divisor;
    }
}


/* 
 * The dblock serializes writes to the database,
 * which reduces deadlocking in the db code,
 * which means that we run faster.
 */
void dblayer_lock_backend(backend *be)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *) be->be_instance_info;
    PR_ASSERT(NULL != inst);
    
    if (NULL != inst->inst_db_mutex) {
        PR_EnterMonitor(inst->inst_db_mutex);
    }
}

void dblayer_unlock_backend(backend *be)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *) be->be_instance_info;
    PR_ASSERT(NULL != inst);
    
    if (NULL != inst->inst_db_mutex) {
        PR_ExitMonitor(inst->inst_db_mutex);
    }
}


/* code which implements checkpointing and log file truncation */

/*
 * create a thread for perf_threadmain
 */
static int
dblayer_start_perf_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread (PR_USER_THREAD,
                                 (VFP) (void *) perf_threadmain, li,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD, 
                                 SLAPD_DEFAULT_THREAD_STACKSIZE) )
    {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "failed to create database perf thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        return_value = -1;
    }
    return return_value;
}

/* Performance thread */
static int perf_threadmain(void *param)
{
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo*)param;

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(priv);

    while (!priv->dblayer_stop_threads) {
        /* sleep for a while, updating perf counters if we need to */
        perfctrs_wait(1000,priv->perf_private,priv->dblayer_env->dblayer_DB_ENV);
    }

    DECR_THREAD_COUNT(priv);
    LDAPDebug(LDAP_DEBUG_TRACE, "Leaving perf_threadmain\n", 0, 0, 0);
    return 0;
}

/*
 * create a thread for deadlock_threadmain
 */
static int
dblayer_start_deadlock_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread (PR_USER_THREAD,
                                 (VFP) (void *) deadlock_threadmain, li,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD,
                                 SLAPD_DEFAULT_THREAD_STACKSIZE) )
    {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "failed to create database deadlock thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        return_value = -1;
    }
    return return_value;
}

static const u_int32_t default_flags = DB_NEXT;

/* this is the loop delay - how long after we release the db pages
   until we acquire them again */
#define TXN_TEST_LOOP_WAIT(msecs) do {                             \
    if (msecs) {                                                   \
        DS_Sleep(PR_MillisecondsToInterval(slapi_rand() % msecs)); \
    }                                                              \
} while (0)

/* this is how long we hold the pages open until we close the cursors */
#define TXN_TEST_PAGE_HOLD(msecs) do {                             \
    if (msecs) {                                                   \
        DS_Sleep(PR_MillisecondsToInterval(slapi_rand() % msecs)); \
    }                                                              \
} while (0)

typedef struct txn_test_iter {
    DB *db;
    DBC *cur;
    size_t cnt;
    const char *attr;
    u_int32_t flags;
    backend *be;
} txn_test_iter;

typedef struct txn_test_cfg {
    PRUint32 hold_msec;
    PRUint32 loop_msec;
    u_int32_t flags;
    int use_txn;
    char **indexes;
    int verbose;
} txn_test_cfg;

static txn_test_iter *
new_txn_test_iter(DB *db, const char *attr, backend *be, u_int32_t flags)
{
    txn_test_iter *tti = (txn_test_iter *)slapi_ch_malloc(sizeof(txn_test_iter));
    tti->db = db;
    tti->cur = NULL;
    tti->cnt = 0;
    tti->attr = attr;
    tti->flags = default_flags|flags;
    tti->be = be;
    return tti;
}

static void
init_txn_test_iter(txn_test_iter *tti)
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
free_txn_test_iter(txn_test_iter *tti)
{
    init_txn_test_iter(tti);
    slapi_ch_free((void **)&tti);
}

static void
free_ttilist(txn_test_iter ***ttilist, size_t *tticnt)
{
    if (!ttilist || !*ttilist || !**ttilist) {
        return;
    }
    while (*tticnt > 0) {
        (*tticnt)--;
        free_txn_test_iter((*ttilist)[*tticnt]);
    }
    slapi_ch_free((void *)ttilist);
}

static void
init_ttilist(txn_test_iter **ttilist, size_t tticnt)
{
    if (!ttilist || !*ttilist) {
        return;
    }
    while (tticnt > 0) {
        tticnt--;
        init_txn_test_iter(ttilist[tticnt]);
    }
}

static void
print_ttilist(txn_test_iter **ttilist, size_t tticnt)
{
    while (tticnt > 0) {
        tticnt--;
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "txn_test_threadmain: attr [%s] cnt [%lu]\n",
                       ttilist[tticnt]->attr, ttilist[tticnt]->cnt);
    }
}

#define TXN_TEST_IDX_OK_IF_NULL "nscpEntryDN"

static void
txn_test_init_cfg(txn_test_cfg *cfg)
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

    slapi_log_error(SLAPI_LOG_FATAL, "txn_test_threadmain",
                    "Config hold_msec [%d] loop_msec [%d] rmw [%d] txn [%d] indexes [%s]\n",
                    cfg->hold_msec, cfg->loop_msec, cfg->flags, cfg->use_txn,
                    getenv(TXN_TEST_INDEXES) ? getenv(TXN_TEST_INDEXES) : indexlist);
}

static int txn_test_threadmain(void *param)
{
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;
    Object *inst_obj;
    int rc = 0;
    txn_test_iter **ttilist = NULL;
    size_t tticnt = 0;
    DB_TXN *txn = NULL;
    txn_test_cfg cfg = {0};
    size_t counter = 0;
    char keybuf[8192];
    char databuf[8192];
    int dbattempts = 0;
    int dbmaxretries = 3;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo*)param;

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(priv);

    txn_test_init_cfg(&cfg);

    if (!priv->dblayer_enable_transactions) {
        goto end;
    }

wait_for_init:
    free_ttilist(&ttilist, &tticnt);
    DS_Sleep(PR_MillisecondsToInterval(1000));
    if (priv->dblayer_stop_threads) {
        goto end;
    }
    dbattempts++;
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        char **idx = NULL;
        ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);
        backend *be = inst->inst_be;

        if (be->be_state != BE_STATE_STARTED) {
            LDAPDebug0Args(LDAP_DEBUG_ANY,
                           "txn_test_threadmain: backend not started, retrying\n");
            object_release(inst_obj);
            goto wait_for_init;
        }

        for (idx = cfg.indexes; idx && *idx; ++idx) {
            DB *db = NULL;
            if (be->be_state != BE_STATE_STARTED) {
                LDAPDebug0Args(LDAP_DEBUG_ANY,
                               "txn_test_threadmain: backend not started, retrying\n");
                object_release(inst_obj);
                goto wait_for_init;
            }

            if (!strcmp(*idx, "id2entry")) {
                dblayer_get_id2entry(be, &db);
                if (db == NULL) {
                    LDAPDebug0Args(LDAP_DEBUG_ANY,
                                   "txn_test_threadmain: id2entry database not found or not ready yet, retrying\n");
                    object_release(inst_obj);
                    goto wait_for_init;
                }
            } else {
                struct attrinfo *ai = NULL;
                ainfo_get(be, *idx, &ai);
                if (NULL == ai) {
                    if (dbattempts >= dbmaxretries) {
                        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                      "txn_test_threadmain: index [%s] not found or not ready yet, skipping\n",
                                  *idx);
                        continue;
                    } else {
                        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                      "txn_test_threadmain: index [%s] not found or not ready yet, retrying\n",
                                      *idx);
                        object_release(inst_obj);
                        goto wait_for_init;
                    }
                }
                if (dblayer_get_index_file(be, ai, &db, 0) || (NULL == db)) {
                    if ((NULL == db) && strcasecmp(*idx, TXN_TEST_IDX_OK_IF_NULL)) {
                        if (dbattempts >= dbmaxretries) {
                            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                          "txn_test_threadmain: database file for index [%s] not found or not ready yet, skipping\n",
                                          *idx);
                            continue;
                        } else {
                            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                          "txn_test_threadmain: database file for index [%s] not found or not ready yet, retrying\n",
                                          *idx);
                            object_release(inst_obj);
                            goto wait_for_init;
                        }
                    }
                }
            }
            if (db) {
                ttilist = (txn_test_iter **)slapi_ch_realloc((char *)ttilist, sizeof(txn_test_iter *) * (tticnt + 1));
                ttilist[tticnt++] = new_txn_test_iter(db, *idx, be, cfg.flags);
            }
        }
    }

    LDAPDebug0Args(LDAP_DEBUG_ANY, "txn_test_threadmain: starting main txn stress loop\n");
    print_ttilist(ttilist, tticnt);

    while (!priv->dblayer_stop_threads) {
retry_txn:
        init_ttilist(ttilist, tticnt);
        if (txn) {
            TXN_ABORT(txn);
            txn = NULL;
        }
        if (cfg.use_txn) {
            rc = TXN_BEGIN(priv->dblayer_env->dblayer_DB_ENV, NULL, &txn, 0);
            if (rc || !txn) {
                LDAPDebug2Args(LDAP_DEBUG_ANY,
                               "txn_test_threadmain failed to create a new transaction, err=%d (%s)\n",
                               rc, dblayer_strerror(rc));
            }
        } else {
            rc = 0;
        }
        if (!rc) {
            DBT key;
            DBT data;
            size_t ii;
            size_t donecnt = 0;
            size_t cnt = 0;

            /* phase 1 - open a cursor to each db */
            if (cfg.verbose) {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "txn_test_threadmain: starting [%lu] indexes\n", tticnt);
            }
            for (ii = 0; ii < tticnt; ++ii) {
                txn_test_iter *tti = ttilist[ii];

retry_cursor:
                if (priv->dblayer_stop_threads) {
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
                        LDAPDebug0Args(LDAP_DEBUG_ANY,
                                       "txn_test_threadmain cursor create deadlock - retry\n");
                    }
                    if (cfg.use_txn) {
                        goto retry_txn;
                    } else {
                        goto retry_cursor;
                    }
                } else if (rc) {
                    LDAPDebug2Args(LDAP_DEBUG_ANY,
                                   "txn_test_threadmain failed to create a new cursor, err=%d (%s)\n",
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
                    txn_test_iter *tti = ttilist[ii];
                    if (tti->cur) {
retry_get:
                        if (priv->dblayer_stop_threads) {
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
                                LDAPDebug0Args(LDAP_DEBUG_ANY,
                                               "txn_test_threadmain cursor get deadlock - retry\n");
                            }
                            if (cfg.use_txn) {
                                goto retry_txn;
                            } else {
                                goto retry_get;
                            }
                        } else if (DB_NOTFOUND == rc) {
                            donecnt++; /* ran out of this one */
                            tti->flags = DB_FIRST|cfg.flags; /* start over until all indexes are done */
                        } else if (rc) {
                            if ((DB_BUFFER_SMALL != rc) || cfg.verbose) {
                                LDAPDebug2Args(LDAP_DEBUG_ANY,
                                               "txn_test_threadmain failed to read a cursor, err=%d (%s)\n",
                                               rc, dblayer_strerror(rc));
                            }
                            tti->cur->c_close(tti->cur);
                            tti->cur = NULL;
                            donecnt++;
                        } else {
                            tti->cnt++;
                            tti->flags = default_flags|cfg.flags;
                            cnt++;
                        }
                    }
                }
            }
            TXN_TEST_PAGE_HOLD(cfg.hold_msec);
            /*print_ttilist(ttilist, tticnt);*/
            init_ttilist(ttilist, tticnt);
            if (cfg.verbose) {
                LDAPDebug2Args(LDAP_DEBUG_ANY,
                               "txn_test_threadmain: finished [%lu] indexes [%lu] records\n", tticnt, cnt);
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
    free_ttilist(&ttilist, &tticnt);
    if (txn) {
        TXN_ABORT(txn);
    }        
    DECR_THREAD_COUNT(priv);
    return 0;
}

/*
 * create a thread for transaction deadlock testing
 */
static int
dblayer_start_txn_test_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread (PR_USER_THREAD,
                                 (VFP) (void *) txn_test_threadmain, li,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD,
                                 SLAPD_DEFAULT_THREAD_STACKSIZE) )
    {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "failed to create txn test thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        return_value = -1;
    }
    return return_value;
}

/* deadlock thread main function */

static int deadlock_threadmain(void *param)
{
    int rval = -1;
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;
    PRIntervalTime    interval;   /*NSPR timeout stuffy*/

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo*)param;

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(priv);

    interval = PR_MillisecondsToInterval(100);
    while (!priv->dblayer_stop_threads)
    {
        if (priv->dblayer_enable_transactions) 
        {
            if (dblayer_db_uses_locking(priv->dblayer_env->dblayer_DB_ENV)) {
                int aborted;
                if ((rval = LOCK_DETECT(priv->dblayer_env->dblayer_DB_ENV,
                                        0, DB_LOCK_YOUNGEST, &aborted)) != 0) {
                    LDAPDebug(LDAP_DEBUG_ANY,
                      "Serious Error---Failed in deadlock detect (aborted at 0x%x), err=%d (%s)\n",
                      aborted, rval, dblayer_strerror(rval));
                }
            }
        }
        DS_Sleep(interval);
    }

    DECR_THREAD_COUNT(priv);
    LDAPDebug(LDAP_DEBUG_TRACE, "Leaving deadlock_threadmain\n", 0, 0, 0);
    return 0;
}

#define checkpoint_debug_message(debug, fmt, a1, a2, a3) \
    if (debug) { LDAPDebug(LDAP_DEBUG_ANY,fmt,a1,a2,a3); }

/* this thread tries to do two things: 
    1. catch a group of transactions that are pending allowing a worker thread 
       to work
    2. flush any left over transactions ( a single transaction for example)
*/

static int
dblayer_start_log_flush_thread(dblayer_private *priv)
{
    int return_value = 0;

    if ((priv->dblayer_durable_transactions) && 
        (priv->dblayer_enable_transactions) && (trans_batch_limit > 0)) {
        log_flush_thread=PR_TRUE;
        if (NULL == PR_CreateThread (PR_USER_THREAD,
                                     (VFP) (void *) log_flush_threadmain, priv,
                                     PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                     PR_UNJOINABLE_THREAD, 
                                     SLAPD_DEFAULT_THREAD_STACKSIZE) ) {
            PRErrorCode prerr = PR_GetError();
            LDAPDebug(LDAP_DEBUG_ANY,
                "failed to create database log flush thread, "
                SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                prerr, slapd_pr_strerror(prerr), 0);
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

static int log_flush_threadmain(void *param)
{
    dblayer_private *priv = NULL;
    PRIntervalTime    interval;

    PR_ASSERT(NULL != param);
    priv = (dblayer_private *) param;

    INCR_THREAD_COUNT(priv);

    interval = PR_MillisecondsToInterval(300);
    while ((!priv->dblayer_stop_threads) && (log_flush_thread))
    {      
        if (priv->dblayer_enable_transactions) 
        {
          DB_CHECKPOINT_LOCK(1, priv->dblayer_env->dblayer_env_lock);
          if(trans_batch_limit > 0) {
            if(trans_batch_count > 1) {
              LOG_FLUSH(priv->dblayer_env->dblayer_DB_ENV,0);
              trans_batch_count=1;
            }
          }
          DB_CHECKPOINT_UNLOCK(1, priv->dblayer_env->dblayer_env_lock);
        }
        DS_Sleep(interval);
    }

    DECR_THREAD_COUNT(priv);
    LDAPDebug(LDAP_DEBUG_TRACE, "Leaving log_flush_threadmain\n", 0, 0, 0);
    return 0;
}

/*
 * create a thread for checkpoint_threadmain
 */
static int
dblayer_start_checkpoint_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread (PR_USER_THREAD,
                                 (VFP) (void *) checkpoint_threadmain, li,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD, 
                                 SLAPD_DEFAULT_THREAD_STACKSIZE) )
    {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY,
                  "failed to create database checkpoint thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        return_value = -1;
    }
    return return_value;
}

static int checkpoint_threadmain(void *param)
{
    time_t time_of_last_checkpoint_completion = 0;    /* seconds since epoch */
    PRIntervalTime    interval;
    int rval = -1;
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;
    int debug_checkpointing = 0;
    int checkpoint_interval;
    char *home_dir = NULL;
    char **list = NULL;
    char **listp = NULL;
    struct dblayer_private_env *penv = NULL;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo*)param;

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(priv);

    interval = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL);
    home_dir = dblayer_get_home_dir(li, NULL);
    if (NULL == home_dir || '\0' == *home_dir)
    {
        LDAPDebug(LDAP_DEBUG_ANY, 
            "Checkpoint thread failed due to missing db home directory info\n",
            0, 0, 0);
        goto error_return;
    }

    /* work around a problem with newly created environments */
    dblayer_force_checkpoint(li);

    penv = priv->dblayer_env;
    debug_checkpointing = priv->db_debug_checkpointing;
    /* assumes dblayer_force_checkpoint worked */
    time_of_last_checkpoint_completion = current_time();
    while (!priv->dblayer_stop_threads)
    {
        /* sleep for a while */
        /* why aren't we sleeping exactly the right amount of time ? */
        /* answer---because the interval might be changed after the server 
         * starts up */
        DS_Sleep(interval);

        if (0 == priv->dblayer_enable_transactions) 
            continue;

        PR_Lock(li->li_config_mutex);
        checkpoint_interval = priv->dblayer_checkpoint_interval;
        PR_Unlock(li->li_config_mutex);

        /* Check to see if the checkpoint interval has elapsed */
        if (current_time() - time_of_last_checkpoint_completion <
                                               checkpoint_interval) 
            continue;

        if (!dblayer_db_uses_transactions(priv->dblayer_env->dblayer_DB_ENV))
            continue;

        /* now checkpoint */
        checkpoint_debug_message(debug_checkpointing,
                                 "Starting checkpoint\n", 0, 0, 0);
        rval = dblayer_txn_checkpoint(li, priv->dblayer_env, 
                                      PR_TRUE, PR_TRUE, PR_FALSE);
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
        if (DB_INCOMPLETE == rval) 
        {
            checkpoint_debug_message(debug_checkpointing,
                                     "Retrying checkpoint\n", 0, 0, 0);
        } else
#endif
        {
            checkpoint_debug_message(debug_checkpointing,
                                     "Checkpoint Done\n", 0, 0, 0);
            if (rval != 0) {
                /* bad error */
                LDAPDebug(LDAP_DEBUG_ANY,
                    "Serious Error---Failed to checkpoint database, "
                    "err=%d (%s)\n", rval, dblayer_strerror(rval), 0);
                if (LDBM_OS_ERR_IS_DISKFULL(rval)) {
                    operation_out_of_disk_space();
                    goto error_return;
                }
            } else {
                time_of_last_checkpoint_completion = current_time();
            }
        }

        checkpoint_debug_message(debug_checkpointing,
                                 "Starting checkpoint\n", 0, 0, 0);
        rval = dblayer_txn_checkpoint(li, priv->dblayer_env, 
                                      PR_TRUE, PR_TRUE, PR_FALSE);
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
        if (DB_INCOMPLETE == rval) 
        {
            checkpoint_debug_message(debug_checkpointing,
                                     "Retrying checkpoint\n", 0, 0, 0);
        } else
#endif
        {
            checkpoint_debug_message(debug_checkpointing,
                                     "Checkpoint Done\n", 0, 0, 0);
            if (rval != 0) {
                /* bad error */
                LDAPDebug(LDAP_DEBUG_ANY,
                    "Serious Error---Failed to checkpoint database, "
                    "err=%d (%s)\n", rval, dblayer_strerror(rval), 0);
                if (LDBM_OS_ERR_IS_DISKFULL(rval)) {
                    operation_out_of_disk_space();
                    goto error_return;
                }
            } else {
                time_of_last_checkpoint_completion = current_time();
            }
        }
        /* find out which log files don't contain active txns */
        DB_CHECKPOINT_LOCK(PR_TRUE, penv->dblayer_env_lock);
        rval = LOG_ARCHIVE(penv->dblayer_DB_ENV, &list,
                           DB_ARCH_ABS, (void *)slapi_ch_malloc);
        DB_CHECKPOINT_UNLOCK(PR_TRUE, penv->dblayer_env_lock);
        if (rval) {
            LDAPDebug2Args(LDAP_DEBUG_ANY, "checkpoint_threadmain: "
                           "log archive failed - %s (%d)\n", 
                           dblayer_strerror(rval), rval);
        } else {
            for (listp = list; listp && *listp != NULL; ++listp) {
                if (priv->dblayer_circular_logging) {
                    checkpoint_debug_message(debug_checkpointing,
                                             "Deleting %s\n", *listp, 0, 0);
                    unlink(*listp);
                } else {
                    char new_filename[MAXPATHLEN];
                    PR_snprintf(new_filename, sizeof(new_filename),
                                "%s.old", *listp);
                    checkpoint_debug_message(debug_checkpointing,
                                "Renaming %s -> %s\n",*listp, new_filename, 0);
                    rename(*listp, new_filename);    
                }
            }
            slapi_ch_free((void**)&list);
            /* Note: references inside the returned memory need not be 
             * individually freed. */
        }
    }
    LDAPDebug0Args(LDAP_DEBUG_TRACE, "Check point before leaving\n");
    rval = dblayer_force_checkpoint(li);
error_return:

    DECR_THREAD_COUNT(priv);
    LDAPDebug0Args(LDAP_DEBUG_TRACE, "Leaving checkpoint_threadmain\n");
    return rval;
}

/*
 * create a thread for trickle_threadmain
 */
static int
dblayer_start_trickle_thread(struct ldbminfo *li)
{
    int return_value = 0;
    if (NULL == PR_CreateThread (PR_USER_THREAD,
                                 (VFP) (void *) trickle_threadmain, li,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD, 
                                 SLAPD_DEFAULT_THREAD_STACKSIZE) )
    {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "failed to create database trickle thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        return_value = -1;
    }
    return return_value;
}

static int trickle_threadmain(void *param)
{
    PRIntervalTime    interval;   /*NSPR timeout stuffy*/
    int rval = -1;
    dblayer_private *priv = NULL;
    struct ldbminfo *li = NULL;
    int debug_checkpointing = 0;

    PR_ASSERT(NULL != param);
    li = (struct ldbminfo*)param;

    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    INCR_THREAD_COUNT(priv);

    interval = PR_MillisecondsToInterval(DBLAYER_SLEEP_INTERVAL);
    debug_checkpointing = priv->db_debug_checkpointing;
    while (!priv->dblayer_stop_threads)
    {
        DS_Sleep(interval);   /* 622855: wait for other threads fully started */
        if (priv->dblayer_enable_transactions) 
        {
            if ( dblayer_db_uses_mpool(priv->dblayer_env->dblayer_DB_ENV) &&
                 (0 != priv->dblayer_trickle_percentage) )
            {
                int pages_written = 0;
                if ((rval = MEMP_TRICKLE(priv->dblayer_env->dblayer_DB_ENV,
                                         priv->dblayer_trickle_percentage,
                                         &pages_written)) != 0)
                {
                    LDAPDebug(LDAP_DEBUG_ANY,"Serious Error---Failed to trickle, err=%d (%s)\n",rval,dblayer_strerror(rval), 0);
                }
                if (pages_written > 0) 
                {
                    checkpoint_debug_message(debug_checkpointing,"Trickle thread wrote %d pages\n",pages_written,0, 0);
                }
            }
        }
    }

    DECR_THREAD_COUNT(priv);
    LDAPDebug(LDAP_DEBUG_TRACE, "Leaving trickle_threadmain priv\n", 0, 0, 0);
    return 0;
}


/* better atol -- it understands a trailing multiplier k/m/g
 * for example, "32k" will be returned as 32768
 * richm: added better error checking and support for 64 bit values.
 * The err parameter is used by the caller to tell if there was an error
 * during the a to i conversion - if 0, the value was successfully
 * converted - if non-zero, there was some error (e.g. not a number)
 */
PRInt64 db_atol(char *str, int *err)
{
    PRInt64 mres1 = LL_INIT(0, 1);
    PRInt64 mres2 = LL_INIT(0, 1);
    PRInt64 mres3 = LL_INIT(0, 1);
    PRInt64 onek = LL_INIT(0, 1024);
    PRInt64 multiplier = LL_INIT(0, 1);
    PRInt64 val = LL_INIT(0, 0);
    PRInt64 result = LL_INIT(0, 0);
    char x = 0;
    int num = PR_sscanf(str, "%lld%c", &val, &x);
    if (num < 1) { /* e.g. not a number */
        if (err)
            *err = 1;
        return result; /* return 0 */
    }

    switch (x) {
    case 'g':
    case 'G':
    LL_MUL(mres1, onek, multiplier);
/*    multiplier *= 1024;*/
    case 'm':
    case 'M':
    LL_MUL(mres2, onek, mres1);
/*    multiplier *= 1024;*/
    case 'k':
    case 'K':
    LL_MUL(mres3, onek, mres2);
/*    multiplier *= 1024;*/
    }
    LL_MUL(result, val, mres3);
/*    result = val * multiplier;*/
    if (err)
        *err = 0;
    return result;
}

PRInt64 db_atoi(char *str, int *err)
{
    return db_atol(str, err);
}

unsigned long db_strtoul(const char *str, int *err)
{
    unsigned long val = 0, result, multiplier = 1;
    char *p;
    errno = 0;

    /*
     * manpage of strtoul: Negative  values  are considered valid input and
     * are silently converted to the equivalent unsigned long int value.
     */
    /* We don't want to make it happen. */
    for (p = (char *)str; p && *p && (*p == ' ' || *p == '\t'); p++) ;
    if ('-' == *p) {
        if (err) *err = ERANGE;
        return val;
    }
    val = strtoul(str, &p, 10);
    if (errno != 0) {
        if (err) *err = errno;
        return val;
    }

    switch (*p) {
    case 'g':
    case 'G':
        multiplier *= 1024;
    case 'm':
    case 'M':
        multiplier *= 1024;
    case 'k':
    case 'K':
        multiplier *= 1024;
        p++;
        if (*p == 'b' || *p == 'B') p++;
        if (err) {
            /* extra chars? */
            *err = (*p != '\0') ? EINVAL : 0;
        }
        break;
    case '\0':
        if (err) *err = 0;
        break;
    default:
        if (err) *err = EINVAL;
        return val;
    }

    result = val * multiplier;

    return result;
}

/* functions called directly by the plugin interface from the front-end */

/* Begin transaction */
int dblayer_plugin_begin(Slapi_PBlock *pb)
{
    int return_value = -1;
    back_txnid    parent;
    back_txn    current;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get( pb, SLAPI_PARENT_TXN, (void**)&parent );

    if (NULL == be) {
        Slapi_DN *sdn;
        slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
        if (NULL == sdn) {
            return return_value;
        }
        be = slapi_be_select(sdn);
        if (NULL == be) {
            return return_value;
        }
        slapi_pblock_set(pb, SLAPI_BACKEND, be);
    }
    /* call begin, and put the result in the txnid parameter */
    return_value = dblayer_txn_begin(be, parent, &current);

    if (0 == return_value) 
    {
        slapi_pblock_set( pb, SLAPI_TXN, (void*)current.back_txn_txn );
    }

    return return_value;
}

/* Commit transaction */
int dblayer_plugin_commit(Slapi_PBlock *pb)
{
    /* get the txnid and call commit */
    int return_value = -1;
    back_txn    current;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get( pb, SLAPI_TXN, (void**)&(current.back_txn_txn) );
    if (NULL == be) {
        return return_value;
    }

    return_value = dblayer_txn_commit(be, &current);

    return return_value;
}

/* Abort Transaction */
int dblayer_plugin_abort(Slapi_PBlock *pb)
{
    /* get the txnid and call abort */
    int return_value = -1;
    back_txn    current;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get( pb, SLAPI_TXN, (void**)&(current.back_txn_txn) );
    if (NULL == be) {
        return return_value;
    }

    return_value = dblayer_txn_abort(be, &current);

    return return_value;
}


/* Helper function for monitor stuff */
int dblayer_memp_stat(struct ldbminfo *li, DB_MPOOL_STAT **gsp,
                      DB_MPOOL_FSTAT ***fsp)
{
    dblayer_private *priv = NULL;
    DB_ENV *env = NULL;

    PR_ASSERT(NULL != li);
    
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    env = priv->dblayer_env->dblayer_DB_ENV;
    PR_ASSERT(NULL != env);
    
    return MEMP_STAT(env, gsp, fsp, 0, (void *)slapi_ch_malloc);
}

/* import wants this one */
int dblayer_memp_stat_instance(ldbm_instance *inst, DB_MPOOL_STAT **gsp,
                               DB_MPOOL_FSTAT ***fsp)
{
    DB_ENV *env = NULL;
    dblayer_private *priv = NULL;

    PR_ASSERT(NULL != inst);

    if (inst->import_env->dblayer_DB_ENV) {
        env = inst->import_env->dblayer_DB_ENV;
    } else {
        priv = (dblayer_private *)inst->inst_li->li_dblayer_private;
        PR_ASSERT(NULL != priv);
        env = priv->dblayer_env->dblayer_DB_ENV;
    }
    PR_ASSERT(NULL != env);

    return MEMP_STAT(env, gsp, fsp, 0, (void *)slapi_ch_malloc);
}

/* Helper functions for recovery */

#define DB_LINE_LENGTH 80

static int commit_good_database(dblayer_private *priv)
{
    /* Write out the guard file */
    char filename[MAXPATHLEN];
    char line[DB_LINE_LENGTH * 2];
    PRFileDesc *prfd;
    int return_value = 0;
    int num_bytes;

    PR_snprintf(filename,sizeof(filename), "%s/guardian", priv->dblayer_home_directory);

    prfd = PR_Open(filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
        priv->dblayer_file_mode );
    if (NULL == prfd)
    {
        LDAPDebug( LDAP_DEBUG_ANY,"Fatal Error---Failed to write guardian file %s, database corruption possible" SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
            filename, PR_GetError(), slapd_pr_strerror(PR_GetError()) );
        return -1;
    } 
    PR_snprintf(line,sizeof(line),"cachesize:%lu\nncache:%d\nversion:%d\n",
            priv->dblayer_cachesize, priv->dblayer_ncache, DB_VERSION_MAJOR);
    num_bytes = strlen(line);
    return_value = slapi_write_buffer(prfd, line, num_bytes);
    if (return_value != num_bytes)
    {
        goto error;
    }
    return_value = PR_Close(prfd);
    if (PR_SUCCESS == return_value)
    {
        return 0;
    } else
    {
        LDAPDebug( LDAP_DEBUG_ANY,"Fatal Error---Failed to write guardian file, database corruption possible\n", 0,0, 0 );
        (void)PR_Delete(filename);
        return -1;
    }
error:
    (void)PR_Close(prfd);
    (void)PR_Delete(filename);
    return -1;
}

/* read the guardian file from db/ and possibly recover the database */
static int read_metadata(struct ldbminfo *li)
{
    char filename[MAXPATHLEN];
    char *buf;
    char *thisline;
    char *nextline;
    char **dirp;
    PRFileDesc *prfd;
    PRFileInfo prfinfo;
    int return_value = 0;
    PRInt32 byte_count = 0;
    char attribute[512];
    char value[128], delimiter;
    int number = 0;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    /* dblayer_recovery_required is initialized in dblayer_init;
     * and might be set 1 in check_db_version;
     * we don't want to override it
     * priv->dblayer_recovery_required = 0; */
    priv->dblayer_previous_cachesize = 0;
    priv->dblayer_previous_ncache = 0;
    /* Open the guard file and read stuff, then delete it */
    PR_snprintf(filename,sizeof(filename),"%s/guardian",priv->dblayer_home_directory);

    memset(&prfinfo, '\0', sizeof(PRFileInfo));
    (void)PR_GetFileInfo(filename, &prfinfo);

    prfd = PR_Open(filename,PR_RDONLY,priv->dblayer_file_mode);
    if (NULL == prfd || 0 == prfinfo.size) {
        /* file empty or not present--means the database needs recovered */
        int count = 0;
        for (dirp = priv->dblayer_data_directories; dirp && *dirp; dirp++)
        {
            count_dbfiles_in_dir(*dirp, &count, 1 /* recurse */);
            if (count > 0) {
#if 0
                char *home_dir;
                /* This code used to check for a broken import by looking 
                 * for a dbversion file.  If it wasn't there, then an import
                 * failed.  Now each instance has its own dbversion file.
                 * If this check is done at all, it ought to be done when
                 * bringing up individual backend instances.
                 */
                /* While we're here, let's check for a broken import.
                   This would be indicated by the following conditions:
                   1. db files in the directory.
                   2. No guardian file.
                   3. No DBVERSION file.
                   If we're here we have confitions 1 and 2,
                   so we should check for condition 3.
                   */
                if (!dbversion_exists(li, home_dir)) {
                    LDAPDebug( LDAP_DEBUG_ANY,"Fatal Error---database is corrupt. Server can't start. Most likely cause is a previously aborted import. Either re-import or delete the database and re-start the server.\n", 0,0, 0 );
                    return -1;
                } else {
                    priv->dblayer_recovery_required = 1;
                }
#endif
                priv->dblayer_recovery_required = 1;
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
        priv->dblayer_recovery_required = 1;
    } else {
        buf[ byte_count ] = '\0';
        thisline = buf;
        while (1) {
            /* Find the end of the line */
            nextline = strchr( thisline, '\n' );
            if (NULL != nextline) {
                *nextline++ = '\0';
                while ('\n' == *nextline) {
                    nextline++;
                }
            }
            sscanf(thisline,"%[a-z]%c%s",attribute,&delimiter,value);
            if (0 == strcmp("cachesize",attribute)) {
                priv->dblayer_previous_cachesize = strtoul(value, NULL, 10);
            } else if (0 == strcmp("ncache",attribute)) {
                number = atoi(value);
                priv->dblayer_previous_ncache = number;
            } else if (0 == strcmp("version",attribute)) {
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
        LDAPDebug(LDAP_DEBUG_ANY,
            "Fatal Error---Failed to delete guardian file, "
            "database corruption possible\n", 0, 0, 0 );
    }
    return return_value;
}

/* handy routine for checkpointing the db */
static int dblayer_force_checkpoint(struct ldbminfo *li)
{
  int ret = 0, i;
  dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
  struct dblayer_private_env *pEnv;
  
  if (NULL == priv || NULL == priv->dblayer_env){
    /* already terminated.  nothing to do */  
    return -1;
  }
   
  pEnv= priv->dblayer_env;
  
  if (priv->dblayer_enable_transactions) {
    
    LDAPDebug(LDAP_DEBUG_TRACE, "Checkpointing database ...\n", 0, 0, 0);
    
    /* 
     * DB workaround. Newly created environments do not know what the
     * previous checkpoint LSN is. The default LSN of [0][0] would
     * cause us to read all log files from very beginning during a
     * later recovery. Taking two checkpoints solves the problem.
     */
    
    for (i = 0; i < 2; i++) {
      ret = dblayer_txn_checkpoint(li, pEnv, PR_TRUE, PR_FALSE, PR_TRUE);
      if (ret == 0) continue;
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
      if (ret != DB_INCOMPLETE)
#endif
      {
        LDAPDebug(LDAP_DEBUG_ANY, "Checkpoint FAILED, error %s (%d)\n",
                  dblayer_strerror(ret), ret, 0);
        break;
      }
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR < 4100
      
      LDAPDebug(LDAP_DEBUG_ANY, "Busy: retrying checkpoint\n", 0, 0, 0);
      
      /* teletubbies: "again! again!" */
      ret = dblayer_txn_checkpoint(li, pEnv, PR_TRUE, PR_FALSE, PR_TRUE);
      if (ret == DB_INCOMPLETE) {
        LDAPDebug(LDAP_DEBUG_ANY, "Busy: giving up on checkpoint\n", 0, 0, 0);
        break;
      } else if (ret != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Checkpoint FAILED, error %s (%d)\n",
                  dblayer_strerror(ret), ret, 0);
        break;
      }
#endif
    }
  }

  return ret;
}

static int
_dblayer_delete_aux_dir(struct ldbminfo *li, char *path)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char filename[MAXPATHLEN];
    dblayer_private *priv = NULL;
    struct dblayer_private_env *pEnv = NULL;
    int rc = -1;

    if (NULL == li || NULL == path) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "_dblayer_delete_aux_dir: Invalid LDBM info (0x%x) "
                       "or path (0x%x)\n", li, path);
        return rc;
    }
    priv = (dblayer_private*)li->li_dblayer_private;
    if (priv) {
        pEnv = priv->dblayer_env;
    }
    dirhandle = PR_OpenDir(path);
    if (!dirhandle) {
        return 0; /* The dir does not exist. */
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle,
                                          PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (! direntry->name)
            break;
        PR_snprintf(filename, sizeof(filename), "%s/%s", path, direntry->name);
        if (pEnv &&
            /* PL_strcmp takes NULL arg */
            (PL_strcmp(LDBM_FILENAME_SUFFIX , strrchr(direntry->name, '.'))
             == 0)) {
            rc = dblayer_db_remove_ex(pEnv, filename, 0, PR_TRUE);
        } else {
            rc = ldbm_delete_dirs(filename);
        }
    }
    PR_CloseDir(dirhandle);
    PR_RmDir(path);
    return rc;
}

/* TEL:  Added startdb flag.  If set (1), the DB environment will be started so
 * that dblayer_db_remove_ex will be used to remove the database files instead
 * of simply deleting them.  That is important when doing a selective restoration
 * of a single backend (FRI).  If not set (0), the traditional remove is used.
 */
static int _dblayer_delete_instance_dir(ldbm_instance *inst, int startdb)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char filename[MAXPATHLEN];
    struct ldbminfo *li = inst->inst_li;
    dblayer_private *priv = NULL;
    struct dblayer_private_env *pEnv = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int rval = 0;

    if (NULL == li)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
                       "_dblayer_delete_instance_dir: NULL LDBM info\n");
        rval = -1;
        goto done;
    }

    if (startdb)
    {
        /* close immediately; no need to run db threads */
        rval = dblayer_start(li, DBLAYER_NORMAL_MODE|DBLAYER_NO_DBTHREADS_MODE);
        if (rval)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "_dblayer_delete_instance_dir: dblayer_start failed! %s (%d)\n",
                dblayer_strerror(rval), rval, 0);
            goto done;
        }
    }

    priv = (dblayer_private*)li->li_dblayer_private;
    if (NULL != priv)
    {
        pEnv = priv->dblayer_env;
    }

    if (inst->inst_dir_name == NULL)
        dblayer_get_instance_data_dir(inst->inst_be);

    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (inst_dirp && *inst_dirp) {
        dirhandle = PR_OpenDir(inst_dirp);
    }
    if (! dirhandle) {
        if ( PR_GetError() == PR_FILE_NOT_FOUND_ERROR ) {
             /* the directory does not exist... that's not an error */
             rval = 0;
             goto done;
        }
        if (inst_dirp && *inst_dirp) {
            LDAPDebug(LDAP_DEBUG_ANY,
              "_dblayer_delete_instance_dir: inst_dir is NULL\n", 0, 0, 0);
        } else {
            LDAPDebug(LDAP_DEBUG_ANY,
              "_dblayer_delete_instance_dir: PR_OpenDir(%s) failed (%d): %s\n", 
              inst_dirp, PR_GetError(),slapd_pr_strerror(PR_GetError()));
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
        if (! direntry->name)
            break;
        PR_snprintf(filename, MAXPATHLEN, "%s/%s", inst_dirp, direntry->name);
        if (pEnv &&
            /* PL_strcmp takes NULL arg */
            (PL_strcmp(LDBM_FILENAME_SUFFIX , strrchr(direntry->name, '.'))
             == 0)) {
            rval = dblayer_db_remove_ex(pEnv, filename, 0, PR_TRUE);
        } else {
            rval = ldbm_delete_dirs(filename);
        }
    }
    PR_CloseDir(dirhandle);
    if (pEnv && startdb)
    {
        rval = dblayer_close(li, DBLAYER_NORMAL_MODE);
        if (rval)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "_dblayer_delete_instance_dir: dblayer_close failed! %s (%d)\n",
                dblayer_strerror(rval), rval, 0);
        }
    }
done:
    /* remove the directory itself too */
    if (0 == rval)
        PR_RmDir(inst_dirp);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

/* delete the db3 files in a specific backend instance --
 * this is probably only used for import.
 * assumption: dblayer is open, but the instance has been closed.
 */
int dblayer_delete_instance_dir(backend *be)
{
  struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
  int ret = dblayer_force_checkpoint(li);
  
  if (ret != 0) {
    return ret;
  } else {
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    return _dblayer_delete_instance_dir(inst, 0);
  }
}


/* delete an entire db/ directory, including either all instances, or
 * just a single instance (leaving the others intact), if the instance param is non-NULL !
 * this is used mostly for restores.
 * dblayer is assumed to be closed.
 */
static int
dblayer_delete_database_ex(struct ldbminfo *li, char *instance, char *cldir)
{
    dblayer_private *priv = NULL;
    Object *inst_obj;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
	PRFileInfo fileinfo;
    char filename[MAXPATHLEN];
    char *log_dir;
    int ret;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* delete each instance */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);

        if (inst->inst_be->be_instance_info != NULL) {
			if ((NULL != instance) && (strcmp(inst->inst_name,instance) != 0)) 
			{
				LDAPDebug(LDAP_DEBUG_ANY,
					"dblayer_delete_database: skipping instance %s\n",inst->inst_name , 0, 0);	
			} else 
			{
				if (NULL == instance)
				{
					ret = _dblayer_delete_instance_dir(inst, 0 /* Do not start DB environment: traditional */);
				} else {
					ret = _dblayer_delete_instance_dir(inst, 1 /* Start DB environment: for FRI */);
				}
				if (ret != 0)
				{
					LDAPDebug(LDAP_DEBUG_ANY,
					"dblayer_delete_database: WARNING _dblayer_delete_instance_dir failed (%d)\n", ret, 0, 0);
					return ret;
				}	
			}
        }
    }

    /* changelog path is given; delete it, too. */
    if (cldir) {
        ret = _dblayer_delete_aux_dir(li, cldir);
        if (ret) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "dblayer_delete_database: failed to deelete \"%s\"\n",
                          chdir);
            return ret;
        }
    }

    /* now smash everything else in the db/ dir */
    dirhandle = PR_OpenDir(priv->dblayer_home_directory);
    if (! dirhandle)
    {
        LDAPDebug(LDAP_DEBUG_ANY, "PR_OpenDir (%s) failed (%d): %s\n", 
        priv->dblayer_home_directory,
        PR_GetError(),slapd_pr_strerror(PR_GetError()));
        return -1;
    }
    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT |
                                          PR_SKIP_DOT_DOT))) {
		int rval_tmp = 0;
        if (! direntry->name)
            break;

		PR_snprintf(filename, MAXPATHLEN, "%s/%s", priv->dblayer_home_directory, 
			direntry->name);

		/* Do not call PR_Delete on the instance directories if they exist.
		 * It would not work, but we still should not do it. */
        rval_tmp = PR_GetFileInfo(filename, &fileinfo);
        if (rval_tmp == PR_SUCCESS && fileinfo.type != PR_FILE_DIRECTORY)
		{
			/* Skip deleting log files; that should be handled below.
			 * (Note, we don't want to use "filename," because that is qualified and would
			 * not be compatibile with what dblayer_is_logfilename expects.) */
			if (!dblayer_is_logfilename(direntry->name))
			{       
				PR_Delete(filename);
			}
		}
    }

    PR_CloseDir(dirhandle);
    /* remove transaction logs */
    if ((NULL != priv->dblayer_log_directory) &&
        (0 != strlen(priv->dblayer_log_directory) )) 
    {
        log_dir = priv->dblayer_log_directory;
    }
    else
    {
        log_dir = dblayer_get_home_dir(li, NULL);
    }
	if (instance == NULL && log_dir && *log_dir)
	{
		ret = dblayer_delete_transaction_logs(log_dir);
		if(ret) {
		  LDAPDebug(LDAP_DEBUG_ANY,
		  "dblayer_delete_database: dblayer_delete_transaction_logs failed (%d)\n",
		  ret, 0, 0);
		  return -1;
		}
	}
    return 0;
}

/* delete an entire db/ directory, including all instances under it!
 * this is used mostly for restores.
 * dblayer is assumed to be closed.
 */
int dblayer_delete_database(struct ldbminfo *li)
{
	return dblayer_delete_database_ex(li, NULL, NULL);
}


/* 
 * Return the size of the database (in kilobytes).  XXXggood returning
 * the size in units of kb is really a hack, and is done because we
 * don't have NSPR support for 64-bit file offsets.
 * Caveats:
 * - We can still return incorrect results if an individual file is
 *   larger than fit in a PRUint32.
 * - PR_GetFileInfo doesn't do any special processing for symlinks,
 *   nor does it inform us if the file is a symlink.  Nice.  So if
 *   a file in the db directory is a symlink, the size we return
 *   will probably be way too small.
 */
int dblayer_database_size(struct ldbminfo *li, unsigned int *size)
{
     dblayer_private *priv = NULL;
    int return_value = 0;
    char filename[MAXPATHLEN];
    PRDir    *dirhandle = NULL;
    /*
     * XXXggood - NSPR will only give us an unsigned 32-bit quantity for
     * file sizes.  This is bad.  Files can be bigger than that these days.
     */
    unsigned int cumulative_size = 0;
    unsigned int remainder = 0;
    PRFileInfo info;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    dirhandle = PR_OpenDir(priv->dblayer_home_directory);
    if (NULL != dirhandle)
    {
        PRDirEntry *direntry = NULL;
        while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name)
            {
                break;
            }
            PR_snprintf(filename,MAXPATHLEN, "%s/%s",priv->dblayer_home_directory,direntry->name);
            return_value = PR_GetFileInfo(filename, &info);
            if (PR_SUCCESS == return_value)
            {
                cumulative_size += (info.size / 1024);
                remainder += (info.size % 1024);
            } else
            {
                cumulative_size = (PRUint32) 0;
                return_value = -1;
                break;
            }
        }
        PR_CloseDir(dirhandle);
    } else
    {
        return_value = -1;
    }

    *size = cumulative_size + (remainder / 1024);
    return return_value;
}


static int count_dbfiles_in_dir(char *directory, int *count, int recurse)
{
    /* The new recurse argument was added to help with multiple backend
     * instances.  When recurse is true, this function will also look through
     * the directories in the given directory for .db3 files. */
    int return_value  = 0;
    PRDir *dirhandle = NULL;

    if (!recurse) {
        /* It is really the callers responsibility to set count to 0 before
         * calling.  However, if recurse isn't true, we can make sure it is
         * set to 0. */
        *count = 0;
    }
    dirhandle = PR_OpenDir(directory);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        char *direntry_name;
        PRFileInfo info;

        while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
            if (NULL == direntry->name) {
                break;
            }
            direntry_name = PR_smprintf("%s/%s", directory, direntry->name);
            if ((PR_GetFileInfo(direntry_name, &info) == PR_SUCCESS) &&
                (PR_FILE_DIRECTORY == info.type) && recurse) {
                /* Recurse into this directory but not any further.  This is
                 * because each instance gets its own directory, but in those
                 * directories there should be only .db3 files.  There should
                 * not be any more directories in an instance directory. */
                count_dbfiles_in_dir(direntry_name, count, 0 /* don't recurse */);
            }
			if (direntry_name) {
				PR_smprintf_free(direntry_name);
			}
            /* PL_strcmp takes NULL arg */
            if (PL_strcmp(LDBM_FILENAME_SUFFIX , strrchr(direntry->name, '.'))
                == 0) {
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
dblayer_copyfile(char *source, char *destination, int overwrite, int mode) 
{
#if defined _WIN32
    return (0 == CopyFile(source,destination,overwrite ? FALSE : TRUE));
#else
#ifdef DB_USE_64LFS
#define OPEN_FUNCTION dblayer_open_large
#else
#define OPEN_FUNCTION open    
#endif
    int source_fd = -1;
    int dest_fd = -1;
    char *buffer = NULL;
    int return_value = -1;
    int bytes_to_write = 0;

    /* malloc the buffer */
    buffer = slapi_ch_malloc(64*1024);
    if (NULL == buffer)
    {
        goto error;
    }
    /* Open source file */
    source_fd = OPEN_FUNCTION(source,O_RDONLY,0);
    if (-1 == source_fd)
    {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "dblayer_copyfile: failed to open source file: %s\n",
                      source);
        goto error;
    }
    /* Open destination file */
    dest_fd = OPEN_FUNCTION(destination,O_CREAT | O_WRONLY, mode);
    if (-1 == dest_fd)
    {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "dblayer_copyfile: failed to open dest file: %s\n",
                      destination);
        goto error;
    }
    LDAPDebug2Args(LDAP_DEBUG_BACKLDBM,
                   "Copying %s to %s\n", source, destination);
    /* Loop round reading data and writing it */
    while (1)
    {
        return_value = read(source_fd,buffer,64*1024);
        if (return_value <= 0)
        {
            /* means error or EOF */
            if (return_value < 0)
            {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "dblayer_copyfile: failed to read: %d\n", errno);
            }
            break;
        }
        bytes_to_write = return_value;
        return_value = write(dest_fd,buffer,bytes_to_write);
        if (return_value != bytes_to_write)
        {
            /* means error */
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "dblayer_copyfile: failed to write: %d\n", errno);
            return_value = -1;
            break;
        }
    }
error:
    if (source_fd != -1)
    {
        close(source_fd);
    }
    if (dest_fd != -1)
    {
        close(dest_fd);
    }
    slapi_ch_free((void**)&buffer);
    return return_value;
#endif
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
 * DBDB added resetlsns arg which is used in partial restore (because the LSNs need to be reset to avoid
 * confusing transaction logging code).
 */
int
dblayer_copy_directory(struct ldbminfo *li,
                       Slapi_Task *task,
                       char *src_dir, 
                       char *dest_dir,
                       int restore,
                       int *cnt,
                       int instance_dir_flag,
                       int indexonly,
                       int resetlsns)
{
    dblayer_private *priv = NULL;
    char            *new_src_dir = NULL;
    char            *new_dest_dir = NULL;
    PRDir           *dirhandle = NULL;
    PRDirEntry      *direntry = NULL;
    size_t          filename_length = 0;
    size_t          offset = 0;
    char            *compare_piece = NULL;
    char            *filename1;
    char            *filename2;
    int             return_value = -1;
    char            *relative_instance_name = NULL;
    char            *inst_dirp = NULL;
    char            inst_dir[MAXPATHLEN];
    char            sep;
    int             suffix_len = 0;

    if (!src_dir || '\0' == *src_dir)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY, 
                       "dblayer_copy_directory: src_dir is empty\n");
        return return_value;
    }
    if (!dest_dir || '\0' == *dest_dir)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY, 
                       "dblayer_copy_directory: dest_dir is empty\n");
        return return_value;
    }

    priv = (dblayer_private *) li->li_dblayer_private;

    /* get the backend instance name */
    sep = get_sep(src_dir);
    if ((relative_instance_name = strrchr(src_dir, sep)) == NULL)
        relative_instance_name = src_dir;
    else
        relative_instance_name++;

    if (is_fullpath(src_dir))
    {
        new_src_dir = src_dir;
    }
    else
    {
        int len;
        ldbm_instance *inst =
                       ldbm_instance_find_by_name(li, relative_instance_name);
        if (NULL == inst)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "Backend instance \"%s\" does not exist; "
                  "Instance path %s could be invalid.\n",
                  relative_instance_name, src_dir, 0);
            return return_value;
        }

        inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                              inst_dir, MAXPATHLEN);
        if (!inst_dirp || !*inst_dirp)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "Instance dir is NULL.\n", 0, 0, 0);
            return return_value;
        }
        len = strlen(inst_dirp);
        sep = get_sep(inst_dirp);
        if (*(inst_dirp+len-1) == sep)
            sep = '\0';
        new_src_dir = inst_dirp;
    }

    dirhandle = PR_OpenDir(new_src_dir);
    if (NULL == dirhandle)
    {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, 
                      "dblayer_copy_directory: failed to open dir %s\n",
                      new_src_dir);

        return return_value;
    }

    suffix_len = sizeof(LDBM_SUFFIX) - 1;
    while (NULL != (direntry =
                    PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
    {
        if (NULL == direntry->name) {
            /* NSPR doesn't behave like the docs say it should */
            break;
        }
        if (indexonly &&
            0 == strcmp(direntry->name, ID2ENTRY LDBM_FILENAME_SUFFIX))
        {
            continue;
        }

        /* Look at the last three characters in the filename */
        filename_length = strlen(direntry->name);
        if (filename_length > suffix_len) {
            offset = filename_length - suffix_len;
        } else {
            offset = 0;
        }
        compare_piece = (char *)direntry->name + offset;

        /* rename .db3 -> .db4 or .db4 -> .db */
        if (0 == strcmp(compare_piece, LDBM_FILENAME_SUFFIX) ||
            0 == strcmp(compare_piece, LDBM_SUFFIX_OLD) ||
            0 == strcmp(direntry->name, DBVERSION_FILENAME)) {
            /* Found a database file.  Copy it. */

            if (NULL == new_dest_dir) {
                /* Need to create the new directory where the files will be
                 * copied to. */
                PRFileInfo info;
                char *prefix = "";
                char mysep = 0;

                if (!is_fullpath(dest_dir))
                {
                    prefix = dblayer_get_home_dir(li, NULL);
                    if (!prefix || !*prefix)
                    {
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
                if (PR_SUCCESS == PR_GetFileInfo(new_dest_dir, &info))
                {
                    ldbm_delete_dirs(new_dest_dir);
                }
                if (mkdir_p(new_dest_dir, 0700) != PR_SUCCESS)
                {
                    LDAPDebug(LDAP_DEBUG_ANY, "Can't create new directory %s, "
                        SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                        new_dest_dir, PR_GetError(),
                        slapd_pr_strerror(PR_GetError()));
                    goto out;
                }
            }

            filename1 = slapi_ch_smprintf("%s/%s", new_src_dir, direntry->name);
            filename2 = slapi_ch_smprintf("%s/%s", new_dest_dir, direntry->name);

            if (restore) {
                LDAPDebug(LDAP_DEBUG_ANY, "Restoring file %d (%s)\n",
                          *cnt, filename2, 0);
                if (task) {
                    slapi_task_log_notice(task,
                        "Restoring file %d (%s)", *cnt, filename2);
                    slapi_task_log_status(task,
                        "Restoring file %d (%s)", *cnt, filename2);
                }
            } else {
                LDAPDebug(LDAP_DEBUG_ANY, "Backing up file %d (%s)\n",
                          *cnt, filename2, 0);
                if (task) {
                    slapi_task_log_notice(task,
                        "Backing up file %d (%s)", *cnt, filename2);
                    slapi_task_log_status(task,
                        "Backing up file %d (%s)", *cnt, filename2);
                }
            }

            /* copy filename1 to filename2 */
            /* If the file is a database file, and resetlsns is set, then we need to do a key by key copy */
            /* PL_strcmp takes NULL arg */
            if (resetlsns &&
                (PL_strcmp(LDBM_FILENAME_SUFFIX, strrchr(filename1, '.'))
                 == 0)) {
                return_value = dblayer_copy_file_resetlsns(src_dir, filename1, filename2,
                                            0, priv);
            } else {
                return_value = dblayer_copyfile(filename1, filename2,
                                            0, priv->dblayer_file_mode);
            }
            slapi_ch_free((void**)&filename1);
            slapi_ch_free((void**)&filename2);
            if (0 > return_value)
                break;

            (*cnt)++;
        }
    }
out:
    PR_CloseDir(dirhandle);
    slapi_ch_free_string(&new_dest_dir);
    if ((new_src_dir != src_dir) && (new_src_dir != inst_dir))
    {
        slapi_ch_free_string(&new_src_dir);
    }
    return return_value;
}

/*
 * Get changelogdir from cn=changelog5,cn=config
 * The value does not have trailing spaces nor slashes.
 */
static int
_dblayer_get_changelogdir(struct ldbminfo *li, char **changelogdir)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Attr *attr = NULL;
    Slapi_Value *v = NULL;
    const char *s = NULL;
    char *attrs[2];
    int rc = -1;

    if (NULL == li || NULL == changelogdir) {
        LDAPDebug2Args(LDAP_DEBUG_ANY, 
                       "ERROR: _dblayer_get_changelogdir: Invalid arg: "
                       "li: 0x%x, changelogdir: 0x%x\n", li, changelogdir);
        return rc;
    }
    *changelogdir = NULL;

    pb = slapi_pblock_new();
    attrs[0] = CHANGELOGDIRATTR;
    attrs[1] = NULL;
    slapi_search_internal_set_pb(pb, CHANGELOGENTRY,
                                 LDAP_SCOPE_BASE, "cn=*", attrs, 0, NULL, NULL,
                                 li->li_identity, 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    
    if (LDAP_NO_SUCH_OBJECT == rc) {
        /* No changelog; Most likely standalone or not a master. */
        rc = LDAP_SUCCESS;
        goto bail;
    }
    if (LDAP_SUCCESS != rc) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, 
                      "ERROR: Failed to search \"%s\"\n", CHANGELOGENTRY);
        goto bail;
    }
    /* rc == LDAP_SUCCESS */
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (NULL == entries) {
        /* No changelog */
        goto bail;
    }
    /* There should be only one entry. */
    rc = slapi_entry_attr_find(entries[0], CHANGELOGDIRATTR, &attr);
    if (rc || NULL == attr) {
        /* No changelog dir */
        rc = LDAP_SUCCESS;
        goto bail;
    }
    rc = slapi_attr_first_value(attr, &v);
    if (rc || NULL == v) {
        /* No changelog dir */
        rc = LDAP_SUCCESS;
        goto bail;
    }
    rc = LDAP_SUCCESS;
    s = slapi_value_get_string(v);
    if (NULL == s) {
        /* No changelog dir */
        goto bail;
    }
    *changelogdir = slapi_ch_strdup(s);
    /* Remove trailing spaces and '/' if any */
    normalize_dir(*changelogdir);
bail:
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    return rc;
}

/* Destination Directory is an absolute pathname */
int
dblayer_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task)
{
    dblayer_private *priv = NULL;
    char **listA = NULL, **listB = NULL, **listi, **listj, *prefix;
    char *home_dir = NULL;
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
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    home_dir = dblayer_get_home_dir(li, NULL);
    if (NULL == home_dir || '\0' == *home_dir)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY, 
                       "Backup: missing db home directory info\n");
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
    dblayer_force_checkpoint(li);
    dblayer_txn_init(li,&txn);
    return_value = dblayer_txn_begin_all(li, NULL, &txn);
    if (return_value) {
        LDAPDebug0Args(LDAP_DEBUG_ANY, 
                       "Backup: transaction error\n");
        return return_value;
    }

    if ( g_get_shutdown() || c_get_shutdown() ) {
        LDAPDebug0Args(LDAP_DEBUG_ANY, "Backup aborted\n");
        return_value = -1;
        goto bail;
    }

    /* repeat this until the logfile sets match... */
    do {
        /* get the list of logfiles currently existing */
        if (priv->dblayer_enable_transactions) {
            return_value = LOG_ARCHIVE(priv->dblayer_env->dblayer_DB_ENV,
                &listA, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listA == NULL)) {
                LDAPDebug0Args(LDAP_DEBUG_ANY,
                               "Backup: log archive error\n");
                if (task) {
                    slapi_task_log_notice(task, "Backup: log archive error\n");
                }
                return_value = -1;
                goto bail;
            }
        } else {
            ok=1;
        }
        if ( g_get_shutdown() || c_get_shutdown() ) {
            LDAPDebug0Args(LDAP_DEBUG_ANY, "Backup aborted\n");
            return_value = -1;
            goto bail;
        }

        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
        {
            ldbm_instance *inst = (ldbm_instance *)object_get_data(inst_obj);
            inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                  inst_dir, MAXPATHLEN);
            if ((NULL == inst_dirp) || ('\0' == *inst_dirp)) {
                LDAPDebug0Args(LDAP_DEBUG_ANY, 
                               "Backup: Instance dir is empty\n");
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backup: Instance dir is empty\n");
                }
                return_value = -1;
                goto bail;
            }
            return_value = dblayer_copy_directory(li, task, inst_dirp,
                                                  dest_dir, 0 /* backup */,
                                                  &cnt, 0, 0, 0);
            if (return_value) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "Backup: error in copying directory "
                          "(%s -> %s): err=%d\n",
                          inst_dirp, dest_dir, return_value);
                if (task) {
                    slapi_task_log_notice(task,
                          "Backup: error in copying directory "
                          "(%s -> %s): err=%d\n",
                          inst_dirp, dest_dir, return_value);
                }
                if (inst_dirp != inst_dir)
                    slapi_ch_free_string(&inst_dirp);
                goto bail;
            }
            if (inst_dirp != inst_dir)
                slapi_ch_free_string(&inst_dirp);
        }
        /* Get changelogdir, if any */
        _dblayer_get_changelogdir(li, &changelogdir);
        if (changelogdir) {
            /* dest dir for changelog: dest_dir/repl_changelog_backup  */
            char *changelog_destdir = slapi_ch_smprintf("%s/%s",
                                                 dest_dir, CHANGELOG_BACKUPDIR);
            return_value = dblayer_copy_directory(li, task, changelogdir,
                                                  changelog_destdir,
                                                  0 /* backup */,
                                                  &cnt, 0, 0, 0);
            if (return_value) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "Backup: error in copying directory "
                          "(%s -> %s): err=%d\n",
                          changelogdir, changelog_destdir, return_value);
                if (task) {
                    slapi_task_log_notice(task,
                          "Backup: error in copying directory "
                          "(%s -> %s): err=%d\n",
                          changelogdir, changelog_destdir, return_value);
                }
                slapi_ch_free_string(&changelog_destdir);
                goto bail;
            }
            /* Copy DBVERSION */
            pathname1 = slapi_ch_smprintf("%s/%s",
                                         changelogdir, DBVERSION_FILENAME);
            pathname2 = slapi_ch_smprintf("%s/%s",
                                         changelog_destdir, DBVERSION_FILENAME);
            return_value = dblayer_copyfile(pathname1, pathname2,
                                            0, priv->dblayer_file_mode);
            slapi_ch_free_string(&pathname1);
            slapi_ch_free_string(&pathname2);
            slapi_ch_free_string(&changelog_destdir);
        }
        if (priv->dblayer_enable_transactions) {
            /* now, get the list of logfiles that still exist */
            return_value = LOG_ARCHIVE(priv->dblayer_env->dblayer_DB_ENV,
                &listB, DB_ARCH_LOG, (void *)slapi_ch_malloc);
            if (return_value || (listB == NULL)) {
                LDAPDebug0Args(LDAP_DEBUG_ANY,
                               "Backup: can't get list of logs\n");
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
                if (! found) {
                    ok = 0;     /* missing log: start over */
                    LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                  "WARNING: Log %s has been swiped "
                                  "out from under me! (retrying)\n", *listi);
                    if (task) {
                        slapi_task_log_notice(task,
                          "WARNING: Log %s has been swiped out from under me! "
                          "(retrying)", *listi);
                    }
                }
            }
            
            if ( g_get_shutdown() || c_get_shutdown() ) {
                LDAPDebug0Args(LDAP_DEBUG_ANY, "Backup aborted\n");
                return_value = -1;
                goto bail;
            }

            if (ok) {
                size_t p1len, p2len;
                char **listptr;
                
                prefix = NULL;
                if ((NULL != priv->dblayer_log_directory) && 
                    (0 != strlen(priv->dblayer_log_directory))) {
                    prefix = priv->dblayer_log_directory;
                } else {
                    prefix = home_dir;
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
                    LDAPDebug2Args(LDAP_DEBUG_ANY, "Backing up file %d (%s)\n",
                        cnt, pathname2);
                    if (task)
                    {
                        slapi_task_log_notice(task,
                                     "Backing up file %d (%s)", cnt, pathname2);
                        slapi_task_log_status(task,
                                     "Backing up file %d (%s)", cnt, pathname2);
                    }
                    return_value = dblayer_copyfile(pathname1, pathname2,
                        0, priv->dblayer_file_mode);
                    if (0 > return_value) {
                        LDAPDebug2Args(LDAP_DEBUG_ANY, "Backup: error in "
                            "copying file '%s' (err=%d) -- Starting over...\n",
                            pathname1, return_value);
                        if (task) {
                            slapi_task_log_notice(task,
                                "Error copying file '%s' (err=%d) -- Starting "
                                "over...", pathname1, return_value);
                        }
                        ok = 0;
                    }
                    if ( g_get_shutdown() || c_get_shutdown() ) {
                        LDAPDebug0Args(LDAP_DEBUG_ANY, "Backup aborted\n");
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
    LDAPDebug2Args(LDAP_DEBUG_ANY, "Backing up file %d (%s)\n", cnt, pathname2);
    if (task) {
        slapi_task_log_notice(task, "Backing up file %d (%s)", cnt, pathname2);
        slapi_task_log_status(task, "Backing up file %d (%s)", cnt, pathname2);
    }
    return_value =
             dblayer_copyfile(pathname1, pathname2, 0, priv->dblayer_file_mode);
    if (return_value) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Backup: error in copying version file "
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

    if (0 == return_value)  /* if everything went well, backup the index conf */
        return_value = dse_conf_backup(li, dest_dir);
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

static int dblayer_is_logfilename(const char* path)
{
    int ret = 0;
    /* Is the filename at least 4 characters long ? */
    if (strlen(path) < 4) 
    {
        return 0; /* Not a log file then */
    }
    /* Are the first 4 characters "log." ? */
    ret = strncmp(path,"log.",4);
    if (0 == ret) 
    {
        /* Now, are the last 4 characters _not_ .db# ? */
        const char *piece = path + (strlen(path) - 4);
        ret = strcmp(piece,LDBM_FILENAME_SUFFIX);
        if (0 != ret) 
        {
            /* Is */
            return 1;
        }
    }
    return 0; /* Is not */
}

/* remove log.xxx from log directory*/
static
int dblayer_delete_transaction_logs(const char * log_dir)
{
    int rc=0;
    char filename1[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    dirhandle = PR_OpenDir(log_dir);
    if (NULL != dirhandle) {
        PRDirEntry *direntry = NULL;
        int is_a_logfile = 0;
        int pre=0; 
        PRFileInfo info ;
            
        while (NULL != (direntry =
                        PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
        {
            if (NULL == direntry->name) {
                /* NSPR doesn't behave like the docs say it should */
                LDAPDebug(LDAP_DEBUG_ANY, "PR_ReadDir failed (%d): %s\n", 
                PR_GetError(),slapd_pr_strerror(PR_GetError()), 0);
                break;
            }
            PR_snprintf(filename1, MAXPATHLEN, "%s/%s", log_dir, direntry->name);
            pre = PR_GetFileInfo(filename1, &info);
            if (pre == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) {
                continue;
            }
            is_a_logfile = dblayer_is_logfilename(direntry->name);
            if (is_a_logfile && (NULL != log_dir) && (0 != strlen(log_dir)) )
            {
                LDAPDebug(LDAP_DEBUG_ANY, "Deleting log file: (%s)\n",
                          filename1, 0, 0);
                unlink(filename1);
            }
        }
        PR_CloseDir(dirhandle);
    }
    else if (PR_FILE_NOT_FOUND_ERROR != PR_GetError())
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "dblayer_delete_transaction_logs: PR_OpenDir(%s) failed (%d): %s\n",
             log_dir, PR_GetError(),slapd_pr_strerror(PR_GetError()));
        rc=1;
    }
    return rc;
}

const char *skip_list[] =
{
    ".ldif",
    NULL
};

static int doskip(const char *filename)
{
    const char **p;
    int len = strlen(filename);

    for (p = skip_list; p && *p; p++)
    {
        int n = strlen(*p);
        if (0 == strncmp(filename + len - n, *p, n))
            return 1;
    }
    return 0;
}

static int dblayer_copy_dirand_contents(char* src_dir, char* dst_dir, int mode, Slapi_Task *task)
{
  int return_value  = 0;
  int tmp_rval;
  char filename1[MAXPATHLEN];
  char filename2[MAXPATHLEN];
  PRDir *dirhandle = NULL;
  PRDirEntry *direntry = NULL;
  PRFileInfo info;

  dirhandle = PR_OpenDir(src_dir);
  if (NULL != dirhandle) 
  {

	while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
	{
      if (NULL == direntry->name) {
         /* NSPR doesn't behave like the docs say it should */
        break;
      }


       PR_snprintf(filename1, MAXPATHLEN, "%s/%s", src_dir, direntry->name);
       PR_snprintf(filename2, MAXPATHLEN, "%s/%s", dst_dir, direntry->name);
       LDAPDebug(LDAP_DEBUG_ANY, "Moving file %s\n",
                          filename2, 0, 0);
      /* Is this entry a directory? */
      tmp_rval = PR_GetFileInfo(filename1, &info);
      if (tmp_rval == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) 
	  {
		 PR_MkDir(filename2,NEWDIR_MODE);
		   return_value = dblayer_copy_dirand_contents(filename1, filename2,
                                               mode,task);
			if (return_value) 
			{
				if (task) 
				{
					slapi_task_log_notice(task,
					"Failed to copy directory %s", filename1);
				}
				break;
			}
	   } else {
			if (task) 
			{
				slapi_task_log_notice(task, "Moving file %s",
                                          filename2);
				slapi_task_log_status(task, "Moving file %s",
                                          filename2);
			}
			return_value = dblayer_copyfile(filename1, filename2, 0,
                                               mode);
	   }
       if (0 > return_value)
         break;
    }
    PR_CloseDir(dirhandle);
  }
  return return_value;
}

static int dblayer_fri_trim(char *fri_dir_path, char* bename)
{
	int retval = 0;
	int tmp_rval;
	char filename[MAXPATHLEN];
	PRDir *dirhandle = NULL;
	PRDirEntry *direntry = NULL;
	PRFileInfo info;

	dirhandle = PR_OpenDir(fri_dir_path);
	if (NULL != dirhandle) 
	{

		while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
		{
			if (NULL == direntry->name) {
			/* NSPR doesn't behave like the docs say it should */
			break;
			}

			PR_snprintf(filename, MAXPATHLEN, "%s/%s", fri_dir_path, direntry->name);

			/* Is this entry a directory? */
			tmp_rval = PR_GetFileInfo(filename, &info);
			if (tmp_rval == PR_SUCCESS && PR_FILE_DIRECTORY == info.type)
			{
				if(strcmp(direntry->name,bename)!=0)
				{
					LDAPDebug(LDAP_DEBUG_ANY, "Removing file %s from staging area\n",
                         filename, 0, 0);
					ldbm_delete_dirs(filename);
				}
				continue;
			}

			if ((strcmp(direntry->name,"DBVERSION") == 0)||
				    (strncmp(direntry->name,"__",2) == 0)||
					(strncmp(direntry->name,"log",3) == 0)){
				LDAPDebug(LDAP_DEBUG_ANY, "Removing file %s from staging area\n",
                         filename, 0, 0);
				PR_Delete(filename);
			}

		}
	}
	PR_CloseDir(dirhandle);
	return retval;
}

/* Recover a stand-alone environment , used in filesystem replica intialization restore */
static int dblayer_recover_environment_path(char *dbhome_dir, dblayer_private *priv)
{
	int retval = 0;
	DB_ENV *env = NULL;
	/* Make an environment for recovery */
	retval = dblayer_make_private_recovery_env(dbhome_dir, priv, &env);
	if (retval) {
		goto error;
	}
	if (env) {
		retval = env->close(env,0);
		if (retval) {
		}
	}
error:
	return retval;
}


static int dblayer_fri_restore(char *home_dir, char *src_dir, dblayer_private *priv, Slapi_Task *task, char** new_src_dir, char* bename)
{
		int retval = 0;
		char *fribak_dir_path = NULL;
		char *fribak_dir_name = "fribak";
		int mode = priv->dblayer_file_mode;

		*new_src_dir = NULL;


		/* First create the recovery directory */
		fribak_dir_path = slapi_ch_smprintf("%s/../%s",home_dir,fribak_dir_name);
		if((-1 == PR_MkDir(fribak_dir_path,NEWDIR_MODE)))
		{
		  LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: %s exists\n",fribak_dir_path, 0, 0);
		  LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Removing %s.\n",fribak_dir_path, 0, 0);
		  retval = ldbm_delete_dirs(fribak_dir_path);
		  if (retval)
		  {
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Removal of %s failed!\n", fribak_dir_path, 0, 0);
			goto error;
		  }
		  PR_MkDir(fribak_dir_path,NEWDIR_MODE);
		  if (retval != PR_SUCCESS)
		  {
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Creation of %s failed!\n", fribak_dir_path, 0, 0);
			goto error;
		  }
		}
		/* Next copy over the entire backup file set to the recovery directory */
		/* We do this because we want to run recovery there, and we need all the files for that */
		retval = dblayer_copy_dirand_contents(src_dir, fribak_dir_path, mode, task);
		if (retval) 
		{
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Copy contents to %s failed!\n", fribak_dir_path, 0, 0);
			goto error;
		}
		/* Next, run recovery on the files */
		retval = dblayer_recover_environment_path(fribak_dir_path, priv);
		if (retval) 
		{
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Recovery failed!\n", 0, 0, 0);
			goto error;
		}
		/* Files nicely recovered, next we stip out what we don't need from the backup set */
		retval = dblayer_fri_trim(fribak_dir_path,bename);
		if (retval) 
		{
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_fri_restore: Trim failed!\n", 0, 0, 0);
			goto error;
		}
		*new_src_dir = fribak_dir_path;
	error:
		return retval;
}

/* Destination Directory is an absolute pathname */

int dblayer_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task, char *bename)
{
    dblayer_private *priv = NULL;
    int return_value = 0;
    int tmp_rval;
    char filename1[MAXPATHLEN];
    char filename2[MAXPATHLEN];
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    PRFileInfo info;
    ldbm_instance *inst = NULL;
    int seen_logfiles = 0;      /* Tells us if we restored any logfiles */
    int is_a_logfile = 0;
    int dbmode;
    int action = 0;
    char *home_dir = NULL;
    char *real_src_dir = NULL;
    int frirestore = 0; /* Is a an FRI/single instance restore. 0 for no, 1 for yes */
    struct stat sbuf;
    char *changelogdir = NULL;
    char *restore_dir = NULL;
    char *prefix = NULL;
    int cnt = 1;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private*)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    /* DBDB this is a hack, take out later */
    PR_Lock(li->li_config_mutex);
    /* dblayer_home_directory is freed in dblayer_post_close.
     * li_directory needs to live beyond dblayer. */
    priv->dblayer_home_directory = slapi_ch_strdup(li->li_directory);
    priv->dblayer_cachesize = li->li_dbcachesize;
    priv->dblayer_ncache = li->li_dbncache;
    priv->dblayer_file_mode = li->li_mode;
    PR_Unlock(li->li_config_mutex);

    home_dir = dblayer_get_home_dir(li, NULL);
    
    if (NULL == home_dir || '\0' == *home_dir)
    {
        LDAPDebug0Args(LDAP_DEBUG_ANY, 
                       "Restore: missing db home directory info\n");
        return -1;
    }

    /* We find out if slapd is running */
    /* If it is, we fail */
    /* We check on the source staging area, no point in going further if it
     * isn't there */
    if (stat(src_dir, &sbuf) < 0) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "Restore: backup directory %s does not "
                      "exist.\n", src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s does not "
                      "exist.\n", src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    } else if (!S_ISDIR(sbuf.st_mode)) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "Restore: backup directory %s is not "
                      "a directory.\n", src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s is not "
                      "a directory.\n", src_dir);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (!dbversion_exists(li, src_dir)) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "Restore: backup directory %s does not "
                      "contain a complete backup\n", src_dir);
        if (task) {
            slapi_task_log_notice(task, "Restore: backup directory %s does not "
                                  "contain a complete backup", src_dir );
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* If this is a FRI restore, the bename will be non-NULL */
    if (bename != NULL)
        frirestore = 1;

    /*
     * Check if the target is a superset of the backup.
     * If not don't restore any db at all, otherwise
     * the target will be crippled.
     */
    dirhandle = PR_OpenDir(src_dir);
    if (NULL != dirhandle)
    {
        while ((direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))
            && direntry->name)
        {
            PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                                                      src_dir, direntry->name);
            if(!frirestore || strcmp(direntry->name,bename)==0)
            {
                tmp_rval = PR_GetFileInfo(filename1, &info);
                if (tmp_rval == PR_SUCCESS && PR_FILE_DIRECTORY == info.type) {
                    /* Is it CHANGELOG_BACKUPDIR? */
                    if (0 == strcmp(CHANGELOG_BACKUPDIR, direntry->name)) {
                        /* Yes, this is a changelog backup. */
                        /* Get the changelog path */
                        _dblayer_get_changelogdir(li, &changelogdir);
                        continue;
                    }
                    inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
                    if ( inst == NULL)
                    {
                        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                "Restore: target server has no %s configured\n",
                                direntry->name);
                        if (task) {
                            slapi_task_log_notice(task,
                                "Restore: target server has no %s configured\n",
                                direntry->name);
                        }
                        PR_CloseDir(dirhandle);
                        return_value = LDAP_UNWILLING_TO_PERFORM;
                        goto error_out;
                    }

                    if (slapd_comp_path(src_dir, inst->inst_parent_dir_name)
                        == 0) {
                        LDAPDebug2Args(LDAP_DEBUG_ANY,
                                "Restore: backup dir %s and target dir %s "
                                "are identical\n",
                                src_dir, inst->inst_parent_dir_name);
                        if (task) {
                            slapi_task_log_notice(task,
                                "Restore: backup dir %s and target dir %s "
                                "are identical\n",
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
    return_value = dblayer_delete_database_ex(li, bename, changelogdir);
    if (return_value) {
        goto error_out;
    }

    if (frirestore) /*if we are restoring a single backend*/
    {
        char *new_src_dir = NULL;
        return_value = dblayer_fri_restore(home_dir,src_dir,priv,task,&new_src_dir,bename);
        if (return_value) {
            goto error_out;
        }
        /* Now modify the src_dir to point to our recovery area and carry on as if nothing had happened... */
        real_src_dir = new_src_dir;
    } else 
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
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
            "Restore: failed to open the directory \"%s\"\n", real_src_dir);
        if (task) {
            slapi_task_log_notice(task,
                "Restore: failed to open the directory \"%s\"\n", real_src_dir);
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
        tmp_rval = PR_GetFileInfo(filename1, &info);
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
                        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "Restore: broken changelog dir path %s\n",
                              changelogdir);
                        if (task) {
                            slapi_task_log_notice(task,
                                 "Restore: broken changelog dir path %s\n",
                                 changelogdir);
                        }
                        goto error_out;
                    }
                    PR_snprintf(p, sizeof(filename1) - (p - filename1),
                                    "/%s", cldirname+1);
                    /* Get the parent dir of changelogdir */
                    *cldirname = '\0';
                    return_value = dblayer_copy_directory(li, task, filename1,
                                  changelogdir, 1 /* restore */, &cnt, 0, 0, 0);
                    *cldirname = '/';
                    if (return_value) {
                        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "Restore: failed to copy directory %s\n",
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
                    return_value = dblayer_copyfile(filename1, filename2,
                                                    0, priv->dblayer_file_mode);
                }
                continue;
            }

            inst = ldbm_instance_find_by_name(li, (char *)direntry->name);
            if (inst == NULL)
                continue;

            restore_dir = inst->inst_parent_dir_name;
            /* If we're doing a partial restore, we need to reset the LSNs on the data files */
            if (dblayer_copy_directory(li, task, filename1,
                restore_dir, 1 /* restore */, &cnt, 0, 0, (bename) ? 1 : 0) == 0)
                continue;
            else
            {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "Restore: failed to copy directory %s\n",
                              filename1);
                if (task) {
                    slapi_task_log_notice(task,
                        "Restore: failed to copy directory %s", filename1);
                }
                goto error_out;
            }
        }

        if (doskip(direntry->name))
            continue;

        /* Is this a log file ? */
        /* Log files have names of the form "log.xxxxx" */
        /* We detect these by looking for the prefix "log." and
         * the lack of the ".db#" suffix */
        is_a_logfile = dblayer_is_logfilename(direntry->name);
        if (is_a_logfile) {
            seen_logfiles = 1;
        }
        if (is_a_logfile && (NULL != priv->dblayer_log_directory) &&
            (0 != strlen(priv->dblayer_log_directory)) ) {
            prefix = priv->dblayer_log_directory;
        } else {
            prefix = home_dir;
        }
        mkdir_p(prefix, 0700);
        PR_snprintf(filename1, sizeof(filename1), "%s/%s",
                                                  real_src_dir, direntry->name);
        PR_snprintf(filename2, sizeof(filename2), "%s/%s",
                                                  prefix, direntry->name);
        LDAPDebug2Args(LDAP_DEBUG_ANY, "Restoring file %d (%s)\n",
                       cnt, filename2);
        if (task) {
            slapi_task_log_notice(task, "Restoring file %d (%s)",
                                  cnt, filename2);
            slapi_task_log_status(task, "Restoring file %d (%s)",
                                  cnt, filename2);
        }
        return_value = dblayer_copyfile(filename1, filename2, 0,
                                        priv->dblayer_file_mode);
        if (0 > return_value) {
            goto error_out;
        }
        cnt++;
    }
    PR_CloseDir(dirhandle);

    /* We're done ! */

    /* [605024] check the DBVERSION and reset idl-switch if needed */
    if (dbversion_exists(li, home_dir))
    {
        char *ldbmversion = NULL;
        char *dataversion = NULL;

        if (dbversion_read(li, home_dir, &ldbmversion, &dataversion) != 0)
        {
            LDAPDebug1Arg(LDAP_DEBUG_ANY, "Warning: Unable to read dbversion "
                          "file in %s\n", home_dir);
        }
        else
        {
            adjust_idl_switch(ldbmversion, li);
            slapi_ch_free_string(&ldbmversion);
            slapi_ch_free_string(&ldbmversion);
        }
    }

    return_value = check_db_version(li, &action);
    if (action &
        (DBVERSION_UPGRADE_3_4|DBVERSION_UPGRADE_4_4|DBVERSION_UPGRADE_4_5))
    {
        dbmode = DBLAYER_CLEAN_RECOVER_MODE;/* upgrade: remove logs & recover */
    }
    else if (seen_logfiles)
    {
        dbmode = DBLAYER_RESTORE_MODE;
    }
    else if (action & DBVERSION_NEED_DN2RDN)
    {
        LDAPDebug2Args(LDAP_DEBUG_ANY, 
            "%s is on, while the instance %s is in the DN format. "
            "Please run dn2rdn to convert the database format.\n",
            CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    }
    else if (action & DBVERSION_NEED_RDN2DN)
    {
        LDAPDebug2Args(LDAP_DEBUG_ANY, 
            "%s is off, while the instance %s is in the RDN format. "
            "Please change the value to on in dse.ldif.\n",
            CONFIG_ENTRYRDN_SWITCH, inst->inst_name);
        return_value = -1;
        goto error_out;
    }
    else 
    {
        dbmode = DBLAYER_RESTORE_NO_RECOVERY_MODE;
    }

    /* now start the database code up, to prevent recovery next time the 
     * server starts;
     * dse_conf_verify may need to have db started, as well. */
    /* If no logfiles were stored, then fatal recovery isn't required */

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE)
    {
        /* command line mode; no need to run db threads */
        dbmode |= DBLAYER_NO_DBTHREADS_MODE;
    }
    else /* on-line mode */
    {
        allinstance_set_not_busy(li);
    }

    tmp_rval = dblayer_start(li, dbmode);
    if (0 != tmp_rval) {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
                       "Restore: failed to init database\n");
        if (task) {
            slapi_task_log_notice(task, "Restore: failed to init database");
        }
        return_value = tmp_rval;
        goto error_out;
    }

    if (0 == return_value) { /* only when the copyfile succeeded */
        /* check the DSE_* files, if any */
        tmp_rval = dse_conf_verify(li, real_src_dir, bename);
        if (0 != tmp_rval)
            LDAPDebug0Args(LDAP_DEBUG_ANY,
                        "Warning: Unable to verify the index configuration\n");
    }

    if (li->li_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* command line: close the database down again */
        tmp_rval = dblayer_close(li, dbmode);
        if (0 != tmp_rval) {
            LDAPDebug0Args(LDAP_DEBUG_ANY,
                           "Restore: Failed to close database\n");
        }
    } else {
        allinstance_set_busy(li); /* on-line mode */
    }

    return_value = tmp_rval?tmp_rval:return_value;

error_out:
    /* Free the restore src dir, but only if we allocated it above */
    if (real_src_dir && (real_src_dir != src_dir)) {
        /* If this was an FRI restore and the staging area exists, go ahead and remove it */
        if (frirestore && PR_Access(real_src_dir, PR_ACCESS_EXISTS) == PR_SUCCESS)
        {
            int ret1 = 0;
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "Restore: Removing staging area %s.\n", real_src_dir);
            ret1 = ldbm_delete_dirs(real_src_dir);
            if (ret1)
            {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "Restore: Removal of staging area %s failed!\n",
                              real_src_dir);
            }
        }
        slapi_ch_free_string(&real_src_dir);
    }
    slapi_ch_free_string(&changelogdir);
    return return_value;
}

/*
 * inst_dir_name is a relative path  (from 6.21)
 *     ==> txn log stores relative paths and becomes relocatable
 * if full path is given, parent dir is inst_parent_dir_name;
 * otherwise, inst_dir in home_dir
 *
 * Set an appropriate path to inst_dir_name, if not yet.
 * Create the specified directory, if not exists.
 */
int dblayer_get_instance_data_dir(backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    char *full_namep = NULL;
    char full_name[MAXPATHLEN];
    PRDir *db_dir = NULL;
    int ret = -1;

    /* if a specific directory name was specified for this particular 
     * instance use it othewise use the ldbm-wide one 
     */
    full_namep = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                           full_name, MAXPATHLEN);
    if (!full_namep || !*full_namep) {
        return ret;
    }
    /* Does this directory already exist? */
    if ((db_dir = PR_OpenDir(full_namep)) != NULL) {
        /* yep. */
        PR_CloseDir(db_dir);
        ret = 0;
    } else {
        /* nope -- create it. */
       ret = mkdir_p(full_namep, 0700);
    }

    if (full_name != full_namep)
        slapi_ch_free_string(&full_namep);
        
    return ret;
}

char *
dblayer_strerror(int error)
{
    return db_strerror(error);
}

/* [605974] check a db region file's existence to know whether import is executed by other process or not */
int
dblayer_in_import(ldbm_instance *inst)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int rval = 0;

    inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                          inst_dir, MAXPATHLEN);
    if (!inst_dirp || !*inst_dirp)
    {
        rval = -1;
        goto done;
    }
    dirhandle = PR_OpenDir(inst_dirp);

    if (NULL == dirhandle)
        goto done;

    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) 
    {
        if (NULL == direntry->name)
        {
            break;
        }
        if (0 ==strncmp(direntry->name, DB_REGION_PREFIX, 5))
        {
            rval = 1;
            break;
        }
    }
    PR_CloseDir(dirhandle);
done:
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

/*
 * to change the db extention (e.g., .db3 -> .db4)
 */
int dblayer_update_db_ext(ldbm_instance *inst, char *oldext, char *newext)
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

    if (NULL == inst)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "update_db_ext: Null instance is passed\n", 0, 0, 0);
        return -1;    /* non zero */
    }
    li = inst->inst_li;
    priv = (dblayer_private*)li->li_dblayer_private;
    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    if (NULL == inst_dirp || '\0' == *inst_dirp) {
        LDAPDebug(LDAP_DEBUG_ANY,
            "update_db_ext: instance dir is NULL\n", 0, 0, 0);
        return -1;    /* non zero */
    }
    for (a = (struct attrinfo *)avl_getfirst(inst->inst_attrs);
         NULL != a;
         a = (struct attrinfo *)avl_getnext())
    {
        PRFileInfo info;
        ofile = slapi_ch_smprintf("%s/%s%s", inst_dirp, a->ai_type, oldext);

        if (PR_GetFileInfo(ofile, &info) != PR_SUCCESS)
        {
            slapi_ch_free_string(&ofile);
            continue;
        }

        /* db->rename disable DB in it; we need to create for each */
        rval = db_create(&thisdb, priv->dblayer_env->dblayer_DB_ENV, 0);
        if (0 != rval)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "db_create returned %d (%s)\n",
                    rval, dblayer_strerror(rval), 0);
            goto done;
        }
        nfile = slapi_ch_smprintf("%s/%s%s", inst_dirp, a->ai_type, newext);
        LDAPDebug(LDAP_DEBUG_TRACE, "update_db_ext: rename %s -> %s\n",
            ofile, nfile, 0);

        rval = thisdb->rename(thisdb, (const char *)ofile, NULL /* subdb */,
                                  (const char *)nfile, 0);
        if (0 != rval)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "rename returned %d (%s)\n",
                rval, dblayer_strerror(rval), 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                "update_db_ext: index (%s) Failed to update index %s -> %s\n",
                inst->inst_name, ofile, nfile);
            goto done;
        }
        slapi_ch_free_string(&ofile);
        slapi_ch_free_string(&nfile);
    }

    rval = db_create(&thisdb, priv->dblayer_env->dblayer_DB_ENV, 0);
    if (0 != rval)
    {
        LDAPDebug(LDAP_DEBUG_ANY, "db_create returned %d (%s)\n",
                    rval, dblayer_strerror(rval), 0);
        goto done;
    }
    ofile = slapi_ch_smprintf("%s/%s%s", inst_dirp, ID2ENTRY, oldext);
    nfile = slapi_ch_smprintf("%s/%s%s", inst_dirp, ID2ENTRY, newext);
    LDAPDebug(LDAP_DEBUG_TRACE, "update_db_ext: rename %s -> %s\n",
            ofile, nfile, 0);
    rval = thisdb->rename(thisdb, (const char *)ofile, NULL /* subdb */,
                                  (const char *)nfile, 0);
    if (0 != rval)
    {
        LDAPDebug(LDAP_DEBUG_ANY, "rename returned %d (%s)\n",
                rval, dblayer_strerror(rval), 0);
        LDAPDebug(LDAP_DEBUG_ANY,
                "update_db_ext: index (%s) Failed to update index %s -> %s\n",
                inst->inst_name, ofile, nfile);
    }
done:
    slapi_ch_free_string(&ofile);
    slapi_ch_free_string(&nfile);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);

    return rval;
}

/*
 * delete the index files belonging to the instance
 */
int dblayer_delete_indices(ldbm_instance *inst)
{
    int rval = -1;
    struct attrinfo *a = NULL;
    int i;

    if (NULL == inst)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "update_index_ext: Null instance is passed\n", 0, 0, 0);
        return rval;
    }
    rval = 0;
    for (a = (struct attrinfo *)avl_getfirst(inst->inst_attrs), i = 0;
         NULL != a;
         a = (struct attrinfo *)avl_getnext(), i++)
    {
        rval += dblayer_erase_index_file(inst->inst_be, a, i/* chkpt; 1st time only */);
    }
    return rval;
}

void dblayer_set_recovery_required(struct ldbminfo *li)
{
    if (NULL == li || NULL == li->li_dblayer_private)
    {
        LDAPDebug(LDAP_DEBUG_ANY,"set_recovery_required: no dblayer info\n",
                  0, 0, 0);
        return;
    }
    li->li_dblayer_private->dblayer_recovery_required = 1;
}

int
ldbm_back_get_info(Slapi_Backend *be, int cmd, void **info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    switch (cmd) {
    case BACK_INFO_DBENV:
    {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        if (li) {
            dblayer_private *prv = (dblayer_private*)li->li_dblayer_private;
            if (prv && prv->dblayer_env && prv->dblayer_env->dblayer_DB_ENV) {
                *(DB_ENV **)info = prv->dblayer_env->dblayer_DB_ENV;
                rc = 0;
            }
        }
        break;
    }
    case BACK_INFO_DBENV_OPENFLAGS:
    {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        if (li) {
            dblayer_private *prv = (dblayer_private*)li->li_dblayer_private;
            if (prv && prv->dblayer_env) {
                *(int *)info = prv->dblayer_env->dblayer_openflags;
                rc = 0;
            }
        }
        break;
    }
    case BACK_INFO_INDEXPAGESIZE:
    {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        if (li) {
            dblayer_private *prv = (dblayer_private*)li->li_dblayer_private;
            if (prv && prv->dblayer_index_page_size) {
                *(size_t *)info = prv->dblayer_index_page_size;
            } else {
                *(size_t *)info = DBLAYER_INDEX_PAGESIZE;
            }
            rc = 0;
        }
        break;
    }
    case BACK_INFO_DIRECTORY:
    {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        if (li) {
            *(char **)info = li->li_directory;
            rc = 0;
        }
        break;
    }
    case BACK_INFO_LOG_DIRECTORY:
    {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        if (li) {
            *(char **)info = ldbm_config_db_logdirectory_get_ext((void *)li);
            rc = 0;
        }
        break;
    }
    default:
        break;
    }

    return rc;
}

int
ldbm_back_set_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    switch (cmd) {
    default:
        break;
    }

    return rc;
}

int
ldbm_back_ctrl_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    switch (cmd) {
    case BACK_INFO_CRYPT_INIT:
    {
        back_info_crypt_init *crypt_init = (back_info_crypt_init *)info;
        rc = back_crypt_init(crypt_init->be, crypt_init->dn,
                             crypt_init->encryptionAlgorithm, 
                             &(crypt_init->state_priv));
        break;
    }
    case BACK_INFO_CRYPT_ENCRYPT_VALUE:
    {
        back_info_crypt_value *crypt_value = (back_info_crypt_value *)info;
        rc = back_crypt_encrypt_value(crypt_value->state_priv, crypt_value->in, 
                                      &(crypt_value->out));
        break;
    }
    case BACK_INFO_CRYPT_DECRYPT_VALUE:
    {
        back_info_crypt_value *crypt_value = (back_info_crypt_value *)info;
        rc = back_crypt_decrypt_value(crypt_value->state_priv, crypt_value->in, 
                                      &(crypt_value->out));
        break;
    }
    default:
        break;
    }

    return rc;
}

static PRUintn thread_private_txn_stack;

typedef struct dblayer_txn_stack {
    PRCList list;
    back_txn txn;
} dblayer_txn_stack;

static void
dblayer_cleanup_txn_stack(void *arg)
{
    dblayer_txn_stack *txn_stack =  (dblayer_txn_stack *)arg;
    while (txn_stack && !PR_CLIST_IS_EMPTY(&txn_stack->list)) {
        dblayer_txn_stack *elem = (dblayer_txn_stack *)PR_LIST_HEAD(&txn_stack->list);
        PR_REMOVE_LINK(&elem->list);
        slapi_ch_free((void **)&elem);
    }
    if (txn_stack) {
        slapi_ch_free((void **)&txn_stack);
    }
    PR_SetThreadPrivate(thread_private_txn_stack, NULL);
    return;
}

static void
dblayer_init_pvt_txn()
{
    PR_NewThreadPrivateIndex(&thread_private_txn_stack, dblayer_cleanup_txn_stack);
}

static void
dblayer_push_pvt_txn(back_txn *txn)
{
    dblayer_txn_stack *new_elem = NULL;
    dblayer_txn_stack *txn_stack = PR_GetThreadPrivate(thread_private_txn_stack);
    if (!txn_stack) {
        txn_stack = (dblayer_txn_stack *)slapi_ch_calloc(1, sizeof(dblayer_txn_stack));
        PR_INIT_CLIST(&txn_stack->list);
        PR_SetThreadPrivate(thread_private_txn_stack, txn_stack);
    }
    new_elem = (dblayer_txn_stack *)slapi_ch_calloc(1, sizeof(dblayer_txn_stack));
    new_elem->txn = *txn; /* copy contents */
    PR_APPEND_LINK(&new_elem->list, &txn_stack->list);
}

static back_txn *
dblayer_get_pvt_txn()
{
    back_txn *txn = NULL;
    dblayer_txn_stack *txn_stack = PR_GetThreadPrivate(thread_private_txn_stack);
    if (txn_stack && !PR_CLIST_IS_EMPTY(&txn_stack->list)) {
        txn = &((dblayer_txn_stack *)PR_LIST_TAIL(&txn_stack->list))->txn;
    }
    return txn;
}

static void
dblayer_pop_pvt_txn()
{
    dblayer_txn_stack *elem = NULL;
    dblayer_txn_stack *txn_stack = PR_GetThreadPrivate(thread_private_txn_stack);
    if (txn_stack && !PR_CLIST_IS_EMPTY(&txn_stack->list)) {
        elem = (dblayer_txn_stack *)PR_LIST_TAIL(&txn_stack->list);
        PR_REMOVE_LINK(&elem->list);
        slapi_ch_free((void **)&elem);
    }
    return;
}
