/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 *  Abstraction layer which sits between db2.0 and
 *  higher layers in the directory server---typically
 *  the back-end.
 *  This module's purposes are 1) to hide messy stuff which
 *  db2.0 needs, and with which we don't want to pollute the back-end
 *  code. 2) Provide some degree of portability to other databases
 *  if that becomes a requirement. Note that it is NOT POSSIBLE
 *  to revert to db1.85 because the backend is now using features
 *  from db2.0 which db1.85 does not have.
 *  Also provides an emulation of the ldbm_ functions, for anyone
 *  who is still calling those. The use of these functions is
 *  deprecated. Only for backwards-compatibility.
 *  Blame: dboreham
 */

/* Return code conventions:
 *  Unless otherwise advertised, all the functions in this module
 *  return an int which is zero if the operation was successful
 *  and non-zero if it wasn't. If the return'ed value was > 0,
 *  it can be interpreted as a system errno value. If it was < 0,
 *  its meaning is defined in dblayer.h
 */

/*
 *  Some information about how this stuff is to be used:
 *
 *  Call dblayer_init() near the beginning of the application's life.
 *  This allocates some resources and allows the config line processing
 *  stuff to work.
 *  Call dblayer_start() when you're sure all config stuff has been seen.
 *  This needs to be called before you can do anything else.
 *  Call dblayer_close() when you're finished using the db and want to exit.
 *  This closes and flushes all files opened by your application since calling
 *  dblayer_start. If you do NOT call dblayer_close(), we assume that the
 *  application crashed, and initiate recover next time you call dblayer_start().
 *  Call dblayer_terminate() after close. This releases resources.
 *
 *  dbi_db_t * handles are retrieved from dblayer via these functions:
 *
 *  dblayer_get_id2entry()
 *  dblayer_get_index_file()
 *
 *  the caller must honour the protocol that these handles are released back
 *  to dblayer when you're done using them, use thse functions to do this:
 *
 *  dblayer_release_id2entry()
 *  dblayer_release_index_file()
 */

#include <sys/types.h>
#include <sys/statvfs.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "back-ldbm.h"
#include "dblayer.h"
#include <prthread.h>
#include <prclist.h>

#define NEWDIR_MODE 0755
#define DB_REGION_PREFIX "__db."


static int dblayer_post_restore = 0;

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

/* routine that allows batch value to be changed remotely:

    1. value = 0 turns batching off
    2. value = 1 makes behavior be like 5.0 but leaves batching on
    3. value > 1 changes batch value

    2 and 3 assume that nsslapd-db-transaction-batch-val is greater 0 at startup
*/


/*
    Threading: dblayer isolates upper layers from threading considerations
    Everything in dblayer is free-threaded. That is, you can have multiple
    threads performing operations on a database and not worry about things.
    Obviously, if you do something stupid, like move a cursor forward in
    one thread, and backwards in another at the same time, you get what you
    deserve. However, such a calling pattern will not crash your application !
*/


/* Helper function which deletes the persistent state of the database library
 * IMHO this should be in inside libdb, but keith won't have it.
 * Stop press---libdb now does delete these files on recovery, so we don't call this any more.
 */

void
dblayer_remember_disk_filled(struct ldbminfo *li)
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


/* This function is called in the initialization code, before the
 * config file is read in, so we can't do much here
 */
int
dblayer_init(struct ldbminfo *li)
{
    /* Allocate memory we need, create mutexes etc. */
    dblayer_private *priv = NULL;
    int ret = 0;

    PR_ASSERT(NULL != li);
    if (NULL != li->li_dblayer_private) {
        return -1;
    }

    priv = (dblayer_private *)slapi_ch_calloc(1, sizeof(dblayer_private));
    if (NULL == priv) {
        /* Memory allocation failed */
        return -1;
    }
    li->li_dblayer_private = priv;

    return ret;
}

