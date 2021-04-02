/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#include "../back-ldbm.h"
#include "../dblayer.h"
#include "../import.h"
#include <db.h>

#define BDB_CONFIG(li) ((mdb_config *)(li)->li_dblayer_config)

typedef struct mdb_db_env
{
#ifdef TODO
    DB_ENV *mdb_DB_ENV;
    Slapi_RWLock *mdb_env_lock;
    int mdb_openflags;
    int mdb_priv_flags;
    pthread_mutex_t mdb_thread_count_lock; /* lock for thread_count_cv */
    pthread_cond_t mdb_thread_count_cv;    /* condition variable for housekeeping thread shutdown */
    PRInt32 mdb_thread_count;              /* Tells us how many threads are running,
                                            * used to figure out when they're all stopped */
#endif /* TODO */
} mdb_db_env;

/* structure which holds our stuff */
typedef struct mdb_config
{
#ifdef TODO
    char *mdb_home_directory;
    char *mdb_log_directory;
    char *mdb_dbhome_directory;  /* default path for relative inst paths */
    char **mdb_data_directories; /* passed to set_data_dir
                                      * including mdb_dbhome_directory */
    char **mdb_db_config;
    int mdb_ncache;
    int mdb_previous_ncache;
    int mdb_tx_max;
    uint64_t mdb_cachesize;
    uint64_t mdb_previous_cachesize; /* Cache size when we last shut down--
                                        * used to determine if we delete
                                        * the mpool */
    int mdb_recovery_required;
    int mdb_txn_wait; /* Default is "off" (DB_TXN_NOWAIT) but for
                                     * support purpose it could be helpful to set
                                     * "on" so that backend hang on deadlock */
    int mdb_enable_transactions;
    int mdb_durable_transactions;
    int mdb_checkpoint_interval;
    int mdb_circular_logging;
    uint32_t mdb_page_size;       /* db page size if configured,
                                     * otherwise default to DBLAYER_PAGESIZE */
    uint32_t mdb_index_page_size; /* db index page size if configured,
                                     * otherwise default to
                                     * DBLAYER_INDEX_PAGESIZE */
    uint64_t mdb_logfile_size;    /* How large can one logfile be ? */
    uint64_t mdb_logbuf_size;     /* how large log buffer can be */
    int mdb_trickle_percentage;
    int mdb_cache_config; /* Special cache configurations
                                     * e.g. force file-based mpool */
    int mdb_lib_version;
    int mdb_spin_count;         /* DB Mutex spin count, 0 == use default */
    int mdb_named_regions;      /* Should the regions be named sections,
                                     * or backed by files ? */
    int mdb_private_mem;        /* private memory will be used for
                                     * allocation of regions and mutexes */
    int mdb_private_import_mem; /* private memory will be used for
                                     * allocation of regions and mutexes for
                                     * import */
    long mdb_shm_key;           /* base segment ID for named regions */
    int mdb_debug;                /* Will limdb emit debugging info into our log ? */
    int mdb_debug_verbose;              /* Get limdb to exhale debugging info */
    int mdb_debug_checkpointing;     /* Enable debugging messages from checkpointing */
    perfctrs_private *perf_private; /* Private data for performance counters code */
    int mdb_stop_threads;       /* Used to signal to threads that they should stop ASAP */
    int mdb_lockdown;           /* use DB_LOCKDOWN */
#define BDB_LOCK_NB_MIN 10000
    int mdb_lock_config;
    int mdb_previous_lock_config;  /* Max lock count when we last shut down--
                                      * used to determine if we delete the mpool */
    u_int32_t mdb_deadlock_policy; /* i.e. the atype to DB_ENV->lock_detect in mdb_deadlock_threadmain */
    int mdb_compactdb_interval;    /* interval to execute compact id2entry dbs */
#endif /* TODO */
} mdb_config;

int mdb_init(struct ldbminfo *li, config_info *config_array);

