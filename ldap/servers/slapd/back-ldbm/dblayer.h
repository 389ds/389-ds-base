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

/* Structures and #defines used in the dblayer. */

#ifndef _DBLAYER_H_
#define _DBLAYER_H_

#ifdef DB_USE_64LFS
#ifdef OS_solaris
#include <dlfcn.h> /* needed for dlopen and dlsym */
#endif             /* solaris: dlopen */
#ifdef OS_solaris
#include <sys/mman.h> /* needed for mmap/mmap64 */
#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif
#endif /* solaris: mmap */
#endif /* DB_USE_64LFS */

#define DBLAYER_PAGESIZE (uint32_t)8 * 1024
#define DBLAYER_INDEX_PAGESIZE (uint32_t)8 * 1024 /* With the new idl design,      \
     the large 8Kbyte pages we use are not optimal. The page pool churns very    \
     quickly as we add new IDs under a sustained add load. Smaller pages stop    \
     this happening so much and consequently make us spend less time flushing    \
     dirty pages on checkpoints.  But 8K is still a good page size for id2entry. \
     So we now allow different page sizes for the primary and secondary indices. \
     */

/* Interval, in ms, that threads sleep when they are wanting to
 * wait for a while withouth spinning. If this time is too long,
 * the server takes too long to shut down. If this interval is too
 * short, then CPU time gets burned by threads doing nothing.
 * As CPU speed increases over time, we reduce this interval
 * to allow the server to be more responsive to shutdown.
 * (Why is this important ? : A: because the TET tests start up
 * and shut down the server a gazillion times, so the server
 * shut down delay has a significant impact on the overall test
 * run time (which is very very very looooonnnnnggggg....).)
*/
#define DBLAYER_SLEEP_INTERVAL 250

#define DB_EXTN_PAGE_HEADER_SIZE 64 /* DBDB this is a guess */

#define DBLAYER_CACHE_FORCE_FILE 1

#define DBLAYER_LIB_VERSION_PRE_24 1
#define DBLAYER_LIB_VERSION_POST_24 2