int
dbimpl_setup(struct ldbminfo *li, const char *plgname)
{
    int rc = 0;
    dblayer_private *priv = NULL;
    char *backend_implement_init = NULL;
    backend_implement_init_fn *backend_implement_init_x = NULL;

    /* initialize dblayer  */
    if (dblayer_init(li)) {
        slapi_log_err(SLAPI_LOG_CRIT, "dblayer_setup", "dblayer_init failed\n");
        return -1;
    }

    /* Fill in the fields of the ldbminfo and the dblayer_private
     * structures with some default values */
    ldbm_config_setup_default(li);

    if (!plgname) {
        ldbm_config_load_dse_info_phase0(li);
        plgname = li->li_backend_implement;
    }

    backend_implement_init = slapi_ch_smprintf("%s_init", plgname);
    backend_implement_init_x = sym_load(li->li_plugin->plg_libpath, backend_implement_init, "dblayer_implement", 1);
    slapi_ch_free_string(&backend_implement_init);

    if (backend_implement_init_x) {
        backend_implement_init_x(li, NULL);
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "dblayer_setup", "failed to init backend implementation\n");
        return -1;
    }

    if (plgname == li->li_backend_implement) {
        ldbm_config_load_dse_info_phase1(li);
        priv = (dblayer_private *)li->li_dblayer_private;
        rc = priv->dblayer_load_dse_fn(li);
    }

    return rc;
}

int dblayer_setup(struct ldbminfo *li)
{
    return dbimpl_setup(li, NULL);
}

/* Check a given filesystem directory for access we need */
#define DBLAYER_DIRECTORY_READ_ACCESS 1
#define DBLAYER_DIRECTORY_WRITE_ACCESS 2
#define DBLAYER_DIRECTORY_READWRITE_ACCESS 3

/* generate an absolute path if the given instance dir is not.  */
char *
dblayer_get_full_inst_dir(struct ldbminfo *li, ldbm_instance *inst, char *buf, int buflen)
{
    char *parent_dir = NULL;
    int mylen = 0;

    if (!inst)
        return NULL;

    if (inst->inst_parent_dir_name) /* e.g., /var/lib/dirsrv/slapd-ID/db */
    {
        parent_dir = inst->inst_parent_dir_name;
        mylen = strlen(parent_dir) + 1;
    } else {
        dblayer_private *priv = li->li_dblayer_private;
        priv->dblayer_get_info_fn(inst->inst_be, BACK_INFO_DB_DIRECTORY, (void **)&parent_dir);
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
    } else if (inst->inst_name) {
        inst->inst_dir_name = slapi_ch_strdup(inst->inst_name);
        mylen += strlen(inst->inst_dir_name) + 2;
        if (!buf || mylen > buflen)
            buf = slapi_ch_malloc(mylen);
        sprintf(buf, "%s%c%s",
                parent_dir, get_sep(parent_dir), inst->inst_dir_name);
    } else {
        mylen += 1;
        if (!buf || mylen > buflen)
            buf = slapi_ch_malloc(mylen);
        sprintf(buf, "%s", parent_dir);
    }
    return buf;
}


/*
 * This function is called after all the config options have been read in,
 * so we can do real initialization work here.
 */

int
dblayer_start(struct ldbminfo *li, int dbmode)
{
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    if (NULL == priv) {
        /* you didn't call init successfully */
        return -1;
    }

    return priv->dblayer_start_fn(li, dbmode);
}

/* mode is one of
 * DBLAYER_NORMAL_MODE,
 * DBLAYER_INDEX_MODE,
 * DBLAYER_IMPORT_MODE,
 * DBLAYER_EXPORT_MODE
 */
int
dblayer_instance_start(backend *be, int mode)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_instance_start_fn(be, mode);
}


/* This returns a dbi_db_t * for the primary index.
 * If the database library is non-reentrant, we lock it.
 * the caller MUST call to unlock the db library once they're
 * finished with the handle. Luckily, the back-end already has
 * these semantics for the older dbcache stuff.
 */
/* Things have changed since the above comment was
 * written.  The database library is reentrant. */