int mdb_close(struct ldbminfo *li, int flags);
int mdb_start(struct ldbminfo *li, int flags);
int mdb_instance_start(backend *be, int flags);
int mdb_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task);
int mdb_verify(Slapi_PBlock *pb);
int mdb_db2ldif(Slapi_PBlock *pb);
int mdb_db2index(Slapi_PBlock *pb);
int mdb_ldif2db(Slapi_PBlock *pb);
int mdb_db_size(Slapi_PBlock *pb);
int mdb_upgradedb(Slapi_PBlock *pb);
int mdb_upgradednformat(Slapi_PBlock *pb);
int mdb_upgradeddformat(Slapi_PBlock *pb);
int mdb_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task);
int mdb_cleanup(struct ldbminfo *li);
int mdb_txn_begin(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock);
int mdb_txn_commit(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int mdb_txn_abort(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int mdb_get_db(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB);
int mdb_rm_db_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt);
int mdb_delete_db(struct ldbminfo *li);
int mdb_public_mdb_import_main(void *arg);
int mdb_get_info(Slapi_Backend *be, int cmd, void **info);
int mdb_set_info(Slapi_Backend *be, int cmd, void **info);
int mdb_back_ctrl(Slapi_Backend *be, int cmd, void *info);
int mdb_config_load_dse_info(struct ldbminfo *li);
int mdb_config_internal_set(struct ldbminfo *li, char *attrname, char *value);
void mdb_public_config_get(struct ldbminfo *li, char *attrname, char *value);
int mdb_public_config_set(struct ldbminfo *li, char *attrname, int apply_mod, int mod_op, int phase, char *value);

/* dbimpl callbacks */
dblayer_get_db_filename_fn_t mdb_public_get_db_filename;
dblayer_bulk_free_fn_t mdb_public_bulk_free;
dblayer_bulk_nextdata_fn_t mdb_public_bulk_nextdata;
dblayer_bulk_nextrecord_fn_t mdb_public_bulk_nextrecord;
dblayer_bulk_init_fn_t mdb_public_bulk_init;
dblayer_bulk_start_fn_t mdb_public_bulk_start;
dblayer_cursor_bulkop_fn_t mdb_public_cursor_bulkop;
dblayer_cursor_op_fn_t mdb_public_cursor_op;
dblayer_db_op_fn_t mdb_public_db_op;
dblayer_new_cursor_fn_t mdb_public_new_cursor;
dblayer_value_free_fn_t mdb_public_value_free;
dblayer_value_init_fn_t mdb_public_value_init;
dblayer_set_dup_cmp_fn_t mdb_public_set_dup_cmp_fn;
dblayer_dbi_txn_begin_fn_t mdb_dbi_txn_begin;
dblayer_dbi_txn_commit_fn_t mdb_dbi_txn_commit;
dblayer_dbi_txn_abort_fn_t mdb_dbi_txn_abort;
dblayer_get_entries_count_fn_t mdb_get_entries_count;
dblayer_cursor_get_count_fn_t mdb_public_cursor_get_count;

/* instance functions */
int mdb_instance_cleanup(struct ldbm_instance *inst);
int mdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value);
int mdb_instance_create(struct ldbm_instance *inst);
int mdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst);

/* function for autotuning */
int mdb_start_autotune(struct ldbminfo *li);

/* helper functions */
int mdb_get_aux_id2entry(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path);
int mdb_get_aux_id2entry_ext(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path, int flags);
int mdb_release_aux_id2entry(backend *be, DB *pDB, DB_ENV *pEnv);
char *mdb_get_home_dir(struct ldbminfo *li, int *dbhome);
char *mdb_get_db_dir(struct ldbminfo *li);
int mdb_copy_directory(struct ldbminfo *li, Slapi_Task *task, char *src_dir, char *dest_dir, int restore, int *cnt, int indexonly, int is_changelog);
int mdb_remove_env(struct ldbminfo *li);
int mdb_bt_compare(DB *db, const DBT *dbt1, const DBT *dbt2);
int mdb_open_huge_file(const char *path, int oflag, int mode);
int mdb_check_and_set_import_cache(struct ldbminfo *li);
int mdb_close_file(DB **db);
int mdb_post_close(struct ldbminfo *li, int dbmode);
int mdb_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op);
void mdb_config_get(void *arg, config_info *config, char *buf);
int mdb_add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep, int *status);
int mdb_back_ldif2db(Slapi_PBlock *pb);
void mdb_set_recovery_required(struct ldbminfo *li);
void *mdb_config_db_logdirectory_get_ext(void *arg);
int mdb_db_remove(mdb_db_env *env, char const path[], char const dbName[]);
int mdb_memp_stat(struct ldbminfo *li, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp);
int mdb_memp_stat_instance(ldbm_instance *inst, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp);
void mdb_set_env_debugging(DB_ENV *pEnv, mdb_config *conf);
void mdb_back_free_incl_excl(char **include, char **exclude);
int mdb_back_ok_to_dump(const char *dn, char **include, char **exclude);
int mdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude);
PRUint64 mdb_get_id2entry_size(ldbm_instance *inst);

int mdb_idl_new_compare_dups(DB * db __attribute__((unused)), const DBT *a, const DBT *b);