typedef int dblayer_start_fn_t(struct ldbminfo *li, int flags);
typedef int dblayer_close_fn_t(struct ldbminfo *li, int flags);
typedef int dblayer_instance_start_fn_t(backend *be, int flags);
typedef int dblayer_backup_fn_t(struct ldbminfo *li, char *dest_dir, Slapi_Task *task);
typedef int dblayer_verify_fn_t(Slapi_PBlock *pb);
typedef int dblayer_db_size_fn_t(Slapi_PBlock *pb);
typedef int dblayer_ldif2db_fn_t(Slapi_PBlock *pb);
typedef int dblayer_db2ldif_fn_t(Slapi_PBlock *pb);
typedef int dblayer_db2index_fn_t(Slapi_PBlock *pb);
typedef int dblayer_cleanup_fn_t(struct ldbminfo *li);
typedef int dblayer_upgradedn_fn_t(Slapi_PBlock *pb);
typedef int dblayer_upgradedb_fn_t(Slapi_PBlock *pb);
typedef int dblayer_restore_fn_t(struct ldbminfo *li, char *src_dir, Slapi_Task *task);
typedef int dblayer_txn_begin_fn_t(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock);
typedef int dblayer_txn_commit_fn_t(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
typedef int dblayer_txn_abort_fn_t(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
typedef int dblayer_get_info_fn_t(Slapi_Backend *be, int cmd, void **info);
typedef int dblayer_set_info_fn_t(Slapi_Backend *be, int cmd, void **info);
typedef int dblayer_back_ctrl_fn_t(Slapi_Backend *be, int cmd, void *info);
typedef int dblayer_delete_db_fn_t(struct ldbminfo *li);
typedef int dblayer_load_dse_fn_t(struct ldbminfo *li);
typedef int dblayer_get_db_fn_t(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB);
typedef int dblayer_rm_db_file_fn_t(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt);
typedef int dblayer_import_fn_t(void *arg);
typedef void dblayer_config_get_fn_t(struct ldbminfo *li, char *attrname, char *value);
typedef int dblayer_config_set_fn_t(struct ldbminfo *li, char *attrname, int mod_apply, int mod_op, int phase, char *value);
typedef int instance_config_set_fn_t(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value);
typedef int instance_config_entry_callback_fn_t(struct ldbminfo *li, struct ldbm_instance *inst);
typedef int instance_cleanup_fn_t(struct ldbm_instance *inst);
typedef int instance_create_fn_t(struct ldbm_instance *inst);
typedef int instance_search_callback_fn_t(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst);
typedef int dblayer_auto_tune_fn_t(struct ldbminfo *li);

typedef char *dblayer_get_db_filename_fn_t(dbi_db_t *db);
typedef int dblayer_bulk_free_fn_t(dbi_bulk_t *bulkdata);
typedef int dblayer_bulk_nextdata_fn_t(dbi_bulk_t *bulkdata, dbi_val_t *data);
typedef int dblayer_bulk_nextrecord_fn_t(dbi_bulk_t *bulkdata, dbi_val_t *key, dbi_val_t *data);
typedef int dblayer_bulk_init_fn_t(dbi_bulk_t *bulkdata);
typedef int dblayer_bulk_start_fn_t(dbi_bulk_t *bulkdata);
typedef int dblayer_cursor_bulkop_fn_t(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_bulk_t *bulkdata);
typedef int dblayer_cursor_op_fn_t(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_val_t *data);
typedef int dblayer_db_op_fn_t(dbi_db_t *db,  dbi_txn_t *txn, dbi_op_t op, dbi_val_t *key, dbi_val_t *data);
typedef int dblayer_new_cursor_fn_t(dbi_db_t *db,  dbi_cursor_t *cursor);
typedef int dblayer_value_alloc_fn_t(dbi_val_t *data, size_t size);
typedef int dblayer_value_free_fn_t(dbi_val_t *data);
typedef int dblayer_value_init_fn_t(dbi_val_t *data);
typedef int dblayer_set_dup_cmp_fn_t(struct attrinfo *a, dbi_dup_cmp_t idx);
typedef int dblayer_dbi_txn_begin_fn_t(dbi_env_t *dbenv, PRBool readonly, dbi_txn_t *parent_txn, dbi_txn_t **txn);
typedef int dblayer_dbi_txn_commit_fn_t(dbi_txn_t *txn);
typedef int dblayer_dbi_txn_abort_fn_t(dbi_txn_t *txn);
typedef int dblayer_get_entries_count_fn_t(dbi_db_t *db, int *count);
typedef int dblayer_cursor_get_count_fn_t(dbi_cursor_t *cursor, dbi_recno_t *count);
typedef int dblayer_private_open_fn_t(const char *db_filename, dbi_env_t **env, dbi_db_t **db);
typedef int dblayer_private_close_fn_t(dbi_env_t **env, dbi_db_t **db);

struct dblayer_private
{
    /* common params for all backen implementations */
    int dblayer_file_mode;            /* pmode for files we create */
    int dblayer_bad_stuff_happened; /* Means that something happened (e.g. out
                                     * of disk space)*/
    int dblayer_idl_divisor;          /* divide page size by this to get IDL size */
                                      /* this is legacy and should go away, but it is not BDB specific */

    /* backend implementation specific data */
    void *dblayer_env;              /* specific database environment */

    /* functions to be provided by backend and assigned during backend init */
    dblayer_start_fn_t *dblayer_start_fn;
    dblayer_close_fn_t *dblayer_close_fn;
    dblayer_instance_start_fn_t *dblayer_instance_start_fn;
    dblayer_backup_fn_t *dblayer_backup_fn;
    dblayer_verify_fn_t *dblayer_verify_fn;
    dblayer_db_size_fn_t *dblayer_db_size_fn;
    dblayer_ldif2db_fn_t *dblayer_ldif2db_fn;
    dblayer_db2ldif_fn_t *dblayer_db2ldif_fn;
    dblayer_db2index_fn_t *dblayer_db2index_fn;
    dblayer_cleanup_fn_t *dblayer_cleanup_fn;
    dblayer_upgradedn_fn_t *dblayer_upgradedn_fn;
    dblayer_upgradedb_fn_t *dblayer_upgradedb_fn;
    dblayer_restore_fn_t *dblayer_restore_fn;
    dblayer_txn_begin_fn_t *dblayer_txn_begin_fn;
    dblayer_txn_commit_fn_t *dblayer_txn_commit_fn;
    dblayer_txn_abort_fn_t *dblayer_txn_abort_fn;
    dblayer_get_info_fn_t *dblayer_get_info_fn;
    dblayer_set_info_fn_t *dblayer_set_info_fn;
    dblayer_back_ctrl_fn_t *dblayer_back_ctrl_fn;
    dblayer_get_db_fn_t *dblayer_get_db_fn;
    dblayer_delete_db_fn_t *dblayer_delete_db_fn;
    dblayer_rm_db_file_fn_t *dblayer_rm_db_file_fn;
    dblayer_import_fn_t *dblayer_import_fn;
    dblayer_load_dse_fn_t *dblayer_load_dse_fn;
    dblayer_config_get_fn_t *dblayer_config_get_fn;
    dblayer_config_set_fn_t *dblayer_config_set_fn;
    instance_config_set_fn_t *instance_config_set_fn;
    instance_config_entry_callback_fn_t *instance_add_config_fn;
    instance_config_entry_callback_fn_t *instance_postadd_config_fn;
    instance_config_entry_callback_fn_t *instance_del_config_fn;
    instance_config_entry_callback_fn_t *instance_postdel_config_fn;
    instance_cleanup_fn_t *instance_cleanup_fn;
    instance_create_fn_t *instance_create_fn;
    instance_create_fn_t *instance_register_monitor_fn;
    instance_search_callback_fn_t *instance_search_callback_fn;
    dblayer_auto_tune_fn_t *dblayer_auto_tune_fn;

    dblayer_get_db_filename_fn_t *dblayer_get_db_filename_fn;
    dblayer_bulk_free_fn_t *dblayer_bulk_free_fn;
    dblayer_bulk_nextdata_fn_t *dblayer_bulk_nextdata_fn;
    dblayer_bulk_nextrecord_fn_t *dblayer_bulk_nextrecord_fn;
    dblayer_bulk_init_fn_t *dblayer_bulk_init_fn;
    dblayer_bulk_start_fn_t *dblayer_bulk_start_fn;
    dblayer_cursor_bulkop_fn_t *dblayer_cursor_bulkop_fn;
    dblayer_cursor_op_fn_t *dblayer_cursor_op_fn;
    dblayer_db_op_fn_t *dblayer_db_op_fn;
    dblayer_new_cursor_fn_t *dblayer_new_cursor_fn;
    dblayer_value_free_fn_t *dblayer_value_free_fn;
    dblayer_value_init_fn_t *dblayer_value_init_fn;
    dblayer_set_dup_cmp_fn_t *dblayer_set_dup_cmp_fn;
    dblayer_dbi_txn_begin_fn_t *dblayer_dbi_txn_begin_fn;
    dblayer_dbi_txn_commit_fn_t *dblayer_dbi_txn_commit_fn;
    dblayer_dbi_txn_abort_fn_t *dblayer_dbi_txn_abort_fn;
    dblayer_get_entries_count_fn_t *dblayer_get_entries_count_fn;
    dblayer_cursor_get_count_fn_t *dblayer_cursor_get_count_fn;
    dblayer_private_open_fn_t *dblayer_private_open_fn;
    dblayer_private_close_fn_t *dblayer_private_close_fn;
};

#define DBLAYER_PRIV_SET_DATA_DIR 0x1

void dblayer_init_pvt_txn(void);
void dblayer_push_pvt_txn(back_txn *txn);
back_txn *dblayer_get_pvt_txn(void);
void dblayer_pop_pvt_txn(void);

int dblayer_delete_indices(ldbm_instance *inst);
int dbimpl_setup(struct ldbminfo *li, const char *plgname);


/* Return the last four characters of a string; used for comparing extensions. */
char *last_four_chars(const char *s);

/* To support backingup/restoring changelog dir */
#define CHANGELOGENTRY "cn=changelog5,cn=config"
#define CHANGELOGDIRATTR "nsslapd-changelogdir"

#endif /* _DBLAYER_H_ */