int
dblayer_get_id2entry(backend *be, dbi_db_t **ppDB)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);

    inst = (ldbm_instance *)be->be_instance_info;

    *ppDB = inst->inst_id2entry;
    return 0;
}

int
dblayer_release_id2entry(backend *be __attribute__((unused)), dbi_db_t *pDB __attribute__((unused)))
{
    return 0;
}

int
dblayer_close_changelog(backend *be)
{
    ldbm_instance *inst;
    dbi_db_t *pDB = NULL;
    int return_value = 0;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *) be->be_instance_info;
    PR_ASSERT(NULL != inst);

    pDB = inst->inst_changelog;
    if (pDB) {
        return_value = dblayer_db_op(be, pDB,  NULL, DBI_OP_CLOSE, NULL, NULL);
        inst->inst_changelog = NULL;
    }
    return return_value;
}

int
dblayer_erase_changelog_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt)
{
    if ((NULL == be) || (NULL == be->be_database)) {
        return 0;
    }
    /* TBD (LK) */
    return 0;
}

int
dblayer_close_indexes(backend *be)
{
    ldbm_instance *inst;
    dbi_db_t *pDB = NULL;
    dblayer_handle *handle = NULL;
    dblayer_handle *next = NULL;
    int return_value = 0;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *)be->be_instance_info;
    PR_ASSERT(NULL != inst);

    for (handle = inst->inst_handle_head; handle != NULL; handle = next) {
        /* Close it, and remove from the list */
        pDB = handle->dblayer_dbp;
        return_value = dblayer_db_op(be, pDB,  NULL, DBI_OP_CLOSE, NULL, NULL);
        next = handle->dblayer_handle_next;
        /* If the backpointer is still valid, NULL the attrinfos ref to us
         * This is important as there is no ordering guarantee between if the
         * handle or the attrinfo is freed first!
         */
        if (handle->dblayer_handle_ai_backpointer) {
            *((dblayer_handle **)handle->dblayer_handle_ai_backpointer) = NULL;
        }
        slapi_ch_free((void **)&handle);
    }

    /* reset the list to make sure we don't use it again */
    inst->inst_handle_tail = NULL;
    inst->inst_handle_head = NULL;

    return return_value;
}