int mdb_delete_indices(ldbm_instance *inst);
uint32_t mdb_get_optimal_block_size(struct ldbminfo *li);
int mdb_copyfile(char *source, char *destination, int overwrite, int mode);
int mdb_delete_instance_dir(backend *be);
int mdb_database_size(struct ldbminfo *li, unsigned int *size);
int mdb_set_batch_transactions(void *arg, void *value, char *errorbuf, int phase, int apply);
int mdb_set_batch_txn_min_sleep(void *arg, void *value, char *errorbuf, int phase, int apply);
int mdb_set_batch_txn_max_sleep(void *arg, void *value, char *errorbuf, int phase, int apply);
void *mdb_get_batch_transactions(void *arg);
void *mdb_get_batch_txn_min_sleep(void *arg);
void *mdb_get_batch_txn_max_sleep(void *arg);
int mdb_update_db_ext(ldbm_instance *inst, char *oldext, char *newext);
int mdb_restore_file_init(struct ldbminfo *li);
void mdb_restore_file_update(struct ldbminfo *li, const char *directory);
int mdb_import_file_init(ldbm_instance *inst);
void mdb_import_file_update(ldbm_instance *inst);
int mdb_import_file_check(ldbm_instance *inst);
int mdb_import_subcount_mother_init(import_subcount_stuff *mothers, ID parent_id, size_t count);
int mdb_import_subcount_mother_count(import_subcount_stuff *mothers, ID parent_id);
void mdb_import_configure_index_buffer_size(size_t size);
size_t mdb_import_get_index_buffer_size(void);
int mdb_ldbm_back_wire_import(Slapi_PBlock *pb);
void *mdb_factory_constructor(void *object, void *parent);
void mdb_factory_destructor(void *extension, void *object, void *parent);
int mdb_check_db_version(struct ldbminfo *li, int *action);
int mdb_check_db_inst_version(ldbm_instance *inst);
int mdb_adjust_idl_switch(char *ldbmversion, struct ldbminfo *li);
int mdb_ldbm_upgrade(ldbm_instance *inst, int action);
int mdb_lookup_dbversion(char *dbversion, int flag);
int mdb_dse_conf_backup(struct ldbminfo *li, char *destination_directory);
int mdb_dse_conf_verify(struct ldbminfo *li, char *src_dir);
int mdb_import_file_check_fn_t(ldbm_instance *inst);


/* dbimpl helpers */
backend *mdb_be(void);
void mdb_dbival2dbt(dbi_val_t *dbi, DBT *dbt, PRBool isresponse);
void mdb_dbt2dbival(DBT *dbt, dbi_val_t *dbi, PRBool isresponse);
int mdb_uses_locking(DB_ENV *db_env);
int mdb_uses_transactions(DB_ENV *db_env);
int mdb_uses_mpool(DB_ENV *db_env);
int mdb_uses_logging(DB_ENV *db_env);

/* mdb version functions */
int mdb_version_write(struct ldbminfo *li, const char *directory, const char *dataversion, PRUint32 flags);
int mdb_version_read(struct ldbminfo *li, const char *directory, char **ldbmversion, char **dataversion);
int mdb_version_exists(struct ldbminfo *li, const char *directory);

/* config functions */
int mdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int mdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int mdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int mdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
void mdb_config_setup_default(struct ldbminfo *li);

/* monitor functions */
int mdb_monitor_instance_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int mdb_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int mdb_dbmonitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int mdb_instance_register_monitor(ldbm_instance *inst);
void mdb_instance_unregister_monitor(ldbm_instance *inst);

/*
 * mdb_perfctrs.c
 */
void mdb_perfctrs_wait(size_t milliseconds, perfctrs_private *priv, DB_ENV *db_env);
void mdb_perfctrs_init(struct ldbminfo *li, perfctrs_private **priv);
void mdb_perfctrs_terminate(perfctrs_private **priv, DB_ENV *db_env);
void mdb_perfctrs_as_entry(Slapi_Entry *e, perfctrs_private *priv, DB_ENV *db_env);

/* mdb_import.c */
int mdb_import_fifo_validate_capacity_or_expand(ImportJob *job, size_t entrysize);
FifoItem *mdb_import_fifo_fetch(ImportJob *job, ID id, int worker);
void mdb_import_free_job(ImportJob *job);
void mdb_import_abort_all(ImportJob *job, int wait_for_them);
int mdb_import_entry_belongs_here(Slapi_Entry *e, backend *be);
int mdb_import_make_merge_filenames(char *directory, char *indexname, int pass, char **oldname, char **newname);
void mdb_import_main(void *arg);

/* mdb_import-merge.c */
int mdb_import_mega_merge(ImportJob *job);

/* mdb_import-threads.c */
void mdb_import_producer(void *param);
void mdb_index_producer(void *param);
void mdb_upgradedn_producer(void *param);
void mdb_import_foreman(void *param);
void mdb_import_worker(void *param);