int
dblayer_instance_close(backend *be)
{
    dbi_db_t *pDB = NULL;
    int return_value = 0;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (NULL == inst)
        return -1;

    if (!inst->inst_db) {
        be->be_state = BE_STATE_STOPPING;
    }
    if (getenv("USE_VALGRIND") || slapi_is_loglevel_set(SLAPI_LOG_CACHE)) {
        /*
         * if any string is set to an environment variable USE_VALGRIND,
         * when running a memory leak checking tool (e.g., valgrind),
         * it reduces the noise by enabling this code.
         */
        slapi_log_err(SLAPI_LOG_DEBUG, "dblayer_instance_close", "%s: Cleaning up entry cache\n",
                      inst->inst_name);
        cache_clear(&inst->inst_cache, CACHE_TYPE_ENTRY);
        slapi_log_err(SLAPI_LOG_DEBUG, "dblayer_instance_close", "%s: Cleaning up dn cache\n",
                      inst->inst_name);
        cache_clear(&inst->inst_dncache, CACHE_TYPE_DN);
    }

    if (attrcrypt_cleanup_private(inst)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dblayer_instance_close", "Failed to clean up attrcrypt system for %s\n",
                      inst->inst_name);
    }

    return_value = dblayer_close_indexes(be);
    return_value |= dblayer_close_changelog(be);

    /* Now close id2entry if it's open */
    pDB = inst->inst_id2entry;
    if (NULL != pDB) {
        return_value |= dblayer_db_op(be, pDB,  NULL, DBI_OP_CLOSE, NULL, NULL);
    }
    inst->inst_id2entry = NULL;

    if (inst->inst_db) {
        /* we have db specific instance data, do the cleanup */
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
        priv->instance_cleanup_fn(inst);
    } else {
        be->be_state = BE_STATE_STOPPED;
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
dblayer_close(struct ldbminfo *li, int dbmode)
{
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_close_fn(li, dbmode);
}

/* Routines for opening and closing random files in the dbi_env_t.
   Used by ldif2db merging code currently.

   Return value:
       Success: 0
    Failure: -1
 */
int
dblayer_open_file(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    PR_ASSERT(NULL != li);
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    return priv->dblayer_get_db_fn(be, indexname, open_flag, ai, ppDB);
}

int
dblayer_get_index_file(backend *be, struct attrinfo *a, dbi_db_t **ppDB, int open_flags)
{
    /*
     * We either already have a dbi_db_t * handle in the attrinfo structure.
     * in which case we simply return it to the caller, OR:
     * we need to make one. We do this as follows:
     * 1a) acquire the mutex that protects the handle list.
     * 1b) check that the dbi_db_t * is still null.
     * 2) get the filename, and call libdb to open it
     * 3) if successful, store the result in the attrinfo stucture
     * 4) store the dbi_db_t * in our own list so we can close it later.
     * 5) release the mutex.
     */
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int return_value = -1;
    dbi_db_t *pDB = NULL;
    char *attribute_name = a->ai_type;

    *ppDB = NULL;

    /* it's like a semaphore -- when count > 0, any file handle that's in
     * the attrinfo will remain valid from here on.
     */
    slapi_atomic_incr_64(&(a->ai_dblayer_count), __ATOMIC_RELEASE);

    if (a->ai_dblayer && ((dblayer_handle *)(a->ai_dblayer))->dblayer_dbp) {
        /* This means that the pointer is valid, so we should return it. */
        *ppDB = ((dblayer_handle *)(a->ai_dblayer))->dblayer_dbp;
        return 0;
    }

    /* attrinfo handle is NULL, at least for now -- grab the mutex and try again. */
    PR_Lock(inst->inst_handle_list_mutex);
    if (a->ai_dblayer && ((dblayer_handle *)(a->ai_dblayer))->dblayer_dbp) {
        /* another thread set the handle while we were waiting on the lock */
        *ppDB = ((dblayer_handle *)(a->ai_dblayer))->dblayer_dbp;
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
        dblayer_handle *handle = (dblayer_handle *)slapi_ch_calloc(1, sizeof(dblayer_handle));
        dblayer_handle *prev_handle = inst->inst_handle_tail;

        PR_ASSERT(NULL != pDB);
        /* Store the returned dbi_db_t * in our own private list of
         * open files */
        if (NULL == prev_handle) {
            /* List was empty */
            inst->inst_handle_tail = handle;
            inst->inst_handle_head = handle;
        } else {
            /* Chain the handle onto the last structure in the list */
            inst->inst_handle_tail = handle;
            prev_handle->dblayer_handle_next = handle;
        }
        /* Stash a pointer to our wrapper structure in the attrinfo structure */
        handle->dblayer_dbp = pDB;
        /* And, most importantly, return something to the caller!*/
        *ppDB = pDB;
        /* and save the hande in the attrinfo structure for next time */
        a->ai_dblayer = handle;
        /* don't need to update count -- we incr'd it already */
        handle->dblayer_handle_ai_backpointer = &(a->ai_dblayer);
    } else {
        /* Did not open it OK ! */
        /* Do nothing, because return value and fact that we didn't
         * store a dbi_db_t * in the attrinfo is enough */
    }
    PR_Unlock(inst->inst_handle_list_mutex);

    if (return_value != 0) {
        /* some sort of error -- we didn't open a handle at all.
         * decrement the refcount back to where it was.
         */
        slapi_atomic_decr_64(&(a->ai_dblayer_count), __ATOMIC_RELEASE);
    }

    return return_value;
}

int dblayer_get_changelog(backend *be, dbi_db_t ** ppDB, int open_flags)
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    int return_value = -1;
    dbi_db_t *pDB = NULL;

    *ppDB = NULL;

    if (inst->inst_changelog) {
        /* This means that the pointer is valid, so we should return it. */
        *ppDB = inst->inst_changelog;
        return 0;
    }

    /* only one thread should open the chgangelog, we can use the mutex
     * for opening the index files.
     */
    PR_Lock(inst->inst_handle_list_mutex);
    if (inst->inst_changelog) {
        /* another thread set the handle while we were waiting on the lock */
        *ppDB = inst->inst_changelog;
        PR_Unlock(inst->inst_handle_list_mutex);
        return 0;
    }

    /* attrinfo handle is still blank, and we have the mutex: open the
     * index file and stuff it in the attrinfo.
     */
    return_value = dblayer_open_file(be, BE_CHANGELOG_FILE, open_flags,
                                     NULL, &pDB);
    if (0 == return_value) {
        /* Opened it OK */
        inst->inst_changelog = pDB;
        /* And, most importantly, return something to the caller!*/
        *ppDB = pDB;
    } else {
        /* Did not open it OK ! */
        /* Do nothing, because return value and fact that we didn't
         * store a dbi_db_t * in the attrinfo is enough
         */
    }
    PR_Unlock(inst->inst_handle_list_mutex);

    return return_value;
}

/*
 * Unlock the db lib mutex here if we need to.
 */
int
dblayer_release_index_file(backend *be __attribute__((unused)), struct attrinfo *a, dbi_db_t *pDB __attribute__((unused)))
{
    slapi_atomic_decr_64(&(a->ai_dblayer_count), __ATOMIC_RELEASE);
    return 0;
}

int
dblayer_erase_index_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt)
{
    if ((NULL == be) || (NULL == be->be_database)) {
        return 0;
    }
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_rm_db_file_fn(be, a, use_lock, no_force_chkpt);
}


/*
 * Transaction stuff. The idea is that the caller doesn't need to
 * know the transaction mechanism underneath (because the caller is
 * typically a few calls up the stack from any dbi_db_t stuff).
 * Sadly, in slapd there was no handy structure associated with
 * an LDAP operation, and passed around everywhere, so we had
 * to invent the back_txn structure.
 * The lower levels of the back-end look into this structure, and
 * take out the dbi_txn_t they need.
 */
int
dblayer_txn_init(struct ldbminfo *li __attribute__((unused)), back_txn *txn)
{
    back_txn *cur_txn = dblayer_get_pvt_txn();
    PR_ASSERT(NULL != txn);

    if (cur_txn && txn) {
        txn->back_txn_txn = cur_txn->back_txn_txn;
        txn->back_special_handling_fn = NULL;
    } else if (txn) {
        txn->back_txn_txn = NULL;
        txn->back_special_handling_fn = NULL;
    }
    return 0;
}


int
dblayer_txn_begin_ext(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock)
{
    dblayer_private *priv = NULL;
    PR_ASSERT(NULL != li);
    /*
     * When server is shutting down, some components need to
     * flush some data (e.g. replication to write ruv).
     * So don't check shutdown signal unless we can't write.
     */
    if (g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL) {
        return -1;
    }

    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    return priv->dblayer_txn_begin_fn(li, parent_txn, txn, use_lock);

}

int
dblayer_read_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return (dblayer_txn_begin_ext(li, parent_txn, txn, PR_FALSE));
}

int
dblayer_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc = 0;
    if (DBLOCK_INSIDE_TXN(li)) {
        rc = dblayer_txn_begin_ext(li, parent_txn, txn, PR_TRUE);
        if (!rc && SERIALLOCK(li)) {
            dblayer_lock_backend(be);
        }
    } else {
        if (SERIALLOCK(li)) {
            dblayer_lock_backend(be);
        }
        rc = dblayer_txn_begin_ext(li, parent_txn, txn, PR_TRUE);
        if (rc && SERIALLOCK(li)) {
            dblayer_unlock_backend(be);
        }
    }
    return rc;
}


int
dblayer_txn_commit_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    dblayer_private *priv = NULL;
    PR_ASSERT(NULL != li);

    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    return priv->dblayer_txn_commit_fn(li, txn, use_lock);
}

int
dblayer_read_txn_commit(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return (dblayer_txn_commit_ext(li, txn, PR_FALSE));
}

int
dblayer_txn_commit(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc;
    if (DBLOCK_INSIDE_TXN(li)) {
        if (SERIALLOCK(li)) {
            dblayer_unlock_backend(be);
        }
        rc = dblayer_txn_commit_ext(li, txn, PR_TRUE);
    } else {
        rc = dblayer_txn_commit_ext(li, txn, PR_TRUE);
        if (SERIALLOCK(li)) {
            dblayer_unlock_backend(be);
        }
    }
    return rc;
}

int
dblayer_txn_abort_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock)
{
    dblayer_private *priv = NULL;

    PR_ASSERT(NULL != li);

    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    return priv->dblayer_txn_abort_fn(li, txn, use_lock);
}

int
dblayer_read_txn_abort(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return (dblayer_txn_abort_ext(li, txn, PR_FALSE));
}

int
dblayer_txn_abort(backend *be, back_txn *txn)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc;
    if (DBLOCK_INSIDE_TXN(li)) {
        if (SERIALLOCK(li)) {
            dblayer_unlock_backend(be);
        }
        rc = dblayer_txn_abort_ext(li, txn, PR_TRUE);
    } else {
        rc = dblayer_txn_abort_ext(li, txn, PR_TRUE);
        if (SERIALLOCK(li)) {
            dblayer_unlock_backend(be);
        }
    }
    return rc;
}

int
dblayer_txn_begin_all(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn)
{
    return (dblayer_txn_begin_ext(li, parent_txn, txn, PR_TRUE));
}

int
dblayer_txn_commit_all(struct ldbminfo *li, back_txn *txn)
{
    return (dblayer_txn_commit_ext(li, txn, PR_TRUE));
}

int
dblayer_txn_abort_all(struct ldbminfo *li, back_txn *txn)
{
    return (dblayer_txn_abort_ext(li, txn, PR_TRUE));
}

/*
 * The dblock serializes writes to the database,
 * which reduces deadlocking in the db code,
 * which means that we run faster.
 */
void
dblayer_lock_backend(backend *be)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);
    if (global_backend_lock_requested()) {
        global_backend_lock_lock();
    }
    inst = (ldbm_instance *)be->be_instance_info;
    PR_ASSERT(NULL != inst);

    if (NULL != inst->inst_db_mutex) {
        PR_EnterMonitor(inst->inst_db_mutex);
    }
}

void
dblayer_unlock_backend(backend *be)
{
    ldbm_instance *inst;

    PR_ASSERT(NULL != be);
    inst = (ldbm_instance *)be->be_instance_info;
    PR_ASSERT(NULL != inst);

    if (NULL != inst->inst_db_mutex) {
        PR_ExitMonitor(inst->inst_db_mutex);
    }

    if (global_backend_lock_requested()) {
        global_backend_lock_unlock();
    }
}


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


/* better atol -- it understands a trailing multiplier k/m/g
 * for example, "32k" will be returned as 32768
 * richm: added better error checking and support for 64 bit values.
 * The err parameter is used by the caller to tell if there was an error
 * during the a to i conversion - if 0, the value was successfully
 * converted - if non-zero, there was some error (e.g. not a number)
 */
PRInt64
db_atol(char *str, int *err)
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

PRInt64
db_atoi(char *str, int *err)
{
    return db_atol(str, err);
}

uint32_t
db_strtoul(const char *str, int *err)
{
    uint32_t val = 0, result, multiplier = 1;
    char *p;
    errno = 0;

    if (!str) {
        if (err) {
            *err = EINVAL;
        }
        return val;
    }
    /*
     * manpage of strtoul: Negative  values  are considered valid input and
     * are silently converted to the equivalent unsigned long int value.
     */
    /* We don't want to make it happen. */
    for (p = (char *)str; *p && (*p == ' ' || *p == '\t'); p++)
        ;
    if ('-' == *p) {
        if (err) {
            *err = ERANGE;
        }
        return val;
    }
    val = strtoul(str, &p, 10);
    if (errno != 0) {
        if (err) {
            *err = errno;
        }
        return val;
    }

    switch (*p) {
    case 't':
    case 'T':
        multiplier *= 1024 * 1024 * 1024;
        break;
    case 'g':
    case 'G':
        multiplier *= 1024 * 1024 * 1024;
        break;
    case 'm':
    case 'M':
        multiplier *= 1024 * 1024;
        break;
    case 'k':
    case 'K':
        multiplier *= 1024;
        p++;
        if (*p == 'b' || *p == 'B') {
            p++;
        }
        if (err) {
            /* extra chars? */
            *err = (*p != '\0') ? EINVAL : 0;
        }
        break;
    case '\0':
        if (err) {
            *err = 0;
        }
        break;
    default:
        if (err) {
            *err = EINVAL;
        }
        return val;
    }

    result = val * multiplier;

    return result;
}

uint64_t
db_strtoull(const char *str, int *err)
{
    uint64_t val = 0, result, multiplier = 1;
    char *p;
    errno = 0;

    if (!str) {
        if (err) {
            *err = EINVAL;
        }
        return -1L;
    }
    /*
     * manpage of strtoull: Negative  values  are considered valid input and
     * are silently converted to the equivalent unsigned long int value.
     */
    /* We don't want to make it happen. */
    for (p = (char *)str; *p && (*p == ' ' || *p == '\t'); p++)
        ;
    if ('-' == *p) {
        if (err) {
            *err = ERANGE;
        }
        return val;
    }
    val = strtoull(str, &p, 10);
    if (errno != 0) {
        if (err) {
            *err = errno;
        }
        return val;
    }

    switch (*p) {
    case 't':
    case 'T':
        multiplier *= 1024LL * 1024LL * 1024LL * 1024LL;
        break;
    case 'g':
    case 'G':
        multiplier *= 1024 * 1024 * 1024;
        break;
    case 'm':
    case 'M':
        multiplier *= 1024 * 1024;
        break;
    case 'k':
    case 'K':
        multiplier *= 1024;
        p++;
        if (*p == 'b' || *p == 'B') {
            p++;
        }
        if (err) {
            /* extra chars? */
            *err = (*p != '\0') ? EINVAL : 0;
        }
        break;
    case '\0':
        if (err) {
            *err = 0;
        }
        break;
    default:
        if (err) {
            *err = EINVAL;
        }
        return val;
    }

    result = val * multiplier;

    return result;
}
/* functions called directly by the plugin interface from the front-end */

/* Begin transaction */
int
dblayer_plugin_begin(Slapi_PBlock *pb)
{
    int return_value = -1;
    back_txnid parent = {0};
    back_txn current = {0};
    Slapi_Backend *be = NULL;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

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

    if (0 == return_value) {
        slapi_pblock_set(pb, SLAPI_TXN, (void *)current.back_txn_txn);
    }

    return return_value;
}

/* Commit transaction */
int
dblayer_plugin_commit(Slapi_PBlock *pb)
{
    /* get the txnid and call commit */
    int return_value = -1;
    back_txn current;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&(current.back_txn_txn));
    if (NULL == be) {
        return return_value;
    }

    return_value = dblayer_txn_commit(be, &current);

    return return_value;
}

/* Abort Transaction */
int
dblayer_plugin_abort(Slapi_PBlock *pb)
{
    /* get the txnid and call abort */
    int return_value = -1;
    back_txn current;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&(current.back_txn_txn));
    if (NULL == be) {
        return return_value;
    }

    return_value = dblayer_txn_abort(be, &current);

    return return_value;
}


/* Helper functions for recovery */

#define DB_LINE_LENGTH 80


/* And finally... Tubular Bells.
 * Well, no, actually backup and restore...
 */



/* Destination Directory is an absolute pathname */
int
dblayer_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task)
{
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_backup_fn(li, dest_dir, task);
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
dblayer_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task)
{
    dblayer_private *priv = NULL;

    PR_ASSERT(NULL != li);
    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);

    return priv->dblayer_restore_fn(li, src_dir, task);
}

void
dblayer_set_restored(void)
{
    dblayer_post_restore = 1;
}

int
dblayer_is_restored(void)
{
    return dblayer_post_restore;
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
int
dblayer_get_instance_data_dir(backend *be)
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
        if (full_namep != full_name) {
            slapi_ch_free_string(&full_namep);
        }
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

    if (full_name != full_namep) {
        slapi_ch_free_string(&full_namep);
    }

    return ret;
}

/* check whether import is executed by other process or not */
int
dblayer_in_import(ldbm_instance *inst)
{
    struct ldbminfo *li = (struct ldbminfo *)inst->inst_li;
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;
    return  prv->dblayer_in_import_fn(inst);
}

int
ldbm_back_get_info(Slapi_Backend *be, int cmd, void **info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    if (!li) {
        return rc;
    }
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;

    return  prv->dblayer_get_info_fn(be, cmd, info);
}

int
ldbm_back_set_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    if (!li) {
        return rc;
    }
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;

    return  prv->dblayer_set_info_fn(be, cmd, info);
}

int
ldbm_back_ctrl_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !info) {
        return rc;
    }

    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    if (!li) {
        return rc;
    }
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;

    return  prv->dblayer_back_ctrl_fn(be, cmd, info);
}

static PRUintn thread_private_txn_stack;

typedef struct dblayer_txn_stack
{
    PRCList list;
    back_txn txn;
} dblayer_txn_stack;

static void
dblayer_cleanup_txn_stack(void *arg)
{
    dblayer_txn_stack *txn_stack = (dblayer_txn_stack *)arg;
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

void
dblayer_init_pvt_txn(void)
{
    PR_NewThreadPrivateIndex(&thread_private_txn_stack, dblayer_cleanup_txn_stack);
}

void
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

back_txn *
dblayer_get_pvt_txn(void)
{
    back_txn *txn = NULL;
    dblayer_txn_stack *txn_stack = PR_GetThreadPrivate(thread_private_txn_stack);
    if (txn_stack && !PR_CLIST_IS_EMPTY(&txn_stack->list)) {
        txn = &((dblayer_txn_stack *)PR_LIST_TAIL(&txn_stack->list))->txn;
    }
    return txn;
}

void
dblayer_pop_pvt_txn(void)
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

const char *
dblayer_get_db_suffix(Slapi_Backend *be)
{
    struct ldbminfo *li = be ? (struct ldbminfo *)be->be_database->plg_private : NULL;
    dblayer_private *prv = li ? (dblayer_private *)li->li_dblayer_private : NULL;

    return  prv ? prv->dblayer_get_db_suffix_fn() : NULL;
}

int
ldbm_back_compact(Slapi_Backend *be, PRBool just_changelog)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int rc = -1;
    if (!li) {
        return rc;
    }
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;

    return  prv->dblayer_compact_fn(be, just_changelog);
}

int
dblayer_is_lmdb(Slapi_Backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    return (li->li_flags & LI_LMDB_IMPL);
}

/*
 * Iterate on the provided curor starting at startingkey (or first key if 
 *  startingkey is NULL) and call action_cb for each records
 * 
 * action_cb callback returns:
 *     DBI_RC_SUCCESS to iterate on next entry
 *     DBI_RC_NOTFOUND to stop iteration with DBI_RC_SUCCESS code
 *     other DBI_RC_ code to stop iteration with that error code.
 */
int dblayer_cursor_iterate(dbi_cursor_t *cursor, dbi_iterate_cb_t *action_cb,
                           const dbi_val_t *startingkey, void *ctx)
{
    struct ldbminfo *li = (struct ldbminfo *)cursor->be->be_database->plg_private;
    int rc = -1;
    if (!li) {
        return rc;
    }
    dblayer_private *prv = (dblayer_private *)li->li_dblayer_private;

    return prv->dblayer_cursor_iterate_fn(cursor, action_cb, startingkey, ctx);
}
